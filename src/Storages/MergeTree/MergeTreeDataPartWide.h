#pragma once

#include <Storages/MergeTree/IDataPartStorage.h>
#include <Storages/MergeTree/IMergeTreeDataPart.h>

namespace DB
{

/** In wide format data of each column is stored in one or several (for complex types) files.
  * Every data file is followed by marks file.
  * Can be used in tables with both adaptive and non-adaptive granularity.
  * This is the regular format of parts for MergeTree and suitable for big parts, as it's the most efficient.
  * Data part would be created in wide format if it's uncompressed size in bytes or number of rows would exceed
  * thresholds `min_bytes_for_wide_part` and `min_rows_for_wide_part`.
  */
class MergeTreeDataPartWide : public IMergeTreeDataPart
{
public:
    MergeTreeDataPartWide(
        const MergeTreeData & storage_,
        const String & name_,
        const MergeTreePartInfo & info_,
        const MutableDataPartStoragePtr & data_part_storage_,
        const IMergeTreeDataPart * parent_part_ = nullptr);

    bool isStoredOnReadonlyDisk() const override;

    bool isStoredOnRemoteDisk() const override;

    bool isStoredOnRemoteDiskWithZeroCopySupport() const override;

    std::optional<String> getFileNameForColumn(const NameAndTypePair & column) const override;

    ~MergeTreeDataPartWide() override;

    bool hasColumnFiles(const NameAndTypePair & column) const override;

    std::optional<time_t> getColumnModificationTime(const String & column_name) const override;

    void loadMarksToCache(const Names & column_names, MarkCache * mark_cache) const override;
    void removeMarksFromCache(MarkCache * mark_cache) const override;

protected:
    static void loadIndexGranularityImpl(
        MergeTreeIndexGranularityPtr & index_granularity_ptr,
        MergeTreeIndexGranularityInfo & index_granularity_info_,
        const IDataPartStorage & data_part_storage_,
        const std::string & any_column_file_name,
        const MergeTreeSettings & storage_settings);

    void doCheckConsistency(bool require_part_metadata) const override;

private:
    /// Loads marks index granularity into memory
    void loadIndexGranularity() override;

    ColumnSize getColumnSizeImpl(const NameAndTypePair & column, std::unordered_set<String> * processed_substreams) const;

    void calculateEachColumnSizes(ColumnSizeByName & each_columns_size, ColumnSize & total_size) const override;

};

}
