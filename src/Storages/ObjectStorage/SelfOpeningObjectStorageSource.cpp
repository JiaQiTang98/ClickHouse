#include <Storages/ObjectStorage/SelfOpeningObjectStorageSource.h>

#include <Formats/FormatFilterInfo.h>
#include <Interpreters/ActionsDAG.h>
#include <QueryPipeline/QueryPipelineBuilder.h>
#include <Storages/ObjectStorage/IObjectIterator.h>
#include <Storages/ObjectStorage/StorageObjectStorageConfiguration.h>
#include <Storages/SelectQueryInfo.h>
#include <Storages/prepareReadingFromFormat.h>
#include <Common/logger_useful.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
    extern const int NOT_IMPLEMENTED;
}

SelfOpeningObjectStorageSource::SelfOpeningObjectStorageSource(
    ReadUnitOpener unit_opener_,
    const StorageID & storage_id_,
    String name_,
    ObjectStoragePtr object_storage_,
    StorageObjectStorageConfigurationPtr configuration_,
    StorageSnapshotPtr storage_snapshot_,
    const ReadFromFormatInfo & info,
    const std::optional<FormatSettings> & format_settings_,
    ContextPtr context_,
    UInt64 max_block_size_,
    std::shared_ptr<IObjectIterator> file_iterator_,
    FormatParserSharedResourcesPtr parser_shared_resources_,
    FormatFilterInfoPtr format_filter_info_,
    bool need_only_count_,
    LazyObjectStorageFileRegistryPtr lazy_row_index_registry_)
    : StorageObjectStorageSource(
          storage_id_,
          std::move(name_),
          std::move(object_storage_),
          std::move(configuration_),
          std::move(storage_snapshot_),
          info,
          format_settings_,
          std::move(context_),
          max_block_size_,
          std::move(file_iterator_),
          std::move(parser_shared_resources_),
          std::move(format_filter_info_),
          need_only_count_,
          std::move(lazy_row_index_registry_))
    , unit_opener(std::move(unit_opener_))
{
}

StorageObjectStorageSource::ReaderHolder SelfOpeningObjectStorageSource::createReader()
{
    /// No loop and no metadata probe, unlike the data-file path: a self-opening unit is not an
    /// object in the storage, so there is nothing to look up, nothing to skip as an empty object,
    /// and no row groups for the query condition cache to prune.
    auto unit = file_iterator->next(0);
    if (!unit || unit->getPath().empty())
        return {};

    /// The unit must carry its own synthetic `relative_path_with_metadata` - a stable path and the
    /// known total size - because that is what keeps `_path` / `_file` / `_size` / `_time` working
    /// unchanged in the inherited `generate`, which dereferences the metadata without checking.
    /// A unit that did not fill it in is a bug in the table engine, not a case to paper over by
    /// probing a path that does not exist.
    if (!unit->getObjectMetadata())
        throw Exception(
            ErrorCodes::LOGICAL_ERROR,
            "A self-opening read unit must carry its own object metadata, but unit '{}' of table engine {} has none",
            unit->getPath(),
            configuration->getEngineName());

    /// `AddingDefaultsTransform` asks the input format which columns the file actually contained,
    /// so it cannot be built without one. Rather than substitute a guess about a unit whose
    /// contents only the engine knows, refuse the read.
    if (read_from_format_info.columns_description.hasDefaults())
        throw Exception(
            ErrorCodes::NOT_IMPLEMENTED,
            "Column defaults are not implemented for the `{}` read unit kind",
            SettingFieldReadUnitKindTraits::toString(ReadUnitKind::SelfOpening));

    /// A self-opening reader is treated as one that does not support `PREWHERE`, so both filters
    /// are stripped unconditionally and re-applied as `FilterTransform`s below. That is the
    /// conservative reading of what a lake SDK means when it accepts a predicate: usually it prunes
    /// files and row groups, not that every row it returns satisfies the predicate. An engine that
    /// can prune still does - the unstripped `format_filter_info` is bound into `unit_opener` by
    /// `StorageObjectStorageConfiguration::resolveReadUnitOpener`.
    FilterDAGInfoPtr stripped_row_level_filter;
    PrewhereInfoPtr stripped_prewhere_info;
    if (format_filter_info)
    {
        stripped_row_level_filter = format_filter_info->row_level_filter;
        stripped_prewhere_info = format_filter_info->prewhere_info;
    }

    /// `format_header` is post-`PREWHERE`: `updateFormatPrewhereInfo` already dropped the columns
    /// that only the stripped filters read. Add them back, so that the `FilterTransform`s below can
    /// evaluate against them. There is no schema-changed variant of this here - schema evolution
    /// happens inside the unit, which hands over rows already in the current schema.
    Block header = read_from_format_info.format_header;
    auto add_filter_inputs = [&](const ActionsDAG & dag)
    {
        for (const auto & required : dag.getRequiredColumns())
        {
            if (!header.has(required.name))
                header.insert({required.type, required.name});
        }
    };
    if (stripped_row_level_filter)
        add_filter_inputs(stripped_row_level_filter->actions);
    if (stripped_prewhere_info)
        add_filter_inputs(stripped_prewhere_info->prewhere_actions);

    LOG_DEBUG(
        log,
        "Reading self-opening read unit '{}', size: {} bytes",
        unit->getPath(),
        unit->getObjectMetadata()->size_bytes);

    auto source = unit_opener(unit, header, max_block_size);
    if (!source)
        throw Exception(
            ErrorCodes::LOGICAL_ERROR,
            "The read unit opener of table engine {} returned no source for unit '{}'",
            configuration->getEngineName(),
            unit->getPath());

    QueryPipelineBuilder builder;
    builder.init(Pipe(source));
    addStrippedFilterTransforms(builder, stripped_row_level_filter, stripped_prewhere_info);

    /// No `read_buf` and no `IInputFormat`: the engine's reader owns whatever I/O it needs. That is
    /// also what makes `ReaderHolder::getInputFormat` return null, so the null-reader guards the
    /// inherited `generate` already has - written for the count-from-cache path, which is likewise
    /// a unit that produces rows without reading a file - apply here unchanged.
    ///
    /// No row lineage columns either: those are produced by the format reader of an Iceberg data
    /// object, which a self-opening unit is not.
    return finishReader(
        std::move(builder), std::move(unit), /*read_buf=*/nullptr, std::move(source), /*row_lineage_columns=*/{}, read_from_format_info);
}

}
