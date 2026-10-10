/*
  +--------------------------------------------------------------------------+
  | libcat                                                                   |
  +--------------------------------------------------------------------------+
  | Licensed under the Apache License, Version 2.0 (the "License");          |
  | you may not use this file except in compliance with the License.         |
  | You may obtain a copy of the License at                                  |
  | http://www.apache.org/licenses/LICENSE-2.0                               |
  | Unless required by applicable law or agreed to in writing, software      |
  | distributed under the License is distributed on an "AS IS" BASIS,        |
  | WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. |
  | See the License for the specific language governing permissions and      |
  | limitations under the License. See accompanying LICENSE file.            |
  +--------------------------------------------------------------------------+
  | Author: Codinghuang <codinghuang@qq.com>                                 |
  +--------------------------------------------------------------------------+
 */

#include "test.h"
#ifdef CAT_PQ

static cat_always_inline cat_bool_t skip()
{
    return (! cat_env_is_true("TEST_CAT_POSTGRESQL", cat_false));
}

static void cat_pq_query(int *row_count, int *column_count)
{
    int stmt_counter = 0;

    PGconn *conn = cat_pq_connectdb(TEST_PQ_CONNINFO);
    ASSERT_EQ(PQstatus(conn), CONNECTION_OK);

    std::string stmt_name = string_format("pdo_stmt_%08x", ++stmt_counter);
    PGresult *result = cat_pq_prepare(conn, stmt_name.c_str(), "SELECT * FROM pg_catalog.pg_tables limit 1", 0, nullptr);
    ASSERT_EQ(PQresultStatus(result), PGRES_COMMAND_OK);
    PQclear(result);

    result = cat_pq_exec_prepared(conn, stmt_name.c_str(), 0, nullptr, nullptr, nullptr, 0);
    ASSERT_EQ(PQresultStatus(result), PGRES_TUPLES_OK);
    *column_count = PQnfields(result);
    *row_count = PQntuples(result);
    PQclear(result);

    std::string dtor_stmt_name = string_format("DEALLOCATE %s", stmt_name.c_str());
    result = cat_pq_exec(conn, dtor_stmt_name.c_str()); // stmt_dtor
    PQclear(result);

    PQfinish(conn);
}

TEST(cat_pq, connect_failed)
{
    SKIP_IF(skip());

    PGconn *conn = cat_pq_connectdb("host=127.0.0.1 port=1234 dbname=postgres user='postgres' password='postgres' connect_timeout=30");
    ASSERT_NE(PQstatus(conn), CONNECTION_OK);
    std::string error_message = std::string(PQerrorMessage(conn));
    ASSERT_NE(error_message.find("Connection refused"), std::string::npos);

    PQfinish(conn);
}

TEST(cat_pq, query)
{
    SKIP_IF(skip());

    int row_count = 0;
    int column_count = 0;

    cat_pq_query(&row_count, &column_count);
    ASSERT_EQ(row_count, 1);
    ASSERT_GT(column_count, 0);
}

TEST(cat_pq, concurrency)
{
    SKIP_IF(skip());

    ASSERT_TRUE(cat_coroutine_wait_all()); // call it before timing test
    auto concurrency = ([](uint32_t concurrency) {
        cat_sync_wait_group_t wg;
        ASSERT_NE(cat_sync_wait_group_create(&wg), nullptr);
        ASSERT_TRUE(cat_sync_wait_group_add(&wg, concurrency));
        for (size_t n = 0; n < concurrency; n++) {
            co([&wg] {
                int row_count = 0;
                int column_count = 0;

                cat_pq_query(&row_count, &column_count);
                ASSERT_EQ(row_count, 1);
                ASSERT_GT(column_count, 0);
                ASSERT_TRUE(cat_sync_wait_group_done(&wg));
            });
        }
        ASSERT_TRUE(cat_sync_wait_group_wait(&wg, TEST_IO_TIMEOUT));
    });

    if (!is_valgrind()) {
        auto s1 = cat_time_msec();
        concurrency(TEST_MAX_CONCURRENCY / 8);
        s1 = cat_time_msec() - s1;

        auto s2 = cat_time_msec();
        concurrency(TEST_MAX_CONCURRENCY / 2);
        s2 = cat_time_msec() - s2;

        ASSERT_LE(s2 / s1, 3.0);
    } else {
        concurrency(TEST_MAX_CONCURRENCY);
    }
}

TEST(cat_pq, parameterized_query)
{
    SKIP_IF(skip());
    PGconn *conn = cat_pq_connectdb(TEST_PQ_CONNINFO);
    ASSERT_NE(conn, nullptr);
    DEFER(PQfinish(conn));
    ASSERT_EQ(PQstatus(conn), CONNECTION_OK);
    const char *values[] = {"42", nullptr};
    PGresult *result = cat_pq_exec_params(conn,
        "SELECT $1::integer + 1, $2::text", 2, nullptr, values, nullptr, nullptr, 0);
    ASSERT_NE(result, nullptr);
    DEFER(PQclear(result));
    ASSERT_EQ(PQresultStatus(result), PGRES_TUPLES_OK);
    ASSERT_EQ(PQntuples(result), 1);
    ASSERT_EQ(PQnfields(result), 2);
    ASSERT_STREQ(PQgetvalue(result, 0, 0), "43");
    ASSERT_EQ(PQgetisnull(result, 0, 1), 1);
}

TEST(cat_pq, query_error_and_recovery)
{
    SKIP_IF(skip());
    PGconn *conn = cat_pq_connectdb(TEST_PQ_CONNINFO);
    ASSERT_NE(conn, nullptr);
    DEFER(PQfinish(conn));
    ASSERT_EQ(PQstatus(conn), CONNECTION_OK);
    PGresult *error = cat_pq_exec(conn, "SELECT ();");
    ASSERT_NE(error, nullptr);
    DEFER(PQclear(error));
    ASSERT_EQ(PQresultStatus(error), PGRES_FATAL_ERROR);
    ASSERT_STREQ(PQresultErrorField(error, PG_DIAG_SQLSTATE), "42601");
    // SQL 错误只影响该语句；连接仍可执行下一次合法查询。
    PGresult *result = cat_pq_exec(conn, "SELECT 17; SELECT 23;");
    ASSERT_NE(result, nullptr);
    DEFER(PQclear(result));
    ASSERT_EQ(PQresultStatus(result), PGRES_TUPLES_OK);
    ASSERT_EQ(PQntuples(result), 1);
    ASSERT_STREQ(PQgetvalue(result, 0, 0), "23");
}

TEST(cat_pq, invalid_connection_options)
{
    SKIP_IF(skip());
    PGconn *conn = cat_pq_connectdb("invalid_connection_option=value");
    ASSERT_NE(conn, nullptr);
    DEFER(PQfinish(conn));
    ASSERT_EQ(PQstatus(conn), CONNECTION_BAD);
    ASSERT_LT(PQsocket(conn), 0);
}

TEST(cat_pq, commands_on_failed_connection)
{
    PGconn *conn = cat_pq_connectdb("invalid_connection_option=value");
    ASSERT_NE(conn, nullptr);
    DEFER(PQfinish(conn));
    ASSERT_EQ(PQstatus(conn), CONNECTION_BAD);
    ASSERT_EQ(cat_pq_prepare(conn, "statement", "SELECT 1", 0, nullptr), nullptr);
    ASSERT_EQ(cat_pq_exec_prepared(conn, "statement", 0, nullptr, nullptr, nullptr, 0), nullptr);
    ASSERT_EQ(cat_pq_exec(conn, "SELECT 1"), nullptr);
    ASSERT_EQ(cat_pq_exec_params(conn, "SELECT 1", 0, nullptr, nullptr, nullptr, nullptr, 0), nullptr);
    ASSERT_NE(PQerrorMessage(conn)[0], '\0');
}

TEST(cat_pq, copy_query_returns_in_progress_result)
{
    SKIP_IF(skip());
    PGconn *conn = cat_pq_connectdb(TEST_PQ_CONNINFO);
    ASSERT_NE(conn, nullptr);
    DEFER(PQfinish(conn));
    ASSERT_EQ(PQstatus(conn), CONNECTION_OK);
    PGresult *result = cat_pq_exec(conn, "COPY (SELECT 42) TO STDOUT");
    ASSERT_NE(result, nullptr);
    DEFER(PQclear(result));
    ASSERT_EQ(PQresultStatus(result), PGRES_COPY_OUT);
    // COPY owns the connection until its data and final result have been consumed.
    char *data = nullptr;
    int length;
    while ((length = PQgetCopyData(conn, &data, 1)) == 0) {
        ASSERT_EQ(cat_poll_one(PQsocket(conn), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
        ASSERT_EQ(PQconsumeInput(conn), 1);
    }
    ASSERT_GT(length, 0);
    const std::string copied(data, length);
    PQfreemem(data);
    data = nullptr;
    ASSERT_EQ(copied, "42\n");
    while ((length = PQgetCopyData(conn, &data, 1)) == 0) {
        ASSERT_EQ(cat_poll_one(PQsocket(conn), POLLIN, nullptr, TEST_IO_TIMEOUT), CAT_RET_OK);
        ASSERT_EQ(PQconsumeInput(conn), 1);
    }
    ASSERT_EQ(length, -1);
    PGresult *completed = PQgetResult(conn);
    ASSERT_NE(completed, nullptr);
    DEFER(PQclear(completed));
    ASSERT_EQ(PQresultStatus(completed), PGRES_COMMAND_OK);
    ASSERT_EQ(PQgetResult(conn), nullptr);
    PGresult *next = cat_pq_exec(conn, "SELECT 43");
    ASSERT_NE(next, nullptr);
    DEFER(PQclear(next));
    ASSERT_EQ(PQresultStatus(next), PGRES_TUPLES_OK);
    ASSERT_STREQ(PQgetvalue(next, 0, 0), "43");
}

#endif
