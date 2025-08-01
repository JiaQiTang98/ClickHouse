#pragma once

#include "config.h"

#if USE_AVRO

#include <Core/Types.h>
#include <Disks/ObjectStorages/IObjectStorage.h>
#include <Interpreters/Context_fwd.h>
#include <Storages/ObjectStorage/DataLakes/IDataLakeMetadata.h>
#include <Storages/ObjectStorage/DataLakes/Iceberg/IcebergMetadataFilesCache.h>
#include <Storages/ObjectStorage/StorageObjectStorage.h>

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <Storages/ObjectStorage/DataLakes/Iceberg/ManifestFile.h>
#include <Storages/ObjectStorage/DataLakes/Iceberg/SchemaProcessor.h>
#include <Storages/ObjectStorage/DataLakes/Iceberg/Snapshot.h>
#include <Formats/FormatParserGroup.h>

#include <Common/SharedMutex.h>
#include <tuple>
#include <optional>
#include <base/defines.h>


namespace DB
{

class IcebergMetadata : public IDataLakeMetadata
{
public:
    using IcebergHistory = std::vector<Iceberg::IcebergHistoryRecord>;

    static constexpr auto name = "Iceberg";

    IcebergMetadata(
        ObjectStoragePtr object_storage_,
        StorageObjectStorageConfigurationWeakPtr configuration_,
        const ContextPtr & context_,
        Int32 metadata_version_,
        Int32 format_version_,
        const Poco::JSON::Object::Ptr & metadata_object,
        IcebergMetadataFilesCachePtr cache_ptr);

    /// Get table schema parsed from metadata.
    NamesAndTypesList getTableSchema() const override;

    bool operator==(const IDataLakeMetadata & other) const override
    {
        const auto * iceberg_metadata = dynamic_cast<const IcebergMetadata *>(&other);
        return iceberg_metadata && getVersion() == iceberg_metadata->getVersion();
    }

    static void createInitial(
        const ObjectStoragePtr & object_storage,
        const StorageObjectStorageConfigurationWeakPtr & configuration,
        const ContextPtr & local_context,
        const std::optional<ColumnsDescription> & columns,
        ASTPtr partition_by,
        bool if_not_exists,
        std::shared_ptr<DataLake::ICatalog> catalog,
        const StorageID & table_id_);

    static DataLakeMetadataPtr create(
        const ObjectStoragePtr & object_storage,
        const StorageObjectStorageConfigurationWeakPtr & configuration,
        const ContextPtr & local_context);

    std::shared_ptr<NamesAndTypesList> getInitialSchemaByPath(ContextPtr local_context, const String & data_path) const override;
    std::shared_ptr<const ActionsDAG> getSchemaTransformer(ContextPtr local_context, const String & data_path) const override;

    bool supportsSchemaEvolution() const override { return true; }

    static Int32 parseTableSchema(const Poco::JSON::Object::Ptr & metadata_object, IcebergSchemaProcessor & schema_processor, LoggerPtr metadata_logger);

    bool supportsUpdate() const override { return true; }
    bool supportsWrites() const override { return true; }

    bool update(const ContextPtr & local_context) override;

    IcebergHistory getHistory(ContextPtr local_context) const;

    std::optional<size_t> totalRows(ContextPtr Local_context) const override;
    std::optional<size_t> totalBytes(ContextPtr Local_context) const override;

    ColumnMapperPtr getColumnMapper() const override { return column_mapper; }

protected:
    ObjectIterator iterate(
        const ActionsDAG * filter_dag,
        FileProgressCallback callback,
        size_t list_batch_size,
        ContextPtr local_context) const override;

private:
    const ObjectStoragePtr object_storage;
    const StorageObjectStorageConfigurationWeakPtr configuration;
    mutable IcebergSchemaProcessor schema_processor;
    LoggerPtr log;

    IcebergMetadataFilesCachePtr manifest_cache;

    std::tuple<Int64, Int32> getVersion() const;

    mutable SharedMutex mutex;

    Int32 last_metadata_version TSA_GUARDED_BY(mutex);
    const Int32 format_version;

    mutable std::unordered_map<String, Int32> schema_id_by_data_file TSA_GUARDED_BY(mutex);
    /// Does schema_id_by_data_files initialized and valid?
    /// Is an optimization to avoid acquiring exclusive lock from get*() method
    mutable std::atomic_bool schema_id_by_data_files_initialized = false;

    Int32 relevant_snapshot_schema_id TSA_GUARDED_BY(mutex);
    std::optional<Iceberg::IcebergSnapshot> relevant_snapshot TSA_GUARDED_BY(mutex);
    Int64 relevant_snapshot_id TSA_GUARDED_BY(mutex) {-1};
    const String table_location;

    mutable std::optional<Strings> cached_unprunned_files_for_last_processed_snapshot TSA_GUARDED_BY(cached_unprunned_files_for_last_processed_snapshot_mutex);
    mutable std::mutex cached_unprunned_files_for_last_processed_snapshot_mutex;

    ColumnMapperPtr column_mapper;

    void updateState(const ContextPtr & local_context, Poco::JSON::Object::Ptr metadata_object, bool metadata_file_changed) TSA_REQUIRES(mutex);
    Strings getDataFiles(const ActionsDAG * filter_dag, ContextPtr local_context) const;
    void updateSnapshot(ContextPtr local_context, Poco::JSON::Object::Ptr metadata_object) TSA_REQUIRES(mutex);
    ManifestFileCacheKeys getManifestList(ContextPtr local_context, const String & filename) const;
    void addTableSchemaById(Int32 schema_id, Poco::JSON::Object::Ptr metadata_object) TSA_REQUIRES(mutex);
    std::optional<Int32> getSchemaVersionByFileIfOutdated(String data_path) const TSA_REQUIRES_SHARED(mutex);
    void initializeSchemasFromManifestList(ContextPtr local_context, ManifestFileCacheKeys manifest_list_ptr) const TSA_REQUIRES(mutex);
    Iceberg::ManifestFilePtr getManifestFile(ContextPtr local_context, const String & filename, Int64 inherited_sequence_number) const TSA_REQUIRES_SHARED(mutex);
    std::optional<String> getRelevantManifestList(const Poco::JSON::Object::Ptr & metadata);
    Iceberg::ManifestFilePtr tryGetManifestFile(const String & filename) const;
};
}

#endif
