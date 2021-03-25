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
  | Author: codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include "test.h"
#include "uv/errno.h"

#ifdef CAT_OS_LINUX
TEST(cat_error, update_last_error_null)
{
    testing::internal::CaptureStderr();
    cat_update_last_error(CAT_UNKNOWN, nullptr);
    std::string output = testing::internal::GetCapturedStderr();
    ASSERT_EQ(output, "Sprintf last error message failed" CAT_EOL);
}
#endif

TEST(cat_error, strerror)
{
    std::string output;

    output = cat_strerror(CAT_UNKNOWN);
    ASSERT_EQ(output, "unknown error");
    output = cat_strerror(CAT_EAGAIN);
    ASSERT_EQ(output, "resource temporarily unavailable");
    output = cat_strerror(INT_MAX);
    ASSERT_EQ(output, "unknown error");
}

// from musl
#define ORIG_ERRNO_MAP(E) \
    E(E2BIG, 7)\
    E(EACCES, 13)\
    E(EADDRINUSE, 98)\
    E(EADDRNOTAVAIL, 99)\
    E(EAFNOSUPPORT, 97)\
    E(EAGAIN, 11)\
    E(EALREADY, 114)\
    E(EBADF, 9)\
    E(EBUSY, 16)\
    E(ECANCELED, 125)\
    E(ECHARSET, 84)\
    E(ECONNABORTED, 103)\
    E(ECONNREFUSED, 111)\
    E(ECONNRESET, 104)\
    E(EDESTADDRREQ, 89)\
    E(EEXIST, 17)\
    E(EFAULT, 14)\
    E(EFBIG, 27)\
    E(EHOSTUNREACH, 113)\
    E(EINTR, 4)\
    E(EINVAL, 22)\
    E(EIO, 5)\
    E(EISCONN, 106)\
    E(EISDIR, 21)\
    E(ELOOP, 40)\
    E(EMFILE, 24)\
    E(EMSGSIZE, 90)\
    E(ENAMETOOLONG, 36)\
    E(ENETDOWN, 100)\
    E(ENETUNREACH, 101)\
    E(ENFILE, 23)\
    E(ENOBUFS, 105)\
    E(ENODEV, 19)\
    E(ENOENT, 2)\
    E(ENOMEM, 12)\
    E(ENONET, 64)\
    E(ENOPROTOOPT, 92)\
    E(ENOSPC, 28)\
    E(ENOSYS, 38)\
    E(ENOTCONN, 107)\
    E(ENOTDIR, 20)\
    E(ENOTEMPTY, 39)\
    E(ENOTSOCK, 88)\
    E(ENOTSUP, 95)\
    E(EPERM, 1)\
    E(EPIPE, 32)\
    E(EPROTO, 71)\
    E(EPROTONOSUPPORT, 93)\
    E(EPROTOTYPE, 91)\
    E(ERANGE, 34)\
    E(EROFS, 30)\
    E(ESHUTDOWN, 108)\
    E(ESPIPE, 29)\
    E(ESRCH, 3)\
    E(ETIMEDOUT, 110)\
    E(ETXTBSY, 26)\
    E(EXDEV, 18)\
    E(ENXIO, 6)\
    E(EMLINK, 31)\
    E(EHOSTDOWN, 112)\
    E(EREMOTEIO, 121)\
    E(ENOTTY, 25)\
    E(EILSEQ, 84)

int ad = UV_EBADF;
TEST(cat_error, orig_strerror){
    #define ORIG_ASSERT(name, code) \
        ASSERT_EQ(std::string(strerror(code)), std::string(cat_orig_strerror(UV_##name))); \
        ASSERT_EQ(cat_orig_errno(UV_##name), code);
    ORIG_ERRNO_MAP(ORIG_ASSERT);
    #undef ORIG_ASSERT
    // no error
    ASSERT_EQ(std::string(strerror(0)), std::string(cat_orig_strerror(0)));
    // error
    ASSERT_EQ(std::string(strerror(114514)), std::string(cat_orig_strerror(114514)));
}