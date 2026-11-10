#include <Storages/ObjectStorage/ReadUnitTransforms.h>

#include <AggregateFunctions/AggregateFunctionGroupBitmapData.h>
#include <Interpreters/ExpressionActions.h>
#include <Interpreters/castColumn.h>
#include <Processors/Transforms/ExtractColumnsTransform.h>
#include <Processors/Transforms/FilterTransform.h>
#include <QueryPipeline/Pipe.h>
#include <Storages/ObjectStorage/DataLakes/DeletionVectorTransform.h>
#include <Storages/ObjectStorage/StorageObjectStorageConfiguration.h>
#include <Storages/ObjectStorage/StorageObjectStorageSource.h>
#include <Storages/SelectQueryInfo.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

namespace
{

std::vector<size_t> getPositionsByName(const Block & from, const Block & to)
{
    std::vector<size_t> positions;
    positions.reserve(to.columns());
    for (const auto & column : to)
    {
        if (!from.has(column.name))
            throw Exception(
                ErrorCodes::LOGICAL_ERROR,
                "Column {} is not found in the header of the chunks of a flat read pipeline, which has columns: {}",
                column.name,
                from.dumpNames());
        positions.push_back(from.getPositionByName(column.name));
    }
    return positions;
}

/// Takes the columns of `chunk` at `positions` and casts them to the types of `header`.
void projectChunk(Chunk & chunk, const Block & from, const std::vector<size_t> & positions, const Block & header)
{
    const size_t num_rows = chunk.getNumRows();
    const auto & columns = chunk.getColumns();
    Columns result;
    result.reserve(positions.size());
    for (size_t i = 0; i < positions.size(); ++i)
    {
        const auto & from_column = from.getByPosition(positions[i]);
        const auto & to_column = header.getByPosition(i);
        if (from_column.type->equals(*to_column.type))
            result.push_back(columns[positions[i]]);
        else
            result.push_back(castColumn({columns[positions[i]], from_column.type, from_column.name}, to_column.type));
    }
    chunk.setColumns(std::move(result), num_rows);
}

}

const ReadUnitContext & getReadUnitContext(const Chunk & chunk)
{
    auto info = chunk.getChunkInfos().get<ChunkInfoReadUnit>();
    if (!info || !info->context)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Chunk of a flat read pipeline does not have ChunkInfoReadUnit");
    /// The context is kept alive by the chunk.
    return *info->context;
}

ReadUnitSwitchingTransform::ReadUnitSwitchingTransform(SharedHeader header_, String name_, Factory factory_)
    : ISimpleTransform(header_, header_, /*skip_empty_chunks=*/true)
    , name(std::move(name_))
    , factory(std::move(factory_))
{
}

void ReadUnitSwitchingTransform::transform(Chunk & chunk)
{
    auto info = chunk.getChunkInfos().get<ChunkInfoReadUnit>();
    if (!info || !info->context)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Chunk of a flat read pipeline does not have ChunkInfoReadUnit");

    if (info->context != current_context)
    {
        /// Release the previous transform (e.g. the delete files it keeps open) before making the next one.
        {
            std::lock_guard lock(current_transform_mutex);
            current_transform.reset();
        }
        current_context = info->context;

        auto transform = factory(*current_context, getInputPort().getSharedHeader());
        std::lock_guard lock(current_transform_mutex);
        current_transform = std::move(transform);
        if (current_transform && isCancelled())
            current_transform->cancel();
    }

    if (!current_transform)
        return;

    current_transform->transform(chunk);
    if (chunk.getNumRows() == 0)
        chunk.clear();
}

void ReadUnitSwitchingTransform::onCancel() noexcept
{
    std::lock_guard lock(current_transform_mutex);
    if (current_transform)
        current_transform->cancel();
}

ReadUnitFilterTransform::ReadUnitFilterTransform(const ReadUnitPipelineDescription & description)
    : ISimpleTransform(description.pre_filter_header, description.post_filter_header, /*skip_empty_chunks=*/true)
{
    const auto & pre_filter_header = *description.pre_filter_header;
    const auto & post_filter_header = *description.post_filter_header;

    positions_if_filters_applied_by_reader = getPositionsByName(pre_filter_header, post_filter_header);

    /// For a data file the format did not apply the filters for, the columns the filters compute are placeholders.
    Block filters_input_header;
    for (const auto & column : pre_filter_header)
        if (!description.columns_computed_by_filters.contains(column.name))
            filters_input_header.insert(column.cloneEmpty());
    filters_input_positions = getPositionsByName(pre_filter_header, filters_input_header);

    auto header = std::make_shared<const Block>(std::move(filters_input_header));

    /// The same as the filters which `StorageObjectStorageSource::buildNestedReader` adds, see the comment there:
    /// the row-level filter first, and it keeps its input columns.
    if (const auto & row_level_filter = description.row_level_filter)
    {
        const auto & filter_node = row_level_filter->actions.findInOutputs(row_level_filter->column_name);
        auto row_level_dag = ActionsDAG::cloneSubDAG({&filter_node}, /*remove_aliases=*/ true);
        auto & row_level_outputs = row_level_dag.getOutputs();
        const auto * row_level_filter_node = row_level_outputs.front();
        row_level_outputs.clear();

        bool remove_row_level_filter_column = row_level_filter->do_remove_column;
        if (row_level_filter_node->type == ActionsDAG::ActionType::INPUT)
            remove_row_level_filter_column = false;
        else
            row_level_outputs.push_back(row_level_filter_node);

        row_level_outputs.insert(row_level_outputs.end(), row_level_dag.getInputs().begin(), row_level_dag.getInputs().end());

        const String filter_column_name = row_level_filter_node->result_name;
        auto row_level_actions = std::make_shared<ExpressionActions>(std::move(row_level_dag));
        row_level_filter_transform = std::make_shared<FilterTransform>(
            header, row_level_actions, filter_column_name, remove_row_level_filter_column,
            /*on_totals=*/false, /*rows_filtered=*/nullptr, /*condition=*/std::nullopt,
            /*update_row_numbers_info=*/true);
        header = row_level_filter_transform->getOutputPort().getSharedHeader();
    }

    if (const auto & prewhere_info = description.prewhere_info)
    {
        auto prewhere_actions = std::make_shared<ExpressionActions>(prewhere_info->prewhere_actions.clone());
        prewhere_transform = std::make_shared<FilterTransform>(
            header, prewhere_actions, prewhere_info->prewhere_column_name, prewhere_info->remove_prewhere_column,
            /*on_totals=*/false, /*rows_filtered=*/nullptr, /*condition=*/std::nullopt,
            /*update_row_numbers_info=*/true);
        header = prewhere_transform->getOutputPort().getSharedHeader();
    }

    filters_output_header = header;
    filters_output_positions = getPositionsByName(*filters_output_header, post_filter_header);
}

void ReadUnitFilterTransform::transform(Chunk & chunk)
{
    const auto & pre_filter_header = getInputPort().getHeader();
    const auto & post_filter_header = getOutputPort().getHeader();

    if (getReadUnitContext(chunk).filters_applied_by_reader)
    {
        projectChunk(chunk, pre_filter_header, positions_if_filters_applied_by_reader, post_filter_header);
        return;
    }

    {
        const size_t num_rows = chunk.getNumRows();
        auto columns = chunk.detachColumns();
        Columns filters_input;
        filters_input.reserve(filters_input_positions.size());
        for (size_t position : filters_input_positions)
            filters_input.push_back(std::move(columns[position]));
        chunk.setColumns(std::move(filters_input), num_rows);
    }

    for (const auto & filter : {row_level_filter_transform, prewhere_transform})
    {
        if (!filter)
            continue;
        filter->transform(chunk);
        /// The filter leaves the chunk without columns if it is cancelled or all rows are filtered out.
        if (chunk.getNumRows() == 0)
        {
            chunk.clear();
            return;
        }
    }

    projectChunk(chunk, *filters_output_header, filters_output_positions, post_filter_header);
}

void ReadUnitFilterTransform::onCancel() noexcept
{
    if (row_level_filter_transform)
        row_level_filter_transform->cancel();
    if (prewhere_transform)
        prewhere_transform->cancel();
}

ReadUnitColumnsTransform::ReadUnitColumnsTransform(SharedHeader header_, ReadUnitPipelineDescriptionPtr description_)
    : ISimpleTransform(
          header_, std::make_shared<const Block>(description_->read_from_format_info.source_header), /*skip_empty_chunks=*/false)
    , description(std::move(description_))
{
}

void ReadUnitColumnsTransform::transform(Chunk & chunk)
{
    auto info = chunk.getChunkInfos().extract<ChunkInfoReadUnit>();
    if (!info || !info->context)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Chunk of a flat read pipeline does not have ChunkInfoReadUnit");
    const auto & unit = *info->context;

    StorageObjectStorageSource::addReadUnitColumns(
        chunk,
        {
            .configuration = description->configuration,
            .read_from_format_info = description->read_from_format_info,
            .storage_id = description->storage_id,
            .format_settings = description->format_settings,
            .context = description->context,
            .data_lake_snapshot_version = unit.data_lake_snapshot_version,
        },
        {
            .object_info = *unit.object_info,
            .read_from_file = true,
            .lazy_file_index = unit.lazy_file_index,
            .headers = unit.headers ? &*unit.headers : nullptr,
        });
}

void addReadUnitTransforms(Pipe & pipe, const ReadUnitPipelineDescriptionPtr & description)
{
    if (pipe.empty())
        return;

    /// The deletion vectors come with the data files (e.g. from DeltaKernel, or in the read tasks of a cluster
    /// read), so they are applied for any storage.
    pipe.addSimpleTransform([](const SharedHeader & header)
    {
        return std::make_shared<ReadUnitSwitchingTransform>(
            header,
            "ReadUnitDeletionVectorTransform",
            [](const ReadUnitContext & unit, const SharedHeader & unit_header) -> std::shared_ptr<ISimpleTransform>
            {
                const auto & data_lake_metadata = unit.object_info->data_lake_metadata;
                if (!data_lake_metadata || !data_lake_metadata->excluded_rows || data_lake_metadata->excluded_rows->size() == 0)
                    return nullptr;
                return std::make_shared<DeletionVectorTransform>(unit_header, data_lake_metadata->excluded_rows);
            });
    });

    description->configuration->addReadUnitTransforms(
        pipe, description->format_settings, description->parser_shared_resources, description->context);

    if (description->hasFilters())
    {
        pipe.addSimpleTransform([&](const SharedHeader &)
        {
            return std::make_shared<ReadUnitFilterTransform>(*description);
        });
    }

    pipe.addSimpleTransform([&](const SharedHeader & header)
    {
        return std::make_shared<ExtractColumnsTransform>(header, description->read_from_format_info.requested_columns);
    });

    pipe.addSimpleTransform([&](const SharedHeader & header)
    {
        return std::make_shared<ReadUnitColumnsTransform>(header, description);
    });
}

}
