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
 */

#include "test.h"

#ifdef CAT_SSL
TEST(cat_ssl, allocated_connection_close_preserves_context)
{
    cat_ssl_context_t *context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOLS_DEFAULT);
    ASSERT_NE(context, nullptr);
    DEFER(cat_ssl_context_close(context));
    unsigned int references = CAT_REF_GET(context);
    for (unsigned int i = 0; i < 3; i++) {
        cat_ssl_t *ssl = cat_ssl_create(nullptr, context);
        ASSERT_NE(ssl, nullptr);
        ASSERT_EQ(CAT_REF_GET(context), references + 1);
        cat_ssl_close(ssl);
        ASSERT_EQ(CAT_REF_GET(context), references);
    }
}
#endif
