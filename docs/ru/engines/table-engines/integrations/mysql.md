---
slug: /ru/engines/table-engines/integrations/mysql
sidebar_position: 4
sidebar_label: MySQL
---

# MySQL {#mysql}

Движок MySQL позволяет выполнять запросы `SELECT` и `INSERT` над данными, хранящимися на удалённом MySQL сервере.

## Создание таблицы {#sozdanie-tablitsy}

``` sql
CREATE TABLE [IF NOT EXISTS] [db.]table_name [ON CLUSTER cluster]
(
    name1 [type1] [DEFAULT|MATERIALIZED|ALIAS expr1] [TTL expr1],
    name2 [type2] [DEFAULT|MATERIALIZED|ALIAS expr2] [TTL expr2],
    ...
) ENGINE = MySQL('host:port', 'database', 'table', 'user', 'password'[, replace_query, 'on_duplicate_clause'])
SETTINGS
    [ connection_pool_size=16, ]
    [ connection_max_tries=3, ]
    [ connection_wait_timeout=5, ]
    [ connection_auto_close=true, ]
    [ connect_timeout=10, ]
    [ read_write_timeout=300 ]    
;
```

Смотрите подробное описание запроса [CREATE TABLE](/sql-reference/statements/create/table).

Структура таблицы может отличаться от структуры исходной таблицы MySQL:

-   Имена столбцов должны быть такими же, как в исходной таблице MySQL, но можно использовать только некоторые из этих столбцов и в любом порядке.
-   Типы столбцов могут отличаться от типов в исходной таблице MySQL. ClickHouse пытается [привести](../../../engines/database-engines/mysql.md#data_types-support) значения к типам данных ClickHouse.
-   Настройка [external_table_functions_use_nulls](/operations/settings/settings#external_table_functions_use_nulls) определяет как обрабатывать Nullable столбцы. Значение по умолчанию: 1. Если значение 0, то табличная функция не делает Nullable столбцы, а вместо NULL выставляет значения по умолчанию для скалярного типа. Это также применимо для значений NULL внутри массивов.

**Параметры движка**

-   `host:port` — адрес сервера MySQL.

-   `database` — имя базы данных на удалённом сервере.

-   `table` — имя таблицы на удалённом сервере.

-   `user` — пользователь MySQL.

-   `password` — пароль пользователя.

-   `replace_query` — флаг, отвечающий за преобразование запросов `INSERT INTO` в `REPLACE INTO`. Если `replace_query=1`, то запрос заменяется.

-   `on_duplicate_clause` — выражение `ON DUPLICATE KEY on_duplicate_clause`, добавляемое к запросу `INSERT`.

        Пример: `INSERT INTO t (c1,c2) VALUES ('a', 2) ON DUPLICATE KEY UPDATE c2 = c2 + 1`, где `on_duplicate_clause` это `UPDATE c2 = c2 + 1`. Чтобы узнать какие `on_duplicate_clause` можно использовать с секцией `ON DUPLICATE KEY`  обратитесь к [документации MySQL](https://dev.mysql.com/doc/refman/8.0/en/insert-on-duplicate.html).

        Чтобы указать `on_duplicate_clause` необходимо передать `0` в параметр `replace_query`. Если одновременно передать `replace_query = 1` и `on_duplicate_clause`, то ClickHouse сгенерирует исключение.

Простые условия `WHERE` такие как `=, !=, >, >=, <, =` выполняются на стороне сервера MySQL.

Остальные условия и ограничение выборки `LIMIT` будут выполнены в ClickHouse только после выполнения запроса к MySQL.

Поддерживает несколько реплик, которые должны быть перечислены через `|`. Например:

```sql
CREATE TABLE test_replicas (id UInt32, name String, age UInt32, money UInt32) ENGINE = MySQL(`mysql{2|3|4}:3306`, 'clickhouse', 'test_replicas', 'root', 'clickhouse');
```

## Пример использования {#primer-ispolzovaniia}

Таблица в MySQL:

``` text
mysql> CREATE TABLE `test`.`test` (
    ->   `int_id` INT NOT NULL AUTO_INCREMENT,
    ->   `int_nullable` INT NULL DEFAULT NULL,
    ->   `float` FLOAT NOT NULL,
    ->   `float_nullable` FLOAT NULL DEFAULT NULL,
    ->   PRIMARY KEY (`int_id`));
Query OK, 0 rows affected (0,09 sec)

mysql> insert into test (`int_id`, `float`) VALUES (1,2);
Query OK, 1 row affected (0,00 sec)

mysql> select * from test;
+--------+--------------+-------+----------------+
| int_id | int_nullable | float | float_nullable |
+--------+--------------+-------+----------------+
|      1 |         NULL |     2 |           NULL |
+--------+--------------+-------+----------------+
1 row in set (0,00 sec)
```

Таблица в ClickHouse, которая получает данные из созданной ранее таблицы MySQL:

``` sql
CREATE TABLE mysql_table
(
    `float_nullable` Nullable(Float32),
    `int_id` Int32
)
ENGINE = MySQL('localhost:3306', 'test', 'test', 'bayonet', '123')
```

``` sql
SELECT * FROM mysql_table
```

``` text
┌─float_nullable─┬─int_id─┐
│           ᴺᵁᴸᴸ │      1 │
└────────────────┴────────┘
```

## Настройки {#mysql-settings}

Настройки по умолчанию не очень эффективны, так как они не используют повторное соединение. Эти настройки позволяют увеличить количество запросов, выполняемых сервером в секунду.

### connection_auto_close {#connection-auto-close}

Позволяет автоматически закрыть соединение после выполнения запроса, то есть отключить повторное использование соединения.

Возможные значения:

-   1 — автоматическое закрытие соединения разрешено (повторное использование отключается).
-   0 — автоматическое закрытие соединения запрещено (повторное использование включается).

Значение по умолчанию: `1`.

### connection_max_tries {#connection-max-tries}

Устанавливает количество повторных попыток для пула со сбоями соединения.

Возможные значения:

-   Положительное целое число.
-   0 — отсутствуют повторные попытки для пула со сбоями соединения.

Значение по умолчанию: `3`.

### connection_pool_size {#connection-pool-size}

Задает размер пула соединений (если используются все соединения, запрос будет ждать, пока какое-либо соединение не будет освобождено).

Возможные значения:

-   Положительное целое число.

Значение по умолчанию: `16`.

### connection_wait_timeout {#connection-wait-timeout}

Задает таймаут (в секундах) ожидания свободного подключения (в случае, если уже есть активные подключения connection_pool_size), 0 - не ждать.

Возможные значения:

-   Положительное целое число.

Значение по умолчанию: `5`.

### connect_timeout {#connect-timeout}

Задает таймаут ожидания подключения (в секундах).

Возможные значения:

-   Положительное целое число.

Значение по умолчанию: `10`.

### read_write_timeout {#read-write-timeout}

Задает таймаут ожидания ввода/вывода (в секундах).

Возможные значения:

-   Положительное целое число.

Значение по умолчанию: `300`.

## См. также {#see-also}

-   [Табличная функция mysql](../../../sql-reference/table-functions/mysql.md)
-   [Использование MySQL в качестве источника для внешнего словаря](../../../sql-reference/dictionaries/external-dictionaries/external-dicts-dict-sources.md#dicts-external_dicts_dict_sources-mysql)
