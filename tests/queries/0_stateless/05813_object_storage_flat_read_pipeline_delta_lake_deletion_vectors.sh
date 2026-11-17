#!/usr/bin/env bash
# Tags: no-fasttest, no-msan
# Tag no-fasttest: delta-kernel-rs is not in fast test
# Tag no-msan: delta-kernel-rs is not built with MSan

# The deletion vectors of DeltaLake with `object_storage_flat_read_pipeline`: DeltaLake adds the processor which
# applies them itself, the result is the same as with the nested read pipeline, and a read of other files has no
# such processor.

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

TABLE="$CUR_DIR/data_lake_corpus/delta/deletion_vectors"

echo "-- version 3: 100 rows written, then two deletes"
for flat in 0 1; do
    ${CLICKHOUSE_LOCAL} --object_storage_flat_read_pipeline=${flat} --query "
        SELECT count(), sum(cityHash64(tuple(*)))
        FROM deltaLakeLocal('${TABLE}') SETTINGS delta_lake_snapshot_version = 3"
done | uniq -c | sed 's/^ *//'

echo "-- latest version"
for flat in 0 1; do
    ${CLICKHOUSE_LOCAL} --object_storage_flat_read_pipeline=${flat} --query "
        SELECT count(), sum(cityHash64(tuple(*))) FROM deltaLakeLocal('${TABLE}')"
done | uniq -c | sed 's/^ *//'

echo "-- pipeline"
${CLICKHOUSE_LOCAL} --object_storage_flat_read_pipeline=1 --query "
    SELECT DISTINCT trimLeft(replaceRegexpOne(explain, ' ×.*', ''))
    FROM (EXPLAIN PIPELINE SELECT * FROM deltaLakeLocal('${TABLE}'))
    WHERE explain LIKE '%DeletionVector%'"

echo "-- pipeline of a read of plain object storage files: flat, without a deletion vector processor"
FILE="${CLICKHOUSE_TEST_UNIQUE_NAME}/data.parquet"
${CLICKHOUSE_CLIENT} --query "
    INSERT INTO FUNCTION s3(s3_conn, filename = '${FILE}', format = Parquet) SELECT number AS a FROM numbers(10)
    SETTINGS s3_truncate_on_insert = 1"
${CLICKHOUSE_CLIENT} --object_storage_flat_read_pipeline=1 --query "
    SELECT countIf(explain LIKE '%ReadUnitColumnsTransform%') > 0, countIf(explain LIKE '%DeletionVector%')
    FROM (EXPLAIN PIPELINE SELECT * FROM s3(s3_conn, filename = '${FILE}', format = Parquet))"
