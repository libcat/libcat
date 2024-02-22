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
  |         dixyes <dixyes@gmail.com>                                        |
  |         codinghuang <2812240764@qq.com>                                  |
  +--------------------------------------------------------------------------+
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <array>
#include <atomic>

#include "cat_api.h"

#ifdef CAT_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#ifndef EVP_RSA_gen
# define EVP_RSA_gen(bits) EVP_PKEY_Q_keygen(NULL, NULL, "RSA", (size_t)(0 + (bits)))
#endif
#endif // CAT_SSL

/* optional, not always included in api.h */
#include "cat_http.h"

/* ext, not enabled by default */
#include "cat_curl.h"

/* ext, not enabled by default */
#include "cat_pq.h"

/* GTEST_SKIP and GTEST_SKIP_ shim */
#ifndef GTEST_SKIP
# define GTEST_SKIP() {/* do nothing */}
#endif
#ifndef GTEST_SKIP_
# define GTEST_SKIP_(message) {printf("skip: %s\n", message);}
#endif

/* common macros */

#define SKIP_IF(expression) do { \
    if (expression) { \
        GTEST_SKIP(); \
        return; \
    } \
} while (0)

#define SKIP_IF_(expression, message) do { \
    if (expression) { \
        GTEST_SKIP_(message); \
        return; \
    } \
} while (0)

#define SKIP_IF_OFFLINE()      SKIP_IF_(is_offline(), "Internet connection required")
#define SKIP_IF_USE_VALGRIND() SKIP_IF_(is_valgrind(), "Valgrind is too slow")

#define TEST_BUFFER_SIZE_STD               8192

#define TEST_IO_TIMEOUT                    ::testing::CONFIG_IO_TIMEOUT

#define TEST_MAX_REQUESTS                  ::testing::CONFIG_MAX_REQUESTS
#define TEST_MAX_CONCURRENCY               ::testing::CONFIG_MAX_CONCURRENCY

#define TEST_LISTEN_HOST                   "localhost"
#define TEST_LISTEN_IPV4                   "127.0.0.1"
#define TEST_LISTEN_IPV6                   "::1"
#define TEST_TMP_PATH                      ::testing::CONFIG_TMP_PATH.c_str()
#ifndef CAT_OS_WIN
#define TEST_PATH_SEP                      "/"
#define TEST_PIPE_PATH                     "/tmp/cat_test.sock"
#define TEST_PIPE_PATH_FMT                 "/tmp/cat_test_%s.sock"
#else
#define TEST_PATH_SEP                      "\\"
#define TEST_PIPE_PATH                     "\\\\?\\pipe\\cat_test"
#define TEST_PIPE_PATH_FMT                 "\\\\?\\pipe\\cat_test_%s"
#endif
#define TEST_SERVER_BACKLOG                8192

#define TEST_PQ_CONNINFO "host=127.0.0.1 dbname=postgres user='postgres' password='postgres' connect_timeout=30"

#define TEST_REMOTE_HTTP_SERVER            ::testing::CONFIG_REMOTE_HTTP_SERVER_HOST.c_str(), ::testing::CONFIG_REMOTE_HTTP_SERVER_HOST.length(), ::testing::CONFIG_REMOTE_HTTP_SERVER_PORT
#define TEST_REMOTE_HTTP_SERVER_HOST       ::testing::CONFIG_REMOTE_HTTP_SERVER_HOST.c_str()
#define TEST_REMOTE_HTTP_SERVER_PORT       ::testing::CONFIG_REMOTE_HTTP_SERVER_PORT
#define TEST_REMOTE_HTTP_SERVER_KEYWORD    ::testing::CONFIG_REMOTE_HTTP_SERVER_KEYWORD

#define TEST_REMOTE_HTTPS_SERVER           ::testing::CONFIG_REMOTE_HTTPS_SERVER_HOST.c_str(), ::testing::CONFIG_REMOTE_HTTPS_SERVER_HOST.length(), ::testing::CONFIG_REMOTE_HTTPS_SERVER_PORT
#define TEST_REMOTE_HTTPS_SERVER_HOST      ::testing::CONFIG_REMOTE_HTTPS_SERVER_HOST.c_str()
#define TEST_REMOTE_HTTPS_SERVER_PORT      ::testing::CONFIG_REMOTE_HTTPS_SERVER_PORT
#define TEST_REMOTE_HTTPS_SERVER_KEYWORD   ::testing::CONFIG_REMOTE_HTTPS_SERVER_KEYWORD

#define TEST_REMOTE_IPV6_HTTP_SERVER_HOST  ::testing::CONFIG_REMOTE_IPV6_HTTP_SERVER_HOST.c_str()

#define TEST_HTTP_STATUS_CODE_OK           200
#define TEST_HTTP_CLIENT_FAKE_USERAGENT    "curl/7.58.0"

#define PP_CAT(a, b) PP_CAT_I(a, b)
#define PP_CAT_I(a, b) PP_CAT_II(~, a ## b)
#define PP_CAT_II(p, res) res
#define UNIQUE_NAME(base) PP_CAT(base, __LINE__)

#define DEFER(X)     std::shared_ptr<void> UNIQUE_NAME(defer)(nullptr, [&](...){ X; })
#define DEFER2(X, N) std::shared_ptr<void> __DEFER__##N(nullptr, [&](...){ X; })

/* test dependency management */

#define TEST_REQUIREMENT_NAME(test_suite_name, test_name) \
        _test_##test_suite_name##_##test_name

#define TEST_REQUIREMENT_DTOR_NAME(test_suite_name, test_name) \
        _test_##test_suite_name##_##test_name##_dtor

#define TEST_REQUIREMENT(test_suite_name, test_name) \
TEST_REQUIREMENT_DTOR(test_suite_name, test_name); \
void TEST_REQUIREMENT_NAME(test_suite_name, test_name)(void)

#define TEST_REQUIREMENT_DTOR(test_suite_name, test_name) \
void TEST_REQUIREMENT_DTOR_NAME(test_suite_name, test_name)(void)

#define TEST_REQUIRE(condition, test_suite_name, test_name) \
    if (!(condition)) { \
        TEST_REQUIREMENT_NAME(test_suite_name, test_name)(); \
        SKIP_IF_(!(condition), #test_suite_name "." #test_name " is not available"); \
    } \
    DEFER(TEST_REQUIREMENT_DTOR_NAME(test_suite_name, test_name)())

/* cover all test source files */

using namespace testing;

namespace testing
{
    /* common vars */

    extern cat_timeout_t CONFIG_IO_TIMEOUT;
    extern uint32_t CONFIG_MAX_REQUESTS;
    extern uint32_t CONFIG_MAX_CONCURRENCY;

    /* REMOTE_HTTP_SERVER */
    extern std::string CONFIG_REMOTE_HTTP_SERVER_HOST;
    extern int CONFIG_REMOTE_HTTP_SERVER_PORT;
    extern std::string CONFIG_REMOTE_HTTP_SERVER_KEYWORD;
    /* REMOTE_HTTPS_SERVER */
    extern std::string CONFIG_REMOTE_HTTPS_SERVER_HOST;
    extern int CONFIG_REMOTE_HTTPS_SERVER_PORT;
    extern std::string CONFIG_REMOTE_HTTPS_SERVER_KEYWORD;
    /* REMOTE_IPV6_HTTP_SERVER */
    extern std::string CONFIG_REMOTE_IPV6_HTTP_SERVER_HOST;
    /* TMP_PATH */
    extern std::string CONFIG_TMP_PATH;

    /* common functions */

    static inline bool is_valgrind(void)
    {
#ifdef CAT_HAVE_ASAN
        return true;
#else
        return cat_env_is_true("USE_VALGRIND", cat_false);
#endif
    }

    static inline bool is_offline(void)
    {
        return cat_env_is_true("OFFLINE", cat_false);
    }

    // https://stackoverflow.com/questions/2342162/stdstring-formatting-like-sprintf
    template <typename... Args>
    std::string string_format(const char *format, Args... args)
    {
        size_t size = (size_t) std::snprintf(nullptr, 0, format, args...) + 1; // Extra space for '\0'
        CAT_ASSERT(((ssize_t) size) > 0);
        char *buffer = (char *) cat_malloc(size);
        CAT_ASSERT(buffer != NULL);
        DEFER(cat_free(buffer));
        std::snprintf(buffer, size, format, args...);
        return std::string(buffer, buffer + size - 1); // We don't want the '\0' inside
    }

    bool has_debugger(void);
    const char *get_debugger_name(void);

    bool file_exists(const char *filename);
    std::string file_get_contents(const char *filename);
    bool file_put_contents(const char *filename, const std::string content);
    bool file_put_contents(const char *filename, const char *content, size_t length);
    bool remove_file(const char *filename);

    std::string get_random_bytes(size_t length = TEST_BUFFER_SIZE_STD);

    static inline std::string get_random_path(size_t length = 8)
    {
        std::string random_bytes = get_random_bytes(length);
        return string_format("%s/%s", TEST_TMP_PATH, random_bytes.c_str());
    }

    cat_coroutine_t *co(std::function<void(void)> function);
    bool defer(std::function<void(void)> function);
    bool work(cat_work_kind_t kind, std::function<void(void)> function, cat_timeout_t timeout);

    void register_shutdown_function(std::function<void(void)> function);

    /* common classes */

    class wait_group
    {
    protected:
        cat_sync_wait_group_t wg;

    public:

        wait_group(ssize_t delta = 0)
        {
            (void) cat_sync_wait_group_create(&wg);
            if (!cat_sync_wait_group_add(&wg, delta)) {
                throw cat_get_last_error_message();
            }
        }

        ~wait_group()
        {
            cat_sync_wait_group_wait(&wg, CAT_TIMEOUT_FOREVER);
        }

        wait_group &operator++() // front
        {
            if (!cat_sync_wait_group_add(&wg, 1)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        wait_group &operator++(int o) // back
        {
            if (!cat_sync_wait_group_add(&wg, 1)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        wait_group &operator--() // front
        {
            if (!cat_sync_wait_group_done(&wg)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        wait_group &operator--(int o) // back
        {
            if (!cat_sync_wait_group_done(&wg)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        wait_group &operator+(int delta)
        {
            if (!cat_sync_wait_group_add(&wg, delta)) {
                throw cat_get_last_error_message();
            }
            return *this;
        }

        bool operator()(cat_timeout_t timeout = CAT_TIMEOUT_FOREVER)
        {
            return cat_sync_wait_group_wait(&wg, timeout);
        }
    };

#ifdef CAT_SSL

    enum X509Type
    {
        X509TypeRSA = 1,
        X509TypeECDSA = 2,
        X509TypeSM2 = 3,
    };
    enum CertFlags
    {
        CertFlagsServer = 1 << 0,
        CertFlagsClient = 1 << 1,
    };

    struct X509KeyCertPair
    {
        const char *key;
        const char *cert;
    };
    class X509KeyCertFilePair
    {
        public:
#ifndef PATH_MAX
# define PATH_MAX 256
#endif
            char keyFile[PATH_MAX];
            char certFile[PATH_MAX];
            char caCertFile[PATH_MAX];
            char chainFile[PATH_MAX];

            X509KeyCertFilePair(const char* caCert, X509KeyCertPair pair) {
                static std::atomic<int> serial;

                std::string path;
                std::string random_filename = string_format("%s/libcat_test_%08x", TEST_TMP_PATH, serial.fetch_add(1));

                path = string_format("%s_ca.crt", random_filename.c_str());
                file_put_contents(path.c_str(), caCert, strlen(caCert));
                strncpy(caCertFile, path.c_str(), sizeof(caCertFile) - 1);

                path = string_format("%s.crt", random_filename.c_str());
                file_put_contents(path.c_str(), pair.cert, strlen(pair.cert));
                strncpy(certFile, path.c_str(), sizeof(certFile) - 1);

                path = string_format("%s.key", random_filename.c_str());
                file_put_contents(path.c_str(), pair.key, strlen(pair.key));
                strncpy(keyFile, path.c_str(), sizeof(keyFile) - 1);

                path = string_format("%s_chain.crt", random_filename.c_str());
                FILE *file = fopen(path.c_str(),
                    "w"
#ifdef CAT_OS_WIN
                    // for no LF -> CRLF auto translation on Windows
                    "b"
#endif
                );
                fwrite(pair.cert, 1, strlen(pair.cert), file);
                fwrite("\n", 1, strlen("\n"), file);
                fwrite(caCert, 1, strlen(caCert), file);
                fclose(file);
                strncpy(chainFile, path.c_str(), sizeof(chainFile) - 1);
                    
            }

            ~X509KeyCertFilePair() {
                remove_file(keyFile);
                remove_file(certFile);
                remove_file(caCertFile);
                remove_file(chainFile);
            }
    };

    struct X509CA
    {
        const char *key;
        const char *cert;
        EVP_PKEY *pkey;
        X509 *x509;
    };

# define checkOpenSSL(failcond) \
    do { \
        if (failcond) { \
            long err = ERR_get_error(); \
            char buf[256]; \
            ERR_error_string_n(err, buf, sizeof(buf)); \
            fprintf(stderr, __FILE__ ":%d OpenSSL error: %s\n", __LINE__, buf); \
            goto fail; \
        } \
    } while (0)


    class X509util
    {
    public:
        X509Type type;

        const char *caCert;
        const char *caKey;
        std::atomic<long> serial;

        static X509util *newRSA()
        {
            X509util *x509 = new X509util();
            x509->caKey = newRSAKey(nullptr);
            if (x509->caKey == nullptr)
            {
                delete x509;
                return nullptr;
            }
            X509CA ca = newCA(x509->caKey);
            if (ca.cert == nullptr)
            {
                free((void *)x509->caKey);
                delete x509;
                return nullptr;
            }
            x509->caCert = ca.cert;
            x509->caPKEY = ca.pkey;
            x509->caX509 = ca.x509;

            x509->type = X509TypeRSA;
            x509->serial.store(2);
            return x509;
        }

        static const char *newRSAKey(const char *passpharse)
        {
            int ret;
            BIO *bio = nullptr;
            EVP_PKEY *pkey = nullptr;
            const char *pem = nullptr;
            size_t len;

            bio = BIO_new(BIO_s_mem());

            pkey = EVP_RSA_gen(rsaBits);
            checkOpenSSL(pkey == nullptr);

            if (passpharse == nullptr) {
                ret = PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
            } else {
                ret = PEM_write_bio_PrivateKey(bio, pkey, EVP_aes_256_cbc(), (unsigned char *)passpharse, strlen(passpharse), nullptr, nullptr);
            }
            checkOpenSSL(ret != 1);

            len = BIO_pending(bio);
            pem = (char *)calloc(len + 1, 1);
            checkOpenSSL(pem == nullptr);
            ret = BIO_read(bio, (void *)pem, len);
            if (ret <= 0)
            {
                checkOpenSSL(true);
                free((void *)pem);
            }

        fail:
            if (bio != nullptr)
            {
                BIO_free(bio);
            }
            if (pkey != nullptr)
            {
                EVP_PKEY_free(pkey);
            }
            return pem;
        }

        X509KeyCertFilePair *newCertFile(
            int flags,
            const char *passphrase,
            const char *commonName,
            time_t secNotBeforeOffset,
            time_t secNotAfterOffset,
            const char *sanIP
        ) {
            std::string path;
            X509KeyCertPair pair = newCert(
                flags, newRSAKey(passphrase), passphrase, commonName, secNotBeforeOffset, secNotAfterOffset, sanIP);
            if (pair.cert == nullptr) {
                return nullptr;
            }
            DEFER([pair]{
                free((void *)pair.cert);
                free((void *)pair.key);
            }());
            
            return new X509KeyCertFilePair(caCert, pair);
        }

        X509KeyCertPair newCert(
            int flags,
            const char *pemKey,
            const char *passphrase,
            const char *commonName,
            time_t secNotBeforeOffset,
            time_t secNotAfterOffset,
            const char *sanIP
        ) {
            int ret;
            BIO *bio = nullptr;
            EVP_PKEY *pkey = nullptr;
            X509_NAME *name;
            X509 *x509 = nullptr;
            X509V3_CTX ctx = {0};
            X509_EXTENSION *ext = nullptr;
            const char *pem = nullptr;
            char *sanConf;
            size_t len;

            bio = BIO_new(BIO_s_mem());
            pkey = EVP_PKEY_new();

            // read pem
            ret = BIO_write(bio, pemKey, strlen(pemKey));
            checkOpenSSL(ret <= 0);
            BIO_seek(bio, 0);
            pkey = PEM_read_bio_PrivateKey(
                bio,
                &pkey,
                [](char *buf, int size, int rwflag, void *u) {
                    strncpy(buf, (const char *)u, size);
                    return (int)(strlen((const char *)u) > size ? size : strlen((const char *)u));
                },
                (void *)passphrase
            );
            checkOpenSSL(pkey == nullptr);

            // generate x509
            x509 = X509_new();
            ret = X509_set_version(x509, 2);
            checkOpenSSL(ret != 1);
            ret = X509_set_pubkey(x509, pkey);
            checkOpenSSL(ret != 1);
            ASN1_INTEGER_set(X509_get_serialNumber(x509), serial.fetch_add(1));

            name = X509_get_subject_name(x509);
            ret = X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (const unsigned char *)"CN", -1, -1, 0);
            checkOpenSSL(ret != 1);
            ret = X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (const unsigned char *)commonName, -1, -1, 0);
            checkOpenSSL(ret != 1);
            ret = X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)commonName, -1, -1, 0);
            checkOpenSSL(ret != 1);

            ret = X509_set_issuer_name(x509, X509_get_subject_name(caX509));
            checkOpenSSL(ret != 1);
            X509_gmtime_adj(X509_get_notBefore(x509), secNotBeforeOffset);
            X509_gmtime_adj(X509_get_notAfter(x509), secNotAfterOffset);

            // set extensions
            X509V3_set_ctx(&ctx, caX509, x509, nullptr, nullptr, 0);
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, "critical,digitalSignature");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);
            if ((flags & CertFlagsServer) && (flags & CertFlagsClient))
            {
                ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage, "serverAuth,clientAuth");
                checkOpenSSL(ext == nullptr);
                ret = X509_add_ext(x509, ext, -1);
            }
            else if (flags & CertFlagsServer)
            {
                ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage, "serverAuth");
                checkOpenSSL(ext == nullptr);
                ret = X509_add_ext(x509, ext, -1);
            }
            else if (flags & CertFlagsClient)
            {
                ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage, "clientAuth");
                checkOpenSSL(ext == nullptr);
                ret = X509_add_ext(x509, ext, -1);
            }
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:FALSE");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_key_identifier, "hash");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_authority_key_identifier, "keyid:always");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);

            sanConf = (char *)calloc(4096, 1); // 懒得算了
            if (sanIP == nullptr)
            {
                snprintf(sanConf, 4096, "DNS:%s", commonName);
            }
            else
            {
                snprintf(sanConf, 4096, "DNS:%s,IP:%s", commonName, sanIP);
            }
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name, sanConf);
            free(sanConf);
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);

            // CPS这么搞不行
            // https://stackoverflow.com/questions/21409677/not-able-to-add-certificate-policy-extension-using-openssl-apis-in-c
            // ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_certificate_policies, "2.23.140.1.2.1");
            // checkOpenSSL(ext == nullptr);
            // ret = X509_add_ext(x509, ext, -1);
            // checkOpenSSL(ret != 1);

            // sign
            ret = X509_sign(x509, caPKEY, EVP_sha256());
            checkOpenSSL(ret == 0);

            // write pem
            BIO_reset(bio);
            ret = PEM_write_bio_X509(bio, x509);
            checkOpenSSL(ret != 1);

            len = BIO_pending(bio);
            pem = (char *)calloc(len + 1, 1);
            checkOpenSSL(pem == nullptr);
            ret = BIO_read(bio, (void *)pem, len);
            if (ret <= 0)
            {
                free((void *)pem);
                checkOpenSSL(true);
            }

        fail:
            if (bio != nullptr)
            {
                BIO_free(bio);
            }
            if (pkey != nullptr)
            {
                EVP_PKEY_free(pkey);
            }
            if (x509 != nullptr)
            {
                X509_free(x509);
            }
            return {pemKey, pem};
        }
        ~X509util()
        {
            free((void *)caKey);
            free((void *)caCert);
            if (caPKEY != nullptr)
            {
                EVP_PKEY_free(caPKEY);
            }
            if (caX509 != nullptr)
            {
                X509_free(caX509);
            }
        }

    private:
        // rsa args
        static constexpr int rsaBits = 2048;

        // ca args
        static constexpr const char *caCN = "ca.local";

        EVP_PKEY *caPKEY;
        X509 *caX509;

        static X509CA newCA(const char *caKey)
        {
            int ret;
            BIO *bio = nullptr;
            EVP_PKEY *pkey = nullptr;
            X509_NAME *name;
            X509 *x509 = nullptr;
            X509V3_CTX ctx = {0};
            X509_EXTENSION *ext = nullptr;
            const char *pem = nullptr;
            size_t len;


            bio = BIO_new(BIO_s_mem());
            pkey = EVP_PKEY_new();

            // read pem
            ret = BIO_write(bio, caKey, strlen(caKey));
            checkOpenSSL(ret <= 0);
            BIO_seek(bio, 0);
            pkey = PEM_read_bio_PrivateKey(bio, &pkey, nullptr, nullptr);
            checkOpenSSL(pkey == nullptr);

            // generate x509
            x509 = X509_new();
            ret = X509_set_version(x509, 2);
            checkOpenSSL(ret != 1);
            ret = X509_set_pubkey(x509, pkey);
            checkOpenSSL(ret != 1);
            ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

            name = X509_get_subject_name(x509);
            ret = X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (const unsigned char *)"CN", -1, -1, 0);
            checkOpenSSL(ret != 1);
            ret = X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (const unsigned char *)"local CA", -1, -1, 0);
            checkOpenSSL(ret != 1);
            ret = X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)caCN, -1, -1, 0);
            checkOpenSSL(ret != 1);

            ret = X509_set_issuer_name(x509, name);
            checkOpenSSL(ret != 1);
            X509_gmtime_adj(X509_get_notBefore(x509), 0);
            X509_gmtime_adj(X509_get_notAfter(x509), 100 * 31536000L);

            // set extensions
            X509V3_set_ctx(&ctx, x509, x509, nullptr, nullptr, 0);
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, "critical,CA:TRUE");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, "critical,digitalSignature,keyCertSign,cRLSign");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_key_identifier, "hash");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_authority_key_identifier, "keyid:always");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);

            // sign
            ret = X509_sign(x509, pkey, EVP_sha256());
            checkOpenSSL(ret == 0);

            // write pem
            BIO_reset(bio);
            ret = PEM_write_bio_X509(bio, x509);
            checkOpenSSL(ret != 1);

            len = BIO_pending(bio);
            pem = (char *)calloc(len + 1, 1);
            if (pem == nullptr)
            {
                // impossible
                goto fail;
            }
            ret = BIO_read(bio, (void *)pem, len);
            if (ret <= 0)
            {
                checkOpenSSL(true);
                free((void *)pem);
            }

            if (bio != nullptr)
            {
                BIO_free(bio);
            }
            return {caKey, pem, pkey, x509};
        fail:
            if (bio != nullptr)
            {
                BIO_free(bio);
            }
            if (pkey != nullptr)
            {
                EVP_PKEY_free(pkey);
            }
            if (x509 != nullptr)
            {
                X509_free(x509);
            }
            return {nullptr, nullptr, nullptr, nullptr};
        }
    };

#undef checkOpenSSL

    extern X509util *x509;
#endif // CAT_SSL
}
