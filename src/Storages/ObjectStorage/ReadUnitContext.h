#pragma once

#include <functional>
#include <optional>
#include <Core/Block_fwd.h>
#include <Core/Field.h>
#include <Processors/Chunk.h>

namespace DB
{

struct ObjectInfo;
using ObjectInfoPtr = std::shared_ptr<ObjectInfo>;

class ExpressionActions;
using ExpressionActionsPtr = std::shared_ptr<ExpressionActions>;

class ISimpleTransform;
struct FormatSettings;

/// A delete of the rows of a data file which have some values (e.g. an Iceberg equality delete file),
/// as a filter over the chunks read from the file, before the schema transform.
struct EqualityDeleteFilter
{
    ExpressionActionsPtr actions;
    /// The column added by `actions`, which is false for the deleted rows.
    String filter_column_name;
};

/// Makes a transform which applies something specific to a data file (read unit), e.g. its position deletes,
/// to the chunks read from it. It is called by the source in the thread which opens the data file, so it may
/// read files (e.g. the delete files) in parallel with reading the previous data file. The transform is applied
/// by a `ReadUnitSwitchingTransform` named `name`, and must not change the header.
struct ReadUnitTransformFactory
{
    String name;
    /// Returns null if nothing has to be done for the data file. The format settings are the ones the data file
    /// is read with, which may differ from the ones of the read (e.g. a Parquet file with streamed Iceberg position
    /// deletes is read in order), the same as `addDeleteTransformers` gets in the nested read pipeline.
    std::function<std::shared_ptr<ISimpleTransform>(
        const ObjectInfoPtr &, const SharedHeader &, const std::optional<FormatSettings> &)> make;
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
    /// Made by the factories `ReadUnitPipelineDescription::transform_factories`, at the same positions (null if
    /// nothing has to be done). Only the `ReadUnitSwitchingTransform` of a position uses its transform.
    std::vector<std::shared_ptr<ISimpleTransform>> transforms;
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
