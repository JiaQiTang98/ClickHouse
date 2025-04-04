-- Tags: long, replica, no-replicated-database, no-shared-merge-tree
-- Tag no-replicated-database: Old syntax is not allowed
-- no-shared-merge-tree: implemented another test

DROP TABLE IF EXISTS replicated_alter1;
DROP TABLE IF EXISTS replicated_alter2;

SET replication_alter_partitions_sync = 2;

set allow_deprecated_syntax_for_merge_tree=1;
CREATE TABLE replicated_alter1 (d Date, k UInt64, i32 Int32) ENGINE=ReplicatedMergeTree('/clickhouse/tables/{database}/test_00062/alter', 'r1', d, k, 8192);
CREATE TABLE replicated_alter2 (d Date, k UInt64, i32 Int32) ENGINE=ReplicatedMergeTree('/clickhouse/tables/{database}/test_00062/alter', 'r2', d, k, 8192);

INSERT INTO replicated_alter1 VALUES ('2015-01-01', 10, 42);

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 ADD COLUMN dt DateTime('UTC');
INSERT INTO replicated_alter1 VALUES ('2015-01-01', 9, 41, '1992-01-01 08:00:00');

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 ADD COLUMN n Nested(ui8 UInt8, s String);
INSERT INTO replicated_alter1 VALUES ('2015-01-01', 8, 40, '2012-12-12 12:12:12', [1,2,3], ['12','13','14']);

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 ADD COLUMN `n.d` Array(Date);
INSERT INTO replicated_alter1 VALUES ('2015-01-01', 7, 39, '2014-07-14 13:26:50', [10,20,30], ['120','130','140'],['2000-01-01','2000-01-01','2000-01-03']);

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 ADD COLUMN s String DEFAULT '0';
INSERT INTO replicated_alter1 VALUES ('2015-01-01', 6,38,'2014-07-15 13:26:50',[10,20,30],['asd','qwe','qwe'],['2000-01-01','2000-01-01','2000-01-03'],'100500');

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 DROP COLUMN `n.d`, MODIFY COLUMN s Int64;

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 ADD COLUMN `n.d` Array(Date), MODIFY COLUMN s UInt32;

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 DROP COLUMN n.ui8, DROP COLUMN n.d;

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 DROP COLUMN n.s;

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 ADD COLUMN n.s Array(String), ADD COLUMN n.d Array(Date);

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 DROP COLUMN n;

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

ALTER TABLE replicated_alter1 MODIFY COLUMN dt Date, MODIFY COLUMN s DateTime('UTC') DEFAULT '1970-01-01 00:00:00';

DESC TABLE replicated_alter1;
SHOW CREATE TABLE replicated_alter1;
DESC TABLE replicated_alter2;
SHOW CREATE TABLE replicated_alter2;
SELECT * FROM replicated_alter1 ORDER BY k;

DROP TABLE replicated_alter1;
DROP TABLE replicated_alter2;
