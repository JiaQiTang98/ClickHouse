#!/usr/bin/env bash
# Tags: no-fasttest, no-parallel-replicas
# - no-fasttest: uses Iceberg
# - no-parallel-replicas: see the comment in `04071_iceberg_orc_prewhere_crash.sh`

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

# A filter which removes all rows, such as `PREWHERE` with `IN` an empty subquery or `ignore`, must stop the read
# before the rows of the data files are read. ORC does not support `PREWHERE`, so the filters are applied after the
# format: by `FilterTransform` in the pipeline of each data file in the nested read pipeline, and by
# `ReadUnitFilterTransform` in the main pipeline with `object_storage_flat_read_pipeline`. The flat read pipeline
# must not read more than the nested one. It stops before any data file is opened, also before the Parquet file,
# for which the format applies the filters.

TABLE="t_${CLICKHOUSE_DATABASE}_${RANDOM}"
TABLE_PATH="${USER_FILES_PATH}/${TABLE}/"
rm -rf "${TABLE_PATH}"

# The table supports `PREWHERE` because of its format. Three data files are ORC, one is Parquet.
${CLICKHOUSE_CLIENT} --query "CREATE TABLE ${TABLE} (a Int64, s String) ENGINE = IcebergLocal('${TABLE_PATH}', 'Parquet')"
for i in 0 1 2; do
    ${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "
        INSERT INTO TABLE FUNCTION icebergLocal('${TABLE_PATH}', 'ORC', 'a Int64, s String')
        SELECT number + ${i} * 1000, toString(number) FROM numbers(1000)"
done
${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "INSERT INTO ${TABLE} SELECT number + 3000, toString(number) FROM numbers(1000)"
${CLICKHOUSE_CLIENT} --query "CREATE TABLE ${TABLE}_empty (a Int64) ENGINE = Memory"

QUERIES=(
    "SELECT count() FROM ${TABLE} PREWHERE a IN (SELECT a FROM ${TABLE}_empty)"
    "SELECT s FROM ${TABLE} PREWHERE ignore(a)"
)

# The Iceberg pruning by the min/max values of the data files would skip all data files for the empty set, and the
# query condition cache would skip them in the second run. The filter is not pushed down to the ORC reader, so a read
# which is not stopped reads all rows.
SETTINGS="--use_iceberg_partition_pruning=0 --use_query_condition_cache=0 --input_format_orc_filter_push_down=0"

for i in "${!QUERIES[@]}"; do
    for flat in 0 1; do
        ${CLICKHOUSE_CLIENT} ${SETTINGS} --object_storage_flat_read_pipeline=${flat} --log_comment="${TABLE}_${i}_${flat}" --query "${QUERIES[$i]}"
    done
done

${CLICKHOUSE_CLIENT} --query "SYSTEM FLUSH LOGS query_log"

# `read_rows` is counted before the filters in the flat read pipeline, after them in the nested one.
${CLICKHOUSE_CLIENT} --query "
    SELECT
        splitByChar('_', log_comment)[-2] AS query,
        if(splitByChar('_', log_comment)[-1] = '1', 'flat', 'nested') AS pipeline,
        ProfileEvents['EngineFileLikeReadFiles'] AS data_files_read,
        read_rows
    FROM system.query_log
    WHERE current_database = currentDatabase() AND type = 'QueryFinish' AND startsWith(log_comment, '${TABLE}_')
    ORDER BY query, pipeline DESC"

${CLICKHOUSE_CLIENT} --query "DROP TABLE ${TABLE}"
${CLICKHOUSE_CLIENT} --query "DROP TABLE ${TABLE}_empty"
rm -rf "${TABLE_PATH}"
