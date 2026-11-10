#pragma once

#include <Core/Field.h>
#include <Processors/Chunk.h>

namespace DB
{

struct ObjectInfo;
using ObjectInfoPtr = std::shared_ptr<ObjectInfo>;

class ExpressionActions;
using ExpressionActionsPtr = std::shared_ptr<ExpressionActions>;

/// A delete of the rows of a data file which have some values (e.g. an Iceberg equality delete file),
/// as a filter over the chunks read from the file, before the schema transform.
struct EqualityDeleteFilter
{
    ExpressionActionsPtr actions;
    /// The column added by `actions`, which is false for the deleted rows.
    String filter_column_name;
};

/// What the processors after `StorageObjectStorageSource` need to know about the data file (read unit)
/// a chunk was read from, when the per-file processing is done in the main query pipeline
/// (`object_storage_flat_read_pipeline`). Created by the source on the first chunk of a file and
/// not changed afterwards, so the processors can tell the files apart by the pointer.
struct ReadUnitContext
{
    ObjectInfoPtr object_info;
    /// Whether the format reader applied the row-level filter and `PREWHERE`. If not, they are applied
    /// by `ReadUnitFilterTransform`.
    bool filters_applied_by_reader = true;
    /// The index of the file in `LazyObjectStorageFileRegistry`, set only for lazy materialization.
    std::optional<UInt64> lazy_file_index;
    /// The response headers of the file for `_headers`, set only if `_headers` is requested.
    std::optional<Map> headers;
    std::optional<UInt64> data_lake_snapshot_version;
};

using ReadUnitContextPtr = std::shared_ptr<const ReadUnitContext>;

/// Attached by `StorageObjectStorageSource` to every chunk of a flat read pipeline, removed by
/// `ReadUnitColumnsTransform`.
class ChunkInfoReadUnit : public ChunkInfoCloneable<ChunkInfoReadUnit>
{
public:
    explicit ChunkInfoReadUnit(ReadUnitContextPtr context_) : context(std::move(context_)) {}
    ChunkInfoReadUnit(const ChunkInfoReadUnit & other) = default;

    ReadUnitContextPtr context;
};

}
