#!/usr/bin/env bash
# Tags: no-fasttest, no-parallel-replicas
# - no-fasttest: uses Iceberg
# - no-parallel-replicas: see the comment in `04071_iceberg_orc_prewhere_crash.sh`

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

# `object_storage_flat_read_pipeline` must return the same results as the nested pipeline per file
# for an Iceberg table with position deletes, a schema change, data files in Parquet and ORC
# (ORC does not support PREWHERE, so the filters are applied after the format), a row policy
# and PREWHERE with an expression kept for the rest of the query.

TABLE="t_${CLICKHOUSE_DATABASE}_${RANDOM}"
TABLE_PATH="${USER_FILES_PATH}/${TABLE}/"
rm -rf "${TABLE_PATH}"

${CLICKHOUSE_CLIENT} --query "CREATE TABLE ${TABLE} (k Int64, a Int64, s String) ENGINE = IcebergLocal('${TABLE_PATH}', 'Parquet')"
${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "INSERT INTO ${TABLE} SELECT number, number % 3, toString(number) FROM numbers(20)"
${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "ALTER TABLE ${TABLE} ADD COLUMN n Nullable(Int64)"
${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "INSERT INTO ${TABLE} SELECT number + 20, number % 3, toString(number + 20), number FROM numbers(20)"
# The position deletes of both data files are in one delete file.
${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "ALTER TABLE ${TABLE} DELETE WHERE k % 7 = 0"
# DELETE requires all data files to be Parquet, so the ORC file is added after it.
${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "
    INSERT INTO TABLE FUNCTION icebergLocal('${TABLE_PATH}', 'ORC', 'k Int64, a Int64, s String, n Nullable(Int64)')
    SELECT number + 40, number % 3, toString(number + 40), NULL FROM numbers(20)"

QUERIES=(
    "SELECT count(), sum(k), groupArray(k) FROM (SELECT k FROM ${TABLE} ORDER BY k)"
    "SELECT groupArray((k, a, s, n)) FROM (SELECT * FROM ${TABLE} ORDER BY k)"
    "SELECT groupArray((k, n)) FROM (SELECT k, n FROM ${TABLE} PREWHERE a = 1 ORDER BY k)"
    "SELECT groupArray((k, x)) FROM (SELECT k, a + 1 AS x FROM ${TABLE} PREWHERE a + 1 > 2 WHERE x > 1 ORDER BY k)"
    "SELECT groupArray((k, s)) FROM (SELECT k, s FROM ${TABLE} PREWHERE n > 5 ORDER BY k)"
    "SELECT groupArray((k, _file LIKE '%.orc', _row_number)) FROM (SELECT k, _file, _row_number FROM ${TABLE} WHERE a = 2 ORDER BY k)"
)

function check()
{
    for query in "${QUERIES[@]}"; do
        for roaring in 0 1; do
            local settings="--optimize_move_to_prewhere=1 --max_block_size=3 --use_roaring_bitmap_iceberg_positional_deletes=${roaring}"
            local nested flat
            nested=$(${CLICKHOUSE_CLIENT} ${settings} --object_storage_flat_read_pipeline=0 --query "${query}" 2>&1)
            flat=$(${CLICKHOUSE_CLIENT} ${settings} --object_storage_flat_read_pipeline=1 --query "${query}" 2>&1)
            if [ "${nested}" != "${flat}" ]; then
                echo "Different results with use_roaring_bitmap_iceberg_positional_deletes=${roaring} for: ${query}"
                echo "nested: ${nested}"
                echo "flat: ${flat}"
            fi
        done
        echo "${flat}"
    done
}

echo "-- without row policy"
check

echo "-- row policy USING a > 0"
${CLICKHOUSE_CLIENT} --query "CREATE ROW POLICY policy_${TABLE} ON ${TABLE} USING a > 0 TO ALL"
check
${CLICKHOUSE_CLIENT} --query "DROP ROW POLICY policy_${TABLE} ON ${TABLE}"

echo "-- pipeline"
${CLICKHOUSE_CLIENT} --object_storage_flat_read_pipeline=1 --query "
    SELECT DISTINCT trimLeft(replaceRegexpOne(explain, ' ×.*', ''))
    FROM (EXPLAIN PIPELINE SELECT k FROM ${TABLE} PREWHERE a = 1)
    WHERE explain LIKE '%ReadUnit%' OR explain LIKE '%ExtractColumnsTransform%'"
${CLICKHOUSE_CLIENT} --object_storage_flat_read_pipeline=0 --query "
    SELECT count() FROM (EXPLAIN PIPELINE SELECT k FROM ${TABLE} PREWHERE a = 1) WHERE explain LIKE '%ReadUnit%'"

echo "-- exception in a filter applied after the format (ORC)"
for flat in 0 1; do
    ${CLICKHOUSE_CLIENT} --optimize_move_to_prewhere=1 --object_storage_flat_read_pipeline=${flat} --query "
        SELECT count() FROM ${TABLE} PREWHERE throwIf(k = 45) = 0" 2>&1 \
        | grep -oE "FUNCTION_THROW_IF_VALUE_IS_NON_ZERO|QUERY_WAS_CANCELLED" | head -1
done

echo "-- LIMIT stops the read"
for flat in 0 1; do
    ${CLICKHOUSE_CLIENT} --max_block_size=1 --max_threads=1 --object_storage_flat_read_pipeline=${flat} --query "
        SELECT count() FROM (SELECT k FROM ${TABLE} LIMIT 1)"
done

echo "-- processors_profile_log"
QUERY_ID="${CLICKHOUSE_DATABASE}_${RANDOM}_flat"
${CLICKHOUSE_CLIENT} --query_id "${QUERY_ID}" --log_processors_profiles=1 --object_storage_flat_read_pipeline=1 --query "
    SELECT k FROM ${TABLE} PREWHERE a = 1 FORMAT Null"
${CLICKHOUSE_CLIENT} --query "SYSTEM FLUSH LOGS processors_profile_log"
${CLICKHOUSE_CLIENT} --query "
    SELECT DISTINCT name FROM system.processors_profile_log
    WHERE event_date >= yesterday() AND query_id = '${QUERY_ID}' AND name LIKE 'ReadUnit%'
    ORDER BY name"

${CLICKHOUSE_CLIENT} --query "DROP TABLE ${TABLE}"
rm -rf "${TABLE_PATH}"
