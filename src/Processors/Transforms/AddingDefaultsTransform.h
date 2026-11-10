#pragma once

#include <Processors/ISimpleTransform.h>
#include <Storages/ColumnsDescription.h>

#include <mutex>


namespace DB
{

class IInputFormat;
class BlockMissingValues;

class ExpressionActions;
using ExpressionActionsPtr = std::shared_ptr<ExpressionActions>;

/// Replaces the values marked in a `BlockMissingValues` bitmask with the column defaults.
/// Not a processor, so that a reader which tracks the bitmask itself can use it.
class ColumnDefaultsFiller
{
public:
    ColumnDefaultsFiller(const ColumnsDescription & columns_, ContextPtr context_);

    /// `header` describes the columns of `chunk`; the column positions of `missing_values` refer to it,
    /// and its rows to the rows of `chunk`. If `is_cancelled` is set, `chunk` may be left empty.
    void fill(Chunk & chunk, const Block & header, const BlockMissingValues & missing_values, const std::atomic<bool> & is_cancelled);

    /// Interrupts the default expressions being evaluated by `fill` right now, if any.
    void cancel() noexcept;

private:
    const ColumnsDescription columns;
    const ColumnDefaults column_defaults;
    ContextPtr context;

    /// The default-expression actions are built per chunk inside `fill`, so `cancel`
    /// needs a published handle to the instance that is executing right now to forward
    /// `cancelExecution` into its functions.
    std::mutex current_actions_mutex;
    ExpressionActionsPtr current_actions;
};

/// Adds defaults to columns using BlockDelayedDefaults bitmask attached to Block by child InputStream.
class AddingDefaultsTransform final : public ISimpleTransform
{
public:
    AddingDefaultsTransform(
        SharedHeader header,
        const ColumnsDescription & columns_,
        IInputFormat & input_format_,
        ContextPtr context_);

    String getName() const override { return "AddingDefaultsTransform"; }

protected:
    void onCancel() noexcept override;

    void transform(Chunk & chunk) override;

private:
    ColumnDefaultsFiller filler;
    IInputFormat & input_format;
};

}
