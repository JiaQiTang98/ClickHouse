#pragma once

#include <Storages/ObjectStorage/ReadUnitKind.h>
#include <Storages/ObjectStorage/StorageObjectStorageSource.h>

namespace DB
{

/// Reads the read units of a table engine whose unit is not a data file that `FormatFactory` can
/// decode from a `ReadBuffer` (`ReadUnitKind::SelfOpening`). Everything about the read except
/// opening one unit is inherited unchanged - `generate` fills in the hive partition, virtual and
/// row lineage columns exactly as it does for a data file, and prefetches the next unit the same
/// way - so the only member here is `createReader`.
///
/// The step resolves the engine's `ReadUnitOpener` once, at pipeline-build time, and constructs
/// this class instead of `StorageObjectStorageSource` (see
/// `ReadFromObjectStorageStep::initializePipeline`). Nothing about the read unit kind is therefore
/// visible inside the shared read path.
class SelfOpeningObjectStorageSource final : public StorageObjectStorageSource
{
public:
    /// `unit_opener_` comes first so that it cannot be confused with the base class parameters that
    /// carry default values.
    SelfOpeningObjectStorageSource(
        ReadUnitOpener unit_opener_,
        const StorageID & storage_id_,
        String name_,
        ObjectStoragePtr object_storage_,
        StorageObjectStorageConfigurationPtr configuration,
        StorageSnapshotPtr storage_snapshot_,
        const ReadFromFormatInfo & info,
        const std::optional<FormatSettings> & format_settings_,
        ContextPtr context_,
        UInt64 max_block_size_,
        std::shared_ptr<IObjectIterator> file_iterator_,
        FormatParserSharedResourcesPtr parser_shared_resources_,
        FormatFilterInfoPtr format_filter_info_,
        bool need_only_count_,
        LazyObjectStorageFileRegistryPtr lazy_row_index_registry_ = nullptr);

    /// Named apart from the base class so that `EXPLAIN PIPELINE` shows that the units of this read
    /// are opened by the table engine. The engine's own reader is not made of `IProcessor`s, so its
    /// internals stay invisible either way - only the fact is visible.
    String getName() const override { return "SelfOpening" + name; }

protected:
    ReaderHolder createReader() override;

private:
    const ReadUnitOpener unit_opener;
};

}
