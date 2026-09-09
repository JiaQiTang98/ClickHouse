#!/usr/bin/env bash
# Tags: no-fasttest
# - no-fasttest: requires `IcebergLocal` (USE_AVRO build option)

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

# `read_unit_kind` declares how one unit of work in the read path is produced. `format_file` is the
# default every table engine uses: one data file opened with `FormatFactory`. `self_opening` is for
# an engine whose read unit produces its rows itself. No engine implements `self_opening` yet, so
# what is checked here is that the abstraction exists, that the default is unchanged down to the
# pipeline, and that each assumption `self_opening` breaks is refused at plan time with its own
# error rather than answered wrongly.
#
# Every read below uses `sum(k)` rather than `count()`: Iceberg answers `count()` from its metadata
# (`totalRows`) without building a read pipeline at all, so a `count()` would not reach the code
# under test.

TABLE="t_${CLICKHOUSE_DATABASE}_read_unit_kind"
TABLE_PATH="${USER_FILES_PATH}/${TABLE}/"
# A `CREATE` naming a path that already holds an Iceberg table is rejected, so the table carrying
# the setting needs its own path.
SELF_TABLE="${TABLE}_self"
SELF_TABLE_PATH="${USER_FILES_PATH}/${SELF_TABLE}/"

trap 'rm -rf "${TABLE_PATH}" "${SELF_TABLE_PATH}" 2>/dev/null' EXIT

${CLICKHOUSE_CLIENT} --query "
    CREATE TABLE ${TABLE} (k UInt64, s String)
    ENGINE = IcebergLocal('${TABLE_PATH}', 'Parquet')
"
${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "
    INSERT INTO ${TABLE} SELECT number AS k, concat('v_', toString(number)) AS s FROM numbers(10)
"

echo '-- the default kind reads the data files with FormatFactory'
${CLICKHOUSE_CLIENT} --query "SELECT count(), sum(k) FROM ${TABLE}"

# The assertions below check for the presence of a message, not the number of lines carrying it:
# with `--send_logs_level=warning` the server streams its own `executeQuery` error line to the
# client on top of the client's exception print, so one error yields several matching lines (and a
# distributed query yields one more per node).
expect_message()
{
    grep -F -q -- "$1" && echo 1 || echo 0
}

echo '-- an unknown value is rejected by the settings framework'
${CLICKHOUSE_CLIENT} --query "
    SELECT sum(k) FROM icebergLocal('${TABLE_PATH}', SETTINGS read_unit_kind = 'no_such_kind')
" 2>&1 | expect_message 'BAD_ARGUMENTS'

echo '-- self_opening is refused at plan time, naming the engine and the requested kind'
${CLICKHOUSE_CLIENT} --query "
    SELECT sum(k) FROM icebergLocal('${TABLE_PATH}', SETTINGS read_unit_kind = 'self_opening')
" 2>&1 | expect_message 'Table engine IcebergLocal does not implement the `self_opening` read unit kind'

# `createFileIterator` rebuilds an `ObjectInfo` from a path string when the read is distributed, so
# a unit that is not identified by a path cannot cross the wire. This gate is checked before the
# engine is asked whether it can serve the kind, so that the error names the actual reason instead
# of the generic "not implemented".
echo '-- distributed processing is refused with its own error'
${CLICKHOUSE_CLIENT} --query "
    SELECT sum(k) FROM icebergLocalCluster('test_cluster_two_shards_localhost', '${TABLE_PATH}', SETTINGS read_unit_kind = 'self_opening')
" 2>&1 | expect_message 'Distributed processing is not implemented for the `self_opening` read unit kind'

echo '-- the setting also applies when stored in the table definition'
${CLICKHOUSE_CLIENT} --query "
    CREATE TABLE ${SELF_TABLE} (k UInt64, s String)
    ENGINE = IcebergLocal('${SELF_TABLE_PATH}', 'Parquet')
    SETTINGS read_unit_kind = 'self_opening'
"
# Writing does not go through the read path, so it must keep working.
${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "
    INSERT INTO ${SELF_TABLE} SELECT number AS k, concat('v_', toString(number)) AS s FROM numbers(5)
"
${CLICKHOUSE_CLIENT} --query "SELECT sum(k) FROM ${SELF_TABLE}" 2>&1 | expect_message 'NOT_IMPLEMENTED'
${CLICKHOUSE_CLIENT} --query "SHOW CREATE TABLE ${SELF_TABLE}" | expect_message "read_unit_kind = \\'self_opening\\'"
${CLICKHOUSE_CLIENT} --query "DROP TABLE ${SELF_TABLE}"

# Writing the default value explicitly must change nothing at all: not the rows, not the pipeline.
echo '-- explicit format_file is identical to omitting the setting'
diff <(${CLICKHOUSE_CLIENT} --query "SELECT * FROM icebergLocal('${TABLE_PATH}') ORDER BY k") \
     <(${CLICKHOUSE_CLIENT} --query "SELECT * FROM icebergLocal('${TABLE_PATH}', SETTINGS read_unit_kind = 'format_file') ORDER BY k") \
     && echo 'rows identical'
diff <(${CLICKHOUSE_CLIENT} --query "EXPLAIN PIPELINE SELECT * FROM icebergLocal('${TABLE_PATH}') ORDER BY k") \
     <(${CLICKHOUSE_CLIENT} --query "EXPLAIN PIPELINE SELECT * FROM icebergLocal('${TABLE_PATH}', SETTINGS read_unit_kind = 'format_file') ORDER BY k") \
     && echo 'pipeline identical'

${CLICKHOUSE_CLIENT} --query "DROP TABLE ${TABLE}"
