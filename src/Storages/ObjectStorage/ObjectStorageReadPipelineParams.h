#pragma once

#include <cstddef>
#include <memory>
#include <optional>

namespace DB
{

struct StorageID;
struct StorageSnapshot;
using StorageSnapshotPtr = std::shared_ptr<StorageSnapshot>;
struct FormatSettings;
struct ReadFromFormatInfo;
class IObjectStorage;
using ObjectStoragePtr = std::shared_ptr<IObjectStorage>;
class StorageObjectStorageConfiguration;
using StorageObjectStorageConfigurationPtr = std::shared_ptr<StorageObjectStorageConfiguration>;
struct IObjectIterator;
using ObjectIterator = std::shared_ptr<IObjectIterator>;
struct FormatParserSharedResources;
using FormatParserSharedResourcesPtr = std::shared_ptr<FormatParserSharedResources>;
struct FormatFilterInfo;
using FormatFilterInfoPtr = std::shared_ptr<FormatFilterInfo>;
struct LazyObjectStorageFileRegistry;
using LazyObjectStorageFileRegistryPtr = std::shared_ptr<LazyObjectStorageFileRegistry>;

/// Everything `ReadFromObjectStorageStep::initializePipeline` has already prepared for the read,
/// handed to a table engine which builds its own read topology instead of the generic one
/// (see `StorageObjectStorageConfiguration::buildReadPipe`).
/// Borrows from the step, so it only lives on the stack for the duration of that call.
struct ObjectStorageReadPipelineParams
{
    const StorageID & storage_id;
    const ObjectStoragePtr & object_storage;
    const StorageObjectStorageConfigurationPtr & configuration;
    const StorageSnapshotPtr & storage_snapshot;
    /// May have been rewritten by `updatePrewhereInfo` — this is the only valid version of it.
    const ReadFromFormatInfo & info;
    const std::optional<FormatSettings> & format_settings;
    /// Already created and shared by every stream — consume it instead of listing the files again,
    /// so that distributed processing, archives and file progress stay in one place.
    const ObjectIterator & iterator;
    const FormatParserSharedResourcesPtr & parser_shared_resources;
    const FormatFilterInfoPtr & format_filter_info;
    const LazyObjectStorageFileRegistryPtr & lazy_row_index_registry;
    size_t max_block_size;
    size_t num_streams;
    /// The step appends a `Resize` when the returned pipe has fewer output ports than this and
    /// `parallelize_output_from_storages` is on. A pipe whose port order carries meaning should
    /// already have this many output ports.
    size_t max_parallel_output_streams;
    bool need_only_count;
};

}
