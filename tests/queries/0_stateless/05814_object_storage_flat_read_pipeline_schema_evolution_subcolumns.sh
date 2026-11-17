#!/usr/bin/env bash
# Tags: no-fasttest
# no-fasttest: the fast test build has no Iceberg and Parquet support.

# With `object_storage_flat_read_pipeline`, a tuple element requested as a column of its own is extracted from its
# tuple for a data file which is read under its initial schema (Iceberg schema evolution), whose columns are whole
# after the schema transform. The values are compared with the ones the writer read back.

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

TABLE="$CUR_DIR/data_lake_corpus/iceberg/v2_schema_evolution"
EXPECTED="$CUR_DIR/data_lake_corpus/expected/79d1f28d4a3901a7.jsonl.zst"

${CLICKHOUSE_LOCAL} --object_storage_flat_read_pipeline=1 --query "
    CREATE TABLE e ENGINE = Memory AS SELECT * FROM icebergLocal('${TABLE}') WHERE 0;
    INSERT INTO e SELECT * FROM file('${EXPECTED}', JSONEachRow);
    SELECT
        (SELECT count(), sum(cityHash64(st.x)) FROM icebergLocal('${TABLE}')) = (SELECT count(), sum(cityHash64(st.x)) FROM e),
        (SELECT count(), sum(cityHash64(st.y)) FROM icebergLocal('${TABLE}')) = (SELECT count(), sum(cityHash64(st.y)) FROM e),
        (SELECT groupArraySorted(100)(st.x) FROM icebergLocal('${TABLE}') WHERE st.x > 0)
            = (SELECT groupArraySorted(100)(st.x) FROM e WHERE st.x > 0);"
