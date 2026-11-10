-- Tags: no-fasttest, no-parallel-replicas
-- - no-fasttest: reads Iceberg tables from Minio
-- - no-parallel-replicas: the cluster read does not use the setting on the initiator

-- `object_storage_flat_read_pipeline` must return the same results as the nested pipeline per file
-- for an Iceberg table with equality and position deletes written by Spark (see `deletes_db/README.md`).

SET optimize_move_to_prewhere = 1;

SELECT 'all rows';
SELECT count(), sum(id), uniqExact(name) FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_table') SETTINGS object_storage_flat_read_pipeline = 0;
SELECT count(), sum(id), uniqExact(name) FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_table') SETTINGS object_storage_flat_read_pipeline = 1;

SELECT 'PREWHERE on a column of an equality delete';
SELECT groupArray((id, name)) FROM (SELECT id, name FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_table') PREWHERE id < 40 ORDER BY id, name) SETTINGS object_storage_flat_read_pipeline = 0;
SELECT groupArray((id, name)) FROM (SELECT id, name FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_table') PREWHERE id < 40 ORDER BY id, name) SETTINGS object_storage_flat_read_pipeline = 1;

SELECT 'a column which is used only by the equality deletes is not selected';
SELECT count(), sum(id) FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_table') WHERE id % 5 = 0 SETTINGS object_storage_flat_read_pipeline = 0;
SELECT count(), sum(id) FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_table') WHERE id % 5 = 0 SETTINGS object_storage_flat_read_pipeline = 1;

SELECT 'virtual columns';
SELECT count(), uniqExact(_file, _row_number), countIf(_row_number IS NULL), countIf(_size = 0) FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_table') SETTINGS object_storage_flat_read_pipeline = 0;
SELECT count(), uniqExact(_file, _row_number), countIf(_row_number IS NULL), countIf(_size = 0) FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_table') SETTINGS object_storage_flat_read_pipeline = 1;

SELECT 'a required column with an optional equality delete column';
SELECT groupArray(data) FROM (SELECT data FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_required_table') ORDER BY data) SETTINGS object_storage_flat_read_pipeline = 0;
SELECT groupArray(data) FROM (SELECT data FROM icebergS3(s3_conn, filename = 'deletes_db/eq_deletes_required_table') ORDER BY data) SETTINGS object_storage_flat_read_pipeline = 1;
