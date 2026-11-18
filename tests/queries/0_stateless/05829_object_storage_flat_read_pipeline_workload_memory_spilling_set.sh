#!/usr/bin/env bash
# Tags: no-fasttest, no-parallel
# - no-fasttest: uses Iceberg
# - no-parallel: the singleton `MEMORY RESERVATION` resource is created and dropped, as in `05345_workload_memory_spilling_set`

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

set -e

# With `object_storage_flat_read_pipeline`, `ReadUnitFilterTransform` applies `PREWHERE` to the data files the format
# does not apply it for (ORC). It must keep the set of `IN (subquery)` spillable after the builder of the set has
# finished, as `FilterTransform` and the reader of `MergeTree` do: the memory pressure of the workload arrives while the
# data file is read, and the set is the only state which can spill.

workload=w_$CLICKHOUSE_TEST_UNIQUE_NAME
TABLE="t_${CLICKHOUSE_DATABASE}_${RANDOM}"
TABLE_PATH="${USER_FILES_PATH}/${TABLE}/"

function cleanup()
{
    $CLICKHOUSE_CLIENT -nm -q "DROP TABLE IF EXISTS ${TABLE}; DROP WORKLOAD IF EXISTS $workload; DROP RESOURCE IF EXISTS memory" >/dev/null 2>&1 || :
    rm -rf "${TABLE_PATH}"
}
trap cleanup EXIT
rm -rf "${TABLE_PATH}"

$CLICKHOUSE_CLIENT -nm -q "
CREATE OR REPLACE RESOURCE memory (MEMORY RESERVATION);
CREATE WORKLOAD $workload SETTINGS max_memory = '1Gi', max_memory_before_spill = '64Mi';
"

# The table supports `PREWHERE` because of its format, the data file is ORC. The file is read in chunks of 1024 rows, so
# that the memory pressure, which is seen between the tasks of the executor, arrives while the file is still read: only a
# processor which uses the set and is not finished can spill it. A table takes the format settings from its `SETTINGS`,
# not from the query.
$CLICKHOUSE_CLIENT -q "
    CREATE TABLE ${TABLE} (k Int64) ENGINE = IcebergLocal('${TABLE_PATH}', 'Parquet') SETTINGS input_format_orc_row_batch_size = 1024"
$CLICKHOUSE_CLIENT --allow_insert_into_iceberg 1 -q "
    INSERT INTO TABLE FUNCTION icebergLocal('${TABLE_PATH}', 'ORC', 'k Int64') SELECT number FROM numbers(30000)"

# Only workload pressure may request spills. Aggregation without keys cannot spill its state. The Iceberg pruning by
# the min/max values of the data files would build the set while the query is planned.
settings=(
    --workload "$workload"
    --object_storage_flat_read_pipeline 1
    --use_iceberg_partition_pruning 0
    --use_query_condition_cache 0
    --max_bytes_before_external_set 0
    --max_bytes_ratio_before_external_set 0
    --enable_adaptive_memory_spill_scheduler 0
    --enable_adaptive_aggregator 0
    --enable_parallel_replicas 0
    --max_rows_to_read 0
    --max_threads 4
    --max_block_size 1024
    --max_untracked_memory 0
    --min_bytes_to_spill 8Mi
    --log_profile_events 1
)

# The set of 1000000 keys is complete before the rows are read. 10000 of them are in the table, so the query
# accumulates 160 MiB of strings while the data file is read.
query="
    SELECT count(), sum(k), length(groupArray(repeat('x', 16384))) FROM ${TABLE}
    PREWHERE k IN (SELECT toInt64(if(number < 10000, number * 2, 10000000 + number)) FROM numbers(1000000))"

$CLICKHOUSE_CLIENT "${settings[@]}" --log_comment "$CLICKHOUSE_TEST_UNIQUE_NAME" -q "$query"

# `EXPLAIN ANALYZE` reports the spill under the step of the processor which spilled the set.
$CLICKHOUSE_CLIENT "${settings[@]}" -q "
WITH groupArray(explain) AS lines,
    arrayFilter(i -> lines[i] LIKE '%Spill: spilled %', arrayEnumerate(lines)) AS spills
SELECT arrayMap(i -> trimBoth(lines[i - 1]), spills), arrayAll(i -> match(lines[i], 'Spill: spilled [1-9]'), spills)
FROM (EXPLAIN ANALYZE pretty = 0, compact = 0, actions = 0, indexes = 0, description = 0 $query)
"

$CLICKHOUSE_CLIENT -q "SYSTEM FLUSH LOGS query_log"
$CLICKHOUSE_CLIENT -q "
SELECT count() = 1,
    max(ProfileEvents['MemoryReservationSpilledBytes']) > 0,
    max(ProfileEvents['SetsSpilledToDisk']) = 1,
    max(ProfileEvents['ExternalSetCompressedBytes']) > 0
FROM system.query_log
WHERE current_database = currentDatabase()
    AND event_date >= yesterday()
    AND log_comment = '$CLICKHOUSE_TEST_UNIQUE_NAME'
    AND type = 'QueryFinish'
    AND is_initial_query
"
