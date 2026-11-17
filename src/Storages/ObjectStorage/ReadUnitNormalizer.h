#pragma once

#include <atomic>
#include <boost/dynamic_bitset.hpp>
#include <Core/Block.h>
#include <Core/Names.h>
#include <Interpreters/ActionsDAG.h>
#include <Storages/ObjectStorage/ReadUnitContext.h>

namespace DB
{

class BlockMissingValues;
class ColumnsDescription;
class ColumnDefaultsFiller;

/// Brings the chunks read by the format from one data file to the header which is the same for all
/// data files of a read (H_pre), so that the further per-file processing can be done by the processors
/// of the main query pipeline (`object_storage_flat_read_pipeline`). In this order:
/// - replaces the identity-partitioned columns with their values from the table metadata;
/// - applies the equality deletes, which refer to the columns by the names in the data file;
/// - applies the schema transform (renames, type changes, columns added by schema evolution);
/// - replaces the values the format did not find in the file with the column defaults;
/// - extracts the tuple elements H_pre has as columns of their own (see `filterTupleColumnsToRead`) from their
///   columns, which a data file read under another schema has as a whole after the schema transform;
/// - casts the columns to the types of H_pre, adds the placeholders and drops the others.
/// Tracks the mask of the values the format did not find through these steps by the column names.
class ReadUnitNormalizer
{
public:
    ReadUnitNormalizer(
        SharedHeader input_header_,
        std::optional<ActionsDAG> identity_partition_columns_dag,
        /// Built for `input_header_`, which replacing the identity-partitioned columns does not change.
        std::vector<EqualityDeleteFilter> equality_delete_filters_,
        std::optional<ActionsDAG> schema_transform,
        const ColumnsDescription & columns_description,
        ContextPtr context,
        SharedHeader output_header_,
        NameSet placeholder_columns_);

    ~ReadUnitNormalizer();

    /// `missing_values` describes the values of `chunk` the format did not find in the file, may be null.
    /// May leave `chunk` without rows, e.g. if all rows were deleted.
    void normalize(Chunk & chunk, const BlockMissingValues * missing_values, const std::atomic<bool> & is_cancelled);

    /// Interrupts the default expressions being evaluated right now, if any.
    void cancel() noexcept;

private:
    using RowsBitMask = boost::dynamic_bitset<>;
    using MissingValuesByName = std::unordered_map<String, RowsBitMask>;

    void applyEqualityDeleteFilter(
        const EqualityDeleteFilter & filter, Block & block, size_t & num_rows, Chunk & chunk, MissingValuesByName & missing_values);
    void projectToOutputHeader(Block & block, size_t num_rows, Chunk & chunk) const;

    const SharedHeader input_header;
    ExpressionActionsPtr identity_partition_columns_actions;
    /// The names of the columns replaced by `identity_partition_columns_actions`.
    NameSet identity_partition_column_names;
    const std::vector<EqualityDeleteFilter> equality_delete_filters;
    ExpressionActionsPtr schema_transform_actions;
    /// For an output of `schema_transform_actions` which is an input renamed or cast, the name of the input.
    std::unordered_map<String, String> schema_transform_output_to_input;
    /// Null if there are no defaults.
    std::unique_ptr<ColumnDefaultsFiller> defaults_filler;
    /// Null if all columns of `output_header` are in the chunks already.
    ExpressionActionsPtr subcolumns_extraction_actions;
    const SharedHeader output_header;
    /// The columns of `output_header` which are not used for this data file: the inputs of the filters the format
    /// applied, or the outputs of `PREWHERE` which are computed after. They are added with default values even if
    /// the chunk has them, because the column read from the file may not be convertible to the type of `output_header`
    /// (e.g. a `Nullable` column of an old data file which the table schema made required).
    const NameSet placeholder_columns;
};

}
