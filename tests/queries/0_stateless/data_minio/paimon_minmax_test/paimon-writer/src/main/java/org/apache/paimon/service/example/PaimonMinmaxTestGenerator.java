package org.apache.paimon.service.example;

import org.apache.paimon.catalog.Catalog;
import org.apache.paimon.catalog.CatalogContext;
import org.apache.paimon.catalog.CatalogFactory;
import org.apache.paimon.catalog.Identifier;
import org.apache.paimon.data.BinaryString;
import org.apache.paimon.data.GenericRow;
import org.apache.paimon.data.InternalRow;
import org.apache.paimon.disk.IOManagerImpl;
import org.apache.paimon.fs.Path;
import org.apache.paimon.schema.Schema;
import org.apache.paimon.table.Table;
import org.apache.paimon.table.sink.BatchTableCommit;
import org.apache.paimon.table.sink.BatchWriteBuilder;
import org.apache.paimon.table.sink.CommitMessage;
import org.apache.paimon.table.sink.TableWriteImpl;
import org.apache.paimon.types.DataTypes;

import java.util.List;

/**
 * Generates the paimon_minmax_test dataset.
 *
 * Table schema (no partition, no primary key → append table):
 *   id      INT NOT NULL
 *   int_val INT NOT NULL
 *   str_val STRING NOT NULL
 *
 * Data is written in 3 separate commits so that each commit produces its own
 * data file with a non-overlapping int_val range:
 *
 *   Batch 1: ids 1-3, int_val [10, 30],   str_val a/b/c
 *   Batch 2: ids 4-6, int_val [110, 130], str_val d/e/f
 *   Batch 3: ids 7-9, int_val [210, 230], str_val g/h/i
 *
 * The table option "metadata.stats-store" = "fields-*" ensures that Paimon
 * writes per-column min/max statistics (_VALUE_STATS_COLS) into the manifest,
 * enabling StarRocks to prune files via min/max filtering.
 */
public class PaimonMinmaxTestGenerator {

    private static final String DB_NAME    = "tests";
    private static final String TABLE_NAME = "paimon_minmax_test";

    public static void main(String[] args) throws Exception {
        // Default output path; override via first CLI arg if needed.
        String rootPath = args.length > 0 ? args[0] : "/tmp/warehouse";
        generate(rootPath);
    }

    public static void generate(String rootPath) throws Exception {
        // ── 1. Build schema ──────────────────────────────────────────────────
        Schema schema = Schema.newBuilder()
                .column("id",      DataTypes.INT().notNull())
                .column("int_val", DataTypes.INT().notNull())
                .column("str_val", DataTypes.STRING().notNull())
                // "fields-*" = dense stats: record min/max for every column.
                // This populates _VALUE_STATS_COLS in the manifest files so
                // that readers can skip data files whose [min, max] range does
                // not overlap with the query predicate.
                .option("metadata.stats-store", "fields-*")
                .build();

        // ── 2. Create catalog & table ────────────────────────────────────────
        Catalog catalog = createCatalog(rootPath);
        try {
            catalog.createDatabase(DB_NAME, /*ignoreIfExists=*/ true);
        } catch (Catalog.DatabaseAlreadyExistException ignored) {}

        Identifier tableId = Identifier.create(DB_NAME, TABLE_NAME);
        try {
            catalog.createTable(tableId, schema, /*ignoreIfExists=*/ false);
        } catch (Catalog.TableAlreadyExistException ignored) {
            System.out.println("Table already exists, reusing: " + tableId);
        }

        Table table = catalog.getTable(tableId);

        // ── 3. Write 3 independent batches ───────────────────────────────────
        // Each batch uses its own BatchWriteBuilder → newWrite → prepareCommit
        // → commit cycle, which guarantees a separate data file per batch.

        // Batch 1: int_val range [10, 30]
        writeBatch(table, rootPath,
                new int[]    {  1,   2,   3 },
                new int[]    { 10,  20,  30 },
                new String[] { "a", "b", "c" });

        // Batch 2: int_val range [110, 130]
        writeBatch(table, rootPath,
                new int[]    {   4,    5,    6 },
                new int[]    { 110,  120,  130 },
                new String[] { "d", "e", "f" });

        // Batch 3: int_val range [210, 230]
        writeBatch(table, rootPath,
                new int[]    {   7,    8,    9 },
                new int[]    { 210,  220,  230 },
                new String[] { "g", "h", "i" });

        System.out.println("Done. Paimon table written to: " + rootPath
                + "/" + DB_NAME + "/" + TABLE_NAME);
    }

    /**
     * Writes one batch of rows and commits them as a single snapshot.
     * Each call produces exactly one new data file.
     */
    private static void writeBatch(
            Table table, String ioTmpPath,
            int[] ids, int[] intVals, String[] strVals) throws Exception {

        BatchWriteBuilder writeBuilder = table.newBatchWriteBuilder();
        TableWriteImpl writer = (TableWriteImpl) writeBuilder.newWrite()
                .withIOManager(new IOManagerImpl(ioTmpPath));
        try {
            for (int i = 0; i < ids.length; i++) {
                // GenericRow field order must match schema: id, int_val, str_val
                GenericRow row = new GenericRow(3);
                row.setField(0, ids[i]);
                row.setField(1, intVals[i]);
                row.setField(2, BinaryString.fromString(strVals[i]));
                writer.write(row);
            }
            List<CommitMessage> messages = writer.prepareCommit();

            BatchTableCommit commit = writeBuilder.newCommit();
            try {
                commit.commit(messages);
                System.out.printf("  committed batch: id=[%d..%d], int_val=[%d..%d]%n",
                        ids[0], ids[ids.length - 1],
                        intVals[0], intVals[intVals.length - 1]);
            } finally {
                commit.close();
            }
        } finally {
            writer.close();
        }
    }

    private static Catalog createCatalog(String rootPath) {
        CatalogContext context = CatalogContext.create(new Path(rootPath));
        return CatalogFactory.createCatalog(context);
    }
}
