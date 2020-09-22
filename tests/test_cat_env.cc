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

#define TEST_ENV_NAME "TEST_ENV_" CAT_TO_STR(CAT_MAGIC_NUMBER)

TEST(cat_env, base)
{
    char *env, rand[CAT_ENV_BUFFER_SIZE + 1];
    ASSERT_TRUE(cat_env_set(TEST_ENV_NAME, "foo"));
    env = cat_env_get(TEST_ENV_NAME);
    ASSERT_NE(env, nullptr);
    DEFER(cat_free(env));
    ASSERT_EQ(std::string("foo"), std::string(env));
    ASSERT_NE(cat_srand(CAT_STRS(rand)), nullptr);
    ASSERT_TRUE(cat_env_set(TEST_ENV_NAME, rand));
    ASSERT_TRUE(cat_env_exists(TEST_ENV_NAME));
    cat_free(env);
    env = cat_env_get(TEST_ENV_NAME);
    ASSERT_NE(env, nullptr);
    ASSERT_EQ(std::string(rand), std::string(env));
    ASSERT_TRUE(cat_env_is(TEST_ENV_NAME, rand, cat_false));
    ASSERT_TRUE(cat_env_unset(TEST_ENV_NAME));
    ASSERT_FALSE(cat_env_is(TEST_ENV_NAME, rand, cat_false));
    cat_free(env);
    env = cat_env_get(TEST_ENV_NAME);
    ASSERT_EQ(env, nullptr);
    ASSERT_FALSE(cat_env_exists(TEST_ENV_NAME));
}

TEST(cat_env, error)
{
    ASSERT_EQ(cat_env_get(nullptr), nullptr);
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_FALSE(cat_env_set("foo", nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_FALSE(cat_env_set(nullptr, "foo"));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
    ASSERT_FALSE(cat_env_unset(nullptr));
    ASSERT_EQ(cat_get_last_error_code(), CAT_EINVAL);
}

TEST(cat_env, is_true)
{
    ASSERT_FALSE(cat_env_is_true(TEST_ENV_NAME, cat_false));
    ASSERT_TRUE(cat_env_is_true(TEST_ENV_NAME, cat_true));
    ASSERT_TRUE(cat_env_set(TEST_ENV_NAME, "1"));
    ASSERT_TRUE(cat_env_is_true(TEST_ENV_NAME, cat_false));
    ASSERT_TRUE(cat_env_set(TEST_ENV_NAME, "0"));
    ASSERT_FALSE(cat_env_is_true(TEST_ENV_NAME, cat_true));
    ASSERT_TRUE(cat_env_unset(TEST_ENV_NAME));
}
