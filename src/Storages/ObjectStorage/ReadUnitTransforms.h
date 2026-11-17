#pragma once

#include <mutex>
#include <Core/Names.h>
#include <Formats/FormatSettings.h>
#include <Interpreters/StorageID.h>
#include <Processors/ISimpleTransform.h>
#include <Storages/ObjectStorage/ReadUnitContext.h>
#include <Storages/prepareReadingFromFormat.h>

namespace DB
{

class Pipe;
class FilterTransform;

class StorageObjectStorageConfiguration;
using StorageObjectStorageConfigurationPtr = std::shared_ptr<StorageObjectStorageConfiguration>;

struct FormatParserSharedResources;
using FormatParserSharedResourcesPtr = std::shared_ptr<FormatParserSharedResources>;

/// What the processors after a flat `StorageObjectStorageSource` (`object_storage_flat_read_pipeline`)
/// do, the same for all data files of a read. Computed at the planning time.
struct ReadUnitPipelineDescription
{
    /// The header of the chunks of the source (H_pre): the columns of `post_filter_header` and the inputs of
    /// the row-level filter and `PREWHERE`, by the names in the table schema.
    SharedHeader pre_filter_header;
    /// The header after the row-level filter and `PREWHERE` (H_post): the columns `requested_columns` are extracted from.
    SharedHeader post_filter_header;

    /// The filters which the format applies for the data files which support them,
    /// and `ReadUnitFilterTransform` applies for the others. May be null.
    FilterDAGInfoPtr row_level_filter;
    PrewhereInfoPtr prewhere_info;

    /// The columns of `pre_filter_header` which are not read from a data file the format applied the filters for,
    /// because they are needed only for the filters.
    NameSet columns_missing_if_filters_applied_by_reader;
    /// The columns of `pre_filter_header` which the filters compute (e.g. `plus(a, 1)` for `PREWHERE a + 1 > 5`).
    /// They are not read from a data file the format did not apply the filters for.
    NameSet columns_computed_by_filters;

    /// The transforms specific to a data file, applied before the filters in this order. Each data lake makes its
    /// own (e.g. the Iceberg position deletes, the DeltaLake deletion vectors), see `getReadUnitTransformFactories`.
    std::vector<ReadUnitTransformFactory> transform_factories;

    /// For `ExtractColumnsTransform` and `ReadUnitColumnsTransform`.
    ReadFromFormatInfo read_from_format_info;
    StorageObjectStorageConfigurationPtr configuration;
    StorageID storage_id = StorageID::createEmpty();
    std::optional<FormatSettings> format_settings;
    FormatParserSharedResourcesPtr parser_shared_resources;
    ContextPtr context;

    bool hasFilters() const { return row_level_filter || prewhere_info; }
};

using ReadUnitPipelineDescriptionPtr = std::shared_ptr<const ReadUnitPipelineDescription>;

/// Applies to each chunk the transform at position `index` of `ReadUnitContext::transforms` of the read unit
/// (data file) of the chunk, e.g. the position deletes of the data file. The transforms are made by the source,
/// see `ReadUnitTransformFactory`.
class ReadUnitSwitchingTransform final : public ISimpleTransform
{
public:
    ReadUnitSwitchingTransform(SharedHeader header_, String name_, size_t index_);

    String getName() const override { return name; }

    void transform(Chunk & chunk) override;

protected:
    void onCancel() noexcept override;

private:
    const String name;
    const size_t index;

    ReadUnitContextPtr current_context;
    /// Guards `current_transform` from `onCancel`, which may be called in parallel with `transform`.
    std::mutex current_transform_mutex;
    std::shared_ptr<ISimpleTransform> current_transform;
};

/// Applies the row-level filter and `PREWHERE` to the chunks of the data files the format did not apply them for,
/// and brings the chunks from `pre_filter_header` to `post_filter_header`.
class ReadUnitFilterTransform final : public ISimpleTransform
{
public:
    explicit ReadUnitFilterTransform(const ReadUnitPipelineDescription & description);

    String getName() const override { return "ReadUnitFilterTransform"; }

    void transform(Chunk & chunk) override;

protected:
    void onCancel() noexcept override;

private:
    /// The positions in `pre_filter_header` of the columns of `post_filter_header`.
    std::vector<size_t> positions_if_filters_applied_by_reader;

    /// The positions in `pre_filter_header` of the columns of the input header of the first filter.
    std::vector<size_t> filters_input_positions;
    /// Null if there is no such filter. Only `transform` of them is called.
    std::shared_ptr<FilterTransform> row_level_filter_transform;
    std::shared_ptr<FilterTransform> prewhere_transform;
    /// The output header of the last filter.
    SharedHeader filters_output_header;
    /// The positions in `filters_output_header` of the columns of `post_filter_header`.
    std::vector<size_t> filters_output_positions;
};

/// Adds the columns which are not read from the data file (see `StorageObjectStorageSource::addReadUnitColumns`)
/// and removes `ChunkInfoReadUnit`, so that the read unit is not kept alive by the chunks buffered further.
class ReadUnitColumnsTransform final : public ISimpleTransform
{
public:
    ReadUnitColumnsTransform(SharedHeader header_, ReadUnitPipelineDescriptionPtr description_);

    String getName() const override { return "ReadUnitColumnsTransform"; }

    void transform(Chunk & chunk) override;

private:
    const ReadUnitPipelineDescriptionPtr description;
};

/// Returns the context of the read unit `chunk` was read from. Throws if `chunk` has no `ChunkInfoReadUnit`.
const ReadUnitContext & getReadUnitContext(const Chunk & chunk);

/// Appends to each stream of a flat `StorageObjectStorageSource` the processors which do the processing
/// of the read units which is not done by the source: the deletes, the stripped filters, the extraction
/// of the subcolumns and the columns which are not read from the data files.
void addReadUnitTransforms(Pipe & pipe, const ReadUnitPipelineDescriptionPtr & description);

}
