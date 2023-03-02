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
  | Author: Twosee <twosee@php.net>                                          |
  +--------------------------------------------------------------------------+
 */

#include "test.h"
#ifdef CAT_PQ

static void cat_pq_query(int *row_count, int *column_count)
{
    char stmt_name[32];
    char dtor_stmt_name[48];
    int stmt_counter = 0;

    PGconn *conn = cat_pq_connectdb(TEST_PQ_CONNINFO);
    ASSERT_EQ(PQstatus(conn), CONNECTION_OK);

    sprintf(stmt_name, "pdo_stmt_%08x", ++stmt_counter);
    PGresult *result = cat_pq_prepare(conn, stmt_name, "SELECT * FROM pg_catalog.pg_tables limit 1", 0, nullptr);
    ASSERT_EQ(PQresultStatus(result), PGRES_COMMAND_OK);
    PQclear(result);

    result = cat_pq_exec_prepared(conn, stmt_name, 0, nullptr, nullptr, nullptr, 0);
    ASSERT_EQ(PQresultStatus(result), PGRES_TUPLES_OK);
    *column_count = PQnfields(result);
    *row_count = PQntuples(result);
    PQclear(result);

    sprintf(dtor_stmt_name, "DEALLOCATE %s", stmt_name);
    result = cat_pq_exec(conn, dtor_stmt_name); // stmt_dtor
    PQclear(result);

    PQfinish(conn);
}

TEST(cat_pq, connect_failed)
{
    SKIP_IF(cat_os_is_windows());

    PGconn *conn = cat_pq_connectdb("host=127.0.0.1 port=1234 dbname=postgres user='postgres' password='postgres' connect_timeout=30");
    ASSERT_NE(PQstatus(conn), CONNECTION_OK);
    std::string error_message = std::string(PQerrorMessage(conn));
    ASSERT_NE(error_message.find("Connection refused"), std::string::npos);

    PQfinish(conn);
}

TEST(cat_pq, query)
{
    SKIP_IF(cat_os_is_windows());

    int row_count = 0;
    int column_count = 0;

    cat_pq_query(&row_count, &column_count);
    ASSERT_EQ(row_count, 1);
    ASSERT_GT(column_count, 0);
}

TEST(cat_pq, concurrency)
{
    SKIP_IF(cat_os_is_windows());

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

#endif
