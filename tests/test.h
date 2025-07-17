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
#include <unordered_map>

#include "cat_api.h"

#ifdef CAT_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/x509v3.h>
#include <openssl/rsa.h>
#undef EVP_RSA_gen
// #ifndef EVP_RSA_gen
static inline EVP_PKEY *EVP_RSA_gen(size_t bits) {
    EVP_PKEY *pkey = nullptr;
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx) {
        goto fail;
    }
    if (!EVP_PKEY_keygen_init(ctx)) {
        goto fail;
    }
    if (!EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits)) {
        goto fail;
    }
    if (!EVP_PKEY_keygen(ctx, &pkey)) {
        goto fail;
    }
fail:
    if (ctx) {
        EVP_PKEY_CTX_free(ctx);
    }
    return pkey;
}
// #endif
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

    typedef class X509KeyCertPair X509KeyCertPair;
    typedef std::unordered_map<std::string, std::variant<
        std::string,
        EVP_PKEY *,
        std::shared_ptr<X509KeyCertPair>,
        int32_t
    >> X509KeyCertPairConfig;
    typedef struct X509KeyPairPath {
        const char *key;
        const char *cert;
    } X509KeyPairPath;

    class X509KeyCertPair
    {
    private:
        EVP_PKEY *pkey;
        X509 *x509;
        std::atomic<int> serial;
        X509KeyPairPath exportPath = {nullptr, nullptr};
    public:
        X509KeyCertPair(
            EVP_PKEY *pkey,
            X509 *x509
        )
        {
            this->pkey = pkey;
            this->x509 = x509;
            this->serial.store(0);
        }

        ~X509KeyCertPair()
        {
            if (pkey != nullptr)
            {
                EVP_PKEY_free(pkey);
            }
            if (x509 != nullptr)
            {
                X509_free(x509);
            }

            if (exportPath.key != nullptr) {
                remove_file(exportPath.key);
                free((void *)exportPath.key);
            }
            if (exportPath.cert != nullptr) {
                remove_file(exportPath.cert);
                free((void *)exportPath.cert);
            }
        }

        std::string privKeyPEMString(const char *passphrase = nullptr)
        {
            BIO *bio = BIO_new(BIO_s_mem());
            if (passphrase != nullptr) {
                PEM_write_bio_PrivateKey(bio, pkey, EVP_aes_256_cbc(), (unsigned char *)passphrase, strlen(passphrase), nullptr, nullptr);
            } else {
                PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr);
            }
            long len = BIO_get_mem_data(bio, nullptr);
            std::string ret(len, '\0');
            BIO_read(bio, (unsigned char *)ret.data(), len);
            BIO_free(bio);
            return ret;
        }

        std::string certPEMString()
        {
            BIO *bio = BIO_new(BIO_s_mem());
            PEM_write_bio_X509(bio, x509);
            long len = BIO_get_mem_data(bio, nullptr);
            std::string ret(len, '\0');
            BIO_read(bio, (unsigned char *)ret.data(), len);
            BIO_free(bio);
            return ret;
        }

        X509KeyPairPath exportPEMs(const char *passphrase = nullptr)
        {
            if (exportPath.key != nullptr && exportPath.cert != nullptr) {
                if (passphrase != nullptr) {
                    throw std::runtime_error("pems is already exported, passphrase is not supported");
                }
                return exportPath;
            }
            uint64_t exportSerial = 0;
            while (true) {
                RAND_bytes((unsigned char *)&exportSerial, sizeof(exportSerial));
                if (file_exists(string_format("%s/libcat_x509_%016x.key", TEST_TMP_PATH, exportSerial).c_str()) ||
                    file_exists(string_format("%s/libcat_x509_%016x.crt", TEST_TMP_PATH, exportSerial).c_str())) {
                    continue;
                }
                break;
            }

            auto keyPath = string_format("%s/libcat_x509_%016x.key", TEST_TMP_PATH, exportSerial);
            std::string privKeyPEM = privKeyPEMString(passphrase);
            file_put_contents(keyPath.c_str(), privKeyPEM.c_str(), privKeyPEM.length());

            auto certPath = string_format("%s/libcat_x509_%016x.crt", TEST_TMP_PATH, exportSerial);
            std::string certPEM = certPEMString();
            file_put_contents(certPath.c_str(), certPEM.c_str(), certPEM.length());

            exportPath.key = strdup(keyPath.c_str());
            exportPath.cert = strdup(certPath.c_str());
            return exportPath;
        }

        static std::shared_ptr<X509KeyCertPair> create(
            X509KeyCertPairConfig &config
        ) {
            int ret;
            X509_NAME *name;
            X509V3_CTX ctx = {0};
            X509_EXTENSION *ext = nullptr;
            EVP_PKEY *pkey = nullptr, *issuerPkey = nullptr;
            X509 *x509 = nullptr, *issuerX509 = nullptr;

            // prepare private key
            if (config.find("pkey") != config.end()) {
                pkey = std::get<EVP_PKEY *>(config["pkey"]);
            } else if (config.find("keyType") != config.end()) {
                std::string keyType = std::get<std::string>(config["keyType"]);
                if (keyType == "RSA2048") {
                    pkey = EVP_RSA_gen(2048);
                } else if (keyType == "RSA4096") {
                    pkey = EVP_RSA_gen(4096);
                } else if (keyType.compare(0, 2, "EC") == 0) {
                    pkey = EVP_EC_gen(keyType.substr(2).c_str());
                } else {
                    throw std::runtime_error("Invalid key type");
                }
            } else {
                throw std::runtime_error("No private key provided and key type not specified");
            }

            // generate x509
            x509 = X509_new();
            if (config.find("issuer") != config.end()) {
                auto issuer = std::get<std::shared_ptr<X509KeyCertPair>>(config["issuer"]);
                issuerPkey = issuer->pkey;
                issuerX509 = issuer->x509;
            } else {
                issuerPkey = pkey;
                issuerX509 = x509;
            }

            ret = X509_set_version(x509, 2);
            checkOpenSSL(ret != 1);
            ret = X509_set_pubkey(x509, pkey);
            checkOpenSSL(ret != 1);
            if (config.find("issuer") != config.end()) {
                auto issuer = std::get<std::shared_ptr<X509KeyCertPair>>(config["issuer"]);
                ASN1_INTEGER_set(X509_get_serialNumber(x509), issuer->serial.fetch_add(1));
            } else {
                ASN1_INTEGER_set(X509_get_serialNumber(x509), 0);
            }

            name = X509_get_subject_name(x509);
            for (std::string key : {"C", "O", "CN"}) {
                if (config.find(key) != config.end()) {
                    ret = X509_NAME_add_entry_by_txt(name, key.c_str(), MBSTRING_ASC, (const unsigned char *)std::get<std::string>(config[key]).c_str(), -1, -1, 0);
                    checkOpenSSL(ret != 1);
                }
            }

            ret = X509_set_issuer_name(x509, X509_get_subject_name(issuerX509));
            checkOpenSSL(ret != 1);
            if (config.find("notBeforeOffsetSeconds") != config.end()) {
                X509_gmtime_adj(X509_getm_notBefore(x509), std::get<int32_t>(config["notBeforeOffsetSeconds"]));
            }
            if (config.find("notAfterOffsetSeconds") != config.end()) {
                X509_gmtime_adj(X509_getm_notAfter(x509), std::get<int32_t>(config["notAfterOffsetSeconds"]));
            }
            X509V3_set_ctx(&ctx, issuerX509, x509, nullptr, nullptr, 0);

            // set extensions
            // keyUsage
            if (config.find("keyUsage") != config.end()) {
                ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage, std::get<std::string>(config["keyUsage"]).c_str());
                checkOpenSSL(ext == nullptr);
                ret = X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
                checkOpenSSL(ret != 1);
            }

            // extKeyUsage
            if (config.find("extKeyUsage") != config.end()) {
                ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_ext_key_usage, std::get<std::string>(config["extKeyUsage"]).c_str());
                checkOpenSSL(ext == nullptr);
                ret = X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
                checkOpenSSL(ret != 1);
            }

            // basicConstraints
            if (config.find("basicConstraints") != config.end()) {
                ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints, std::get<std::string>(config["basicConstraints"]).c_str());
                checkOpenSSL(ext == nullptr);
                ret = X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
                checkOpenSSL(ret != 1);
            }

            // subjectKeyIdentifier
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_key_identifier, "hash");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);

            // authorityKeyIdentifier
            ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_authority_key_identifier, "keyid:always");
            checkOpenSSL(ext == nullptr);
            ret = X509_add_ext(x509, ext, -1);
            X509_EXTENSION_free(ext);
            checkOpenSSL(ret != 1);

            // subjectAltName
            if (config.find("subjectAltName") != config.end()) {
                ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name, std::get<std::string>(config["subjectAltName"]).c_str());
                checkOpenSSL(ext == nullptr);
                ret = X509_add_ext(x509, ext, -1);
                X509_EXTENSION_free(ext);
                checkOpenSSL(ret != 1);
            }
            // TODO: certificate Policies
            // https://stackoverflow.com/questions/21409677/not-able-to-add-certificate-policy-extension-using-openssl-apis-in-c
            // ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_certificate_policies, "2.23.140.1.2.1");
            // checkOpenSSL(ext == nullptr);
            // ret = X509_add_ext(x509, ext, -1);
            // checkOpenSSL(ret != 1);

            // sign
            ret = X509_sign(x509, issuerPkey, EVP_sha256());
            checkOpenSSL(ret == 0);

            return std::make_shared<X509KeyCertPair>(pkey, x509);

        fail:
            if (pkey != nullptr)
            {
                EVP_PKEY_free(pkey);
            }
            if (x509 != nullptr)
            {
                X509_free(x509);
            }
            return nullptr;
        }
    };

    extern std::shared_ptr<X509KeyCertPair> publicCAPair;
#undef checkOpenSSL
#endif // CAT_SSL
}
