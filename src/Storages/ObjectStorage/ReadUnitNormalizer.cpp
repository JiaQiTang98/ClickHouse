#include <Storages/ObjectStorage/ReadUnitNormalizer.h>

#include <Columns/ColumnConst.h>
#include <Columns/FilterDescription.h>
#include <Columns/IColumn.h>
#include <Core/BlockMissingValues.h>
#include <Functions/IFunction.h>
#include <Interpreters/ExpressionActions.h>
#include <Interpreters/castColumn.h>
#include <Processors/Transforms/AddingDefaultsTransform.h>
#include <Processors/Transforms/FilterTransform.h>
#include <Storages/ColumnsDescription.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

namespace
{

/// The input whose value `node` has, possibly renamed or cast, or null if `node` computes something else.
const ActionsDAG::Node * findInputOfRenameOrCast(const ActionsDAG::Node * node)
{
    while (node)
    {
        if (node->type == ActionsDAG::ActionType::INPUT)
            return node;
        if (node->type == ActionsDAG::ActionType::ALIAS)
        {
            node = node->children.front();
            continue;
        }
        if (node->type == ActionsDAG::ActionType::FUNCTION && node->function_base && !node->children.empty())
        {
            const auto & function_name = node->function_base->getName();
            if (function_name == "_CAST" || function_name == "CAST" || function_name == "materialize")
            {
                node = node->children.front();
                continue;
            }
        }
        return nullptr;
    }
    return nullptr;
}

}

ReadUnitNormalizer::ReadUnitNormalizer(
    SharedHeader input_header_,
    std::optional<ActionsDAG> identity_partition_columns_dag,
    std::vector<EqualityDeleteFilter> equality_delete_filters_,
    std::optional<ActionsDAG> schema_transform,
    const ColumnsDescription & columns_description,
    ContextPtr context,
    SharedHeader output_header_,
    NameSet placeholder_columns_)
    : input_header(std::move(input_header_))
    , equality_delete_filters(std::move(equality_delete_filters_))
    , output_header(std::move(output_header_))
    , placeholder_columns(std::move(placeholder_columns_))
{
    if (identity_partition_columns_dag)
    {
        for (const auto * output : identity_partition_columns_dag->getOutputs())
            if (output->type != ActionsDAG::ActionType::INPUT)
                identity_partition_column_names.insert(output->result_name);
        identity_partition_columns_actions = std::make_shared<ExpressionActions>(std::move(*identity_partition_columns_dag));
    }

    if (schema_transform)
    {
        for (const auto * output : schema_transform->getOutputs())
            if (const auto * input = findInputOfRenameOrCast(output))
                schema_transform_output_to_input.emplace(output->result_name, input->result_name);
        schema_transform_actions = std::make_shared<ExpressionActions>(std::move(*schema_transform));
    }

    if (columns_description.hasDefaults())
        defaults_filler = std::make_unique<ColumnDefaultsFiller>(columns_description, context);
}

ReadUnitNormalizer::~ReadUnitNormalizer() = default;

void ReadUnitNormalizer::normalize(Chunk & chunk, const BlockMissingValues * missing_values, const std::atomic<bool> & is_cancelled)
{
    size_t num_rows = chunk.getNumRows();
    Block block = input_header->cloneWithColumns(chunk.detachColumns());

    /// The mask is needed only to fill the defaults.
    MissingValuesByName missing_values_by_name;
    if (defaults_filler && missing_values)
    {
        const size_t num_columns = std::min(missing_values->getNumColumns(), block.columns());
        for (size_t i = 0; i < num_columns; ++i)
            if (missing_values->hasDefaultBits(i))
                missing_values_by_name.emplace(block.getByPosition(i).name, missing_values->getDefaultsBitmask(i));
    }

    if (identity_partition_columns_actions)
    {
        identity_partition_columns_actions->execute(block, num_rows, /*dry_run=*/false, /*allow_duplicates_in_input=*/false, &is_cancelled);
        /// The values come from the table metadata, not from the file.
        for (const auto & name : identity_partition_column_names)
            missing_values_by_name.erase(name);
    }

    for (const auto & filter : equality_delete_filters)
    {
        applyEqualityDeleteFilter(filter, block, num_rows, chunk, missing_values_by_name);
        if (num_rows == 0)
            return;
    }

    if (schema_transform_actions)
    {
        schema_transform_actions->execute(block, num_rows, /*dry_run=*/false, /*allow_duplicates_in_input=*/false, &is_cancelled);

        MissingValuesByName remapped;
        for (const auto & [output_name, input_name] : schema_transform_output_to_input)
        {
            auto it = missing_values_by_name.find(input_name);
            if (it != missing_values_by_name.end())
                remapped.emplace(output_name, it->second);
        }
        missing_values_by_name = std::move(remapped);
    }

    if (defaults_filler && !missing_values_by_name.empty())
    {
        BlockMissingValues block_missing_values(block.columns());
        for (size_t i = 0; i < block.columns(); ++i)
        {
            auto it = missing_values_by_name.find(block.getByPosition(i).name);
            if (it == missing_values_by_name.end())
                continue;
            /// The mask may have fewer bits than rows.
            for (size_t row = it->second.find_first(); row != RowsBitMask::npos && row < num_rows; row = it->second.find_next(row))
                block_missing_values.setBit(i, row);
        }

        Chunk filled(block.getColumns(), num_rows);
        defaults_filler->fill(filled, block.cloneEmpty(), block_missing_values, is_cancelled);
        if (is_cancelled)
        {
            chunk.clear();
            return;
        }
        block.setColumns(filled.detachColumns());
    }

    projectToOutputHeader(block, num_rows, chunk);
}

void ReadUnitNormalizer::applyEqualityDeleteFilter(
    const EqualityDeleteFilter & filter, Block & block, size_t & num_rows, Chunk & chunk, MissingValuesByName & missing_values)
{
    filter.actions->execute(block, num_rows);

    ColumnPtr filter_column = block.getByName(filter.filter_column_name).column;
    block.erase(filter.filter_column_name);

    FilterDescription filter_description(*filter_column);
    const size_t num_rows_after = filter_description.countBytesInFilter();
    if (num_rows_after == num_rows)
        return;

    for (auto & column : block)
        column.column = filter_description.filter(*column.column, num_rows_after);

    /// `chunk` has no columns now, but still has the chunk infos.
    FilterTransform::updateRowNumbersInfo(chunk, *filter_description.data);

    const auto & filter_data = *filter_description.data;
    for (auto & [_, mask] : missing_values)
    {
        RowsBitMask filtered;
        const size_t mask_size = std::min(mask.size(), filter_data.size());
        for (size_t row = 0; row < mask_size; ++row)
            if (filter_data[row])
                filtered.push_back(mask[row]);
        mask = std::move(filtered);
    }

    num_rows = num_rows_after;
}

void ReadUnitNormalizer::projectToOutputHeader(Block & block, size_t num_rows, Chunk & chunk) const
{
    Columns columns;
    columns.reserve(output_header->columns());
    for (const auto & output_column : *output_header)
    {
        if (placeholder_columns.contains(output_column.name))
        {
            columns.push_back(output_column.type->createColumnConstWithDefaultValue(num_rows));
        }
        else if (const auto * column = block.findByName(output_column.name))
        {
            if (column->type->equals(*output_column.type))
                columns.push_back(column->column);
            else
                columns.push_back(castColumn(*column, output_column.type));
        }
        else
        {
            throw Exception(
                ErrorCodes::LOGICAL_ERROR,
                "Column {} is not found in the chunk read from a data file, which has columns: {}",
                output_column.name,
                block.dumpNames());
        }
    }
    chunk.setColumns(std::move(columns), num_rows);
}

void ReadUnitNormalizer::cancel() noexcept
{
    if (defaults_filler)
        defaults_filler->cancel();
}

}
