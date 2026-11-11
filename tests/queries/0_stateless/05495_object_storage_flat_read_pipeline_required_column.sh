#!/usr/bin/env bash
# Tags: no-fasttest, no-parallel-replicas
# - no-fasttest: uses Iceberg
# - no-parallel-replicas: see the comment in `04071_iceberg_orc_prewhere_crash.sh`

CUR_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=../shell_config.sh
. "$CUR_DIR"/../shell_config.sh

# `object_storage_flat_read_pipeline` converts the columns which only the filters read to the types of the table
# schema, so a data file with NULLs in a column which external metadata made required throws on `PREWHERE` over it,
# as reading the column does. The nested pipeline passes such a column through, see
# `04737_iceberg_prewhere_modify_column_nullable`.

TABLE="t_${CLICKHOUSE_DATABASE}_${RANDOM}"
TABLE_PATH="${USER_FILES_PATH}/${TABLE}/"
rm -rf "${TABLE_PATH}"

${CLICKHOUSE_CLIENT} --query "CREATE TABLE ${TABLE} (id Nullable(Int64), s String) ENGINE = IcebergLocal('${TABLE_PATH}', 'Parquet')"
${CLICKHOUSE_CLIENT} --allow_insert_into_iceberg=1 --query "INSERT INTO ${TABLE} SELECT * FROM values('id Nullable(Int64), s String', (1, 'one'), (NULL, 'none'), (3, 'three'))"

# Appends a schema in which `id` is required, as another engine could write it.
LATEST=$(ls "${TABLE_PATH}metadata/" | grep -E '^v[0-9]+\.metadata\.json$' | sort -t v -k2 -n | tail -1)
META=$(python3 - "${TABLE_PATH}metadata" "${LATEST}" <<'PYEOF'
import copy, json, os, re, sys

metadata_dir, latest_file = sys.argv[1], sys.argv[2]

with open(os.path.join(metadata_dir, latest_file)) as fh:
    metadata = json.load(fh)

current = next(s for s in metadata["schemas"] if s["schema-id"] == metadata["current-schema-id"])
tightened = copy.deepcopy(current)
tightened["schema-id"] = max(s["schema-id"] for s in metadata["schemas"]) + 1
for field in tightened["fields"]:
    if field["name"] == "id":
        field["required"] = True

metadata["schemas"].append(tightened)
metadata["current-schema-id"] = tightened["schema-id"]
metadata["last-updated-ms"] = metadata.get("last-updated-ms", 0) + 60000

version = int(re.match(r"v(\d+)\.metadata\.json", latest_file).group(1)) + 1
tmp_file = os.path.join(metadata_dir, ".tmp_next")
with open(tmp_file, "w") as fh:
    json.dump(metadata, fh)
os.rename(tmp_file, os.path.join(metadata_dir, f"v{version}.metadata.json"))
print(f"metadata/v{version}.metadata.json")
PYEOF
)

TF="icebergLocal('${TABLE_PATH}', 'Parquet', SETTINGS iceberg_metadata_file_path = '${META}')"
SETTINGS="--optimize_move_to_prewhere=1 --object_storage_flat_read_pipeline=1"

${CLICKHOUSE_CLIENT} --query "DESCRIBE ${TF}" | cut -f1,2
${CLICKHOUSE_CLIENT} ${SETTINGS} --query "SELECT s FROM ${TF} ORDER BY s"
${CLICKHOUSE_CLIENT} ${SETTINGS} --query "SELECT s FROM ${TF} PREWHERE id > 1 ORDER BY s" 2>&1 \
    | grep -oF "CANNOT_INSERT_NULL_IN_ORDINARY_COLUMN" | head -1
${CLICKHOUSE_CLIENT} ${SETTINGS} --query "SELECT id FROM ${TF} ORDER BY s" 2>&1 \
    | grep -oF "CANNOT_INSERT_NULL_IN_ORDINARY_COLUMN" | head -1

${CLICKHOUSE_CLIENT} --query "DROP TABLE ${TABLE}"
rm -rf "${TABLE_PATH}"
