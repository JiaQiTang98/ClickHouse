#!/usr/bin/env bash
# Tags: no-fasttest,no-parallel-replicas
# no-parallel-replicas: the ProfileEvents with the expected values are reported on the replicas the query runs in,
# and the coordinator does not collect all ProfileEvents values.

CURDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CURDIR"/../shell_config.sh

DATA_PATH="${CURDIR}/data_minio/paimon_minmax_test"

# test 1: full scan baseline (no pruning)
$CLICKHOUSE_CLIENT --query_id="test_03784_1_$CLICKHOUSE_TEST_UNIQUE_NAME" --query "
SELECT id, int_val, str_val
FROM paimonLocal('${DATA_PATH}')
ORDER BY id
"

# test 2: range query on value column, no minmax pruning
$CLICKHOUSE_CLIENT --query_id="test_03784_2_$CLICKHOUSE_TEST_UNIQUE_NAME" --query "
SELECT id, int_val, str_val
FROM paimonLocal('${DATA_PATH}')
WHERE int_val > 100
ORDER BY id
"

# test 3: same range query WITH minmax pruning — must return identical rows to test 2
$CLICKHOUSE_CLIENT --query_id="test_03784_3_$CLICKHOUSE_TEST_UNIQUE_NAME" --query "
SELECT id, int_val, str_val
FROM paimonLocal('${DATA_PATH}')
WHERE int_val > 100
ORDER BY id
SETTINGS use_paimon_minmax_index_pruning=1
"

# test 4: more selective range query, no minmax pruning
$CLICKHOUSE_CLIENT --query_id="test_03784_4_$CLICKHOUSE_TEST_UNIQUE_NAME" --query "
SELECT id, int_val, str_val
FROM paimonLocal('${DATA_PATH}')
WHERE int_val > 200
ORDER BY id
"

# test 5: same selective range query WITH minmax pruning — must return identical rows to test 4
$CLICKHOUSE_CLIENT --query_id="test_03784_5_$CLICKHOUSE_TEST_UNIQUE_NAME" --query "
SELECT id, int_val, str_val
FROM paimonLocal('${DATA_PATH}')
WHERE int_val > 200
ORDER BY id
SETTINGS use_paimon_minmax_index_pruning=1
"

# test 6: equality predicate, no pruning
$CLICKHOUSE_CLIENT --query_id="test_03784_6_$CLICKHOUSE_TEST_UNIQUE_NAME" --query "
SELECT id, int_val, str_val
FROM paimonLocal('${DATA_PATH}')
WHERE int_val = 120
ORDER BY id
"

# test 7: same equality predicate WITH minmax pruning — must return identical rows to test 6
$CLICKHOUSE_CLIENT --query_id="test_03784_7_$CLICKHOUSE_TEST_UNIQUE_NAME" --query "
SELECT id, int_val, str_val
FROM paimonLocal('${DATA_PATH}')
WHERE int_val = 120
ORDER BY id
SETTINGS use_paimon_minmax_index_pruning=1
"

$CLICKHOUSE_CLIENT --query "
    SYSTEM FLUSH LOGS query_log;
"

# Verify file counts: pruning queries (3,5,7) should read fewer files than their baselines (2,4,6)
$CLICKHOUSE_CLIENT --query "
    SELECT sum(ProfileEvents['EngineFileLikeReadFiles']) FROM system.query_log
    WHERE initial_query_id like '%test_03784%' and initial_query_id like '%$CLICKHOUSE_TEST_UNIQUE_NAME%' AND
    current_database = currentDatabase() and type='QueryFinish' group by initial_query_id order by initial_query_id;"
