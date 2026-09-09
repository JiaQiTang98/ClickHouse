#pragma once

#include <Core/Block_fwd.h>
#include <Core/SettingsEnums.h>

#include <functional>


namespace DB
{

/// How one unit of work in the read path of an object storage table is produced.
enum class ReadUnitKind : uint8_t
{
    /// One data file, decoded from a `ReadBuffer` by a `FormatFactory` input format.
    /// `StorageObjectStorageSource` assembles the transform chain around it: the deletes,
    /// the schema evolution, the `PREWHERE` fallback filters and the column defaults.
    FormatFile,
    /// The unit produces its rows itself, through a `ReadUnitOpener` the step resolves before
    /// the read starts. The table engine's semantics — merge-on-read, deletion vectors, schema
    /// evolution, type promotion — are already applied inside it, so
    /// `StorageObjectStorageSource` only adds the virtual and hive partition columns around it,
    /// exactly as it does for a data file.
    SelfOpening,
};

DECLARE_SETTING_ENUM(ReadUnitKind)

/// Only forward declarations here: this header is included by `DataLakeStorageSettings.h`, which the
/// whole storage layer sees, so it must not pull in `Processors/ISource.h`.
class ISource;
using SourcePtr = std::shared_ptr<ISource>;
struct ObjectInfo;
using ObjectInfoPtr = std::shared_ptr<ObjectInfo>;

/// Opens one read unit as a source, for a table engine whose unit is not a format-readable file.
/// Resolved once per read by `ReadFromObjectStorageStep::initializePipeline` (see
/// `StorageObjectStorageConfiguration::resolveReadUnitOpener`), so only the per-unit parameters are
/// left here — everything that is fixed for the whole read is already bound into the callable.
using ReadUnitOpener = std::function<SourcePtr(ObjectInfoPtr unit, const Block & header, size_t max_block_size)>;

}
