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
#ifdef CAT_SSL

# define checkOpenSSL(failcond) \
    do { \
        if (failcond) { \
            long err = ERR_get_error(); \
            char buf[256]; \
            ERR_error_string_n(err, buf, sizeof(buf)); \
            fprintf(stderr, __FILE__ ":%d OpenSSL error: %s\n", __LINE__, buf); \
            return cat_false; \
        } \
    } while (0)

TEST(cat_ssl, remote_https_server)
{
    SKIP_IF_OFFLINE();
    cat_socket_t *socket;
    char buffer[TEST_BUFFER_SIZE_STD];
    ssize_t nread;

    socket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
    ASSERT_NE(socket, nullptr);
    DEFER(cat_socket_close(socket));

    ASSERT_TRUE(cat_socket_connect_to(socket, TEST_REMOTE_HTTPS_SERVER));
    ASSERT_TRUE(cat_socket_enable_crypto(socket, nullptr));

    char *request = cat_sprintf(
        "GET / HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Accept: */*\r\n"
        "\r\n",
        TEST_REMOTE_HTTPS_SERVER_HOST,
        TEST_HTTP_CLIENT_FAKE_USERAGENT
    );
    ASSERT_NE(request, nullptr);
    DEFER(cat_free(request));
    ASSERT_TRUE(cat_socket_send(socket, request, strlen(request)));
    nread = cat_socket_recv(socket, CAT_STRS(buffer));
    ASSERT_GT(nread, 0);
    CAT_LOG_DEBUG(TEST_SOCKET, "Data[%zd]: %.*s", nread, (int) nread, buffer);
    ASSERT_NE(std::string(buffer, nread).find(TEST_REMOTE_HTTPS_SERVER_KEYWORD), std::string::npos);
}

typedef struct test_load_cert_s {
    const char *caCert;
    const char *caKey;
    const char *severCert;
    const char *severKey;
    const char *clientCert;
    const char *clientKey;
} test_load_cert_t;

static cat_bool_t load_cert_test_callback(cat_ssl_context_t *context, cat_socket_crypto_options_t *options)
{
    BIO *bio;
    X509 *x;
    EVP_PKEY *pkey;
    test_load_cert_t *certs = (test_load_cert_t *)options->context;
    if (!certs || !certs->caCert || !certs->caKey || !certs->severCert || !certs->severKey) {
        // never here
        return cat_false;
    }

    bio = BIO_new(BIO_s_mem());
    checkOpenSSL(bio == nullptr);
    DEFER(BIO_free(bio));

    // load private key
    pkey = EVP_PKEY_new();
    (void) BIO_seek(bio, 0);
    checkOpenSSL(BIO_write(bio, certs->severKey, strlen(certs->severKey)) <= 0);
    (void) BIO_seek(bio, 0);
    pkey = PEM_read_bio_PrivateKey(bio, &pkey, nullptr, nullptr);
    checkOpenSSL(pkey == nullptr);
    DEFER(EVP_PKEY_free(pkey));

    x = X509_new();
    DEFER(X509_free(x));

    // load server cert
    (void) BIO_seek(bio, 0);
    checkOpenSSL(BIO_write(bio, certs->severCert, strlen(certs->severCert)) <= 0);
    (void) BIO_seek(bio, 0);
    x = PEM_read_bio_X509(bio, &x, nullptr, nullptr);
    checkOpenSSL(x == nullptr);

    // use server cert
    checkOpenSSL(SSL_CTX_use_certificate(context->ctx, x) != 1);

    X509_free(x);
    x = X509_new();

    // load ca cert
    (void) BIO_seek(bio, 0);
    checkOpenSSL(BIO_write(bio, certs->caCert, strlen(certs->caCert)) <= 0);
    (void) BIO_seek(bio, 0);
    x = PEM_read_bio_X509(bio, &x, nullptr, nullptr);
    checkOpenSSL(x == nullptr);

    // add ca cert (with add1 +1 refcount)
    checkOpenSSL(SSL_CTX_add1_chain_cert(context->ctx, x) != 1);

    // rewind
    (void) BIO_seek(bio, 0);

    checkOpenSSL(SSL_CTX_use_PrivateKey(context->ctx, pkey) != 1);

    checkOpenSSL(SSL_CTX_check_private_key(context->ctx) != 1);

    return cat_true;
}

static cat_bool_t load_ca_test_callback(cat_ssl_context_t *context, cat_socket_crypto_options_t *options)
{
    printf("load_ca_test_callback\n");
    test_load_cert_t *certs = (test_load_cert_t *)options->context;
    if (!certs || !certs->caCert) {
        // never here
        return cat_false;
    }

    BIO *bio;
    X509 *cert;
    X509_STORE *cert_store;

    cert_store = SSL_CTX_get_cert_store(context->ctx);
    checkOpenSSL(cert_store == nullptr);

    bio = BIO_new(BIO_s_mem());
    checkOpenSSL(bio == nullptr);
    DEFER(BIO_free(bio));

    checkOpenSSL(BIO_write(bio, certs->caCert, strlen(certs->caCert)) <= 0);
    (void) BIO_seek(bio, 0);

    cert = X509_new();
    DEFER(X509_free(cert));
    cert = PEM_read_bio_X509(bio, &cert, nullptr, nullptr);
    checkOpenSSL(cert == nullptr);

    int ret = X509_STORE_add_cert(cert_store, cert);
    checkOpenSSL(ret != 1);

    return cat_true;
}

TEST(cat_ssl, load_certs)
{
    cat_socket_t *clientSocket, *serverSocket;

    auto serverKeyCert = x509->newCert(
        CertFlagsServer,
        x509->newRSAKey(nullptr),
        nullptr,
        "localhost",
        0,
        90 * 24 * 60 * 60,
        "127.0.0.1"
    );

    DEFER([serverKeyCert]{
        free((void *)serverKeyCert.cert);
        free((void *)serverKeyCert.key);
    }());

    // auto f = fopen("/tmp/ca.crt", "w");
    // ASSERT_NE(f, nullptr);
    // DEFER(fclose(f));
    // ASSERT_GT(fwrite(x509->caCert, 1, strlen(x509->caCert), f), 0);
    // fflush(f);
    // auto f2 = fopen("/tmp/svr.key", "w");
    // ASSERT_NE(f2, nullptr);
    // DEFER(fclose(f2));
    // ASSERT_GT(fwrite(serverKeyCert.key, 1, strlen(serverKeyCert.key), f2), 0);
    // fflush(f2);
    // auto f3 = fopen("/tmp/svr.crt", "w");
    // ASSERT_NE(f3, nullptr);
    // DEFER(fclose(f3));
    // ASSERT_GT(fwrite(serverKeyCert.cert, 1, strlen(serverKeyCert.cert), f3), 0);
    // ASSERT_GT(fwrite("\n", 1, 1, f3), 0);
    // ASSERT_GT(fwrite(x509->caCert, 1, strlen(x509->caCert), f3), 0);
    // fflush(f3);

    test_load_cert_t certs = {
        .caCert = x509->caCert,
        .caKey = x509->caKey,
        .severCert = serverKeyCert.cert,
        .severKey = serverKeyCert.key,
        .clientCert = nullptr,
        .clientKey = nullptr
    };

    serverSocket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
    ASSERT_NE(serverSocket, nullptr);
    DEFER(cat_socket_close(serverSocket));
    ASSERT_TRUE(cat_socket_bind_to(serverSocket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(serverSocket, 1));

    cat_socket_t *connSocket = cat_socket_create(nullptr, cat_socket_get_simple_type(serverSocket));
    co([serverSocket, connSocket, certs] {
        ASSERT_TRUE(cat_socket_accept(serverSocket, connSocket));
        DEFER(cat_socket_close(connSocket));
        cat_socket_crypto_options_t options;
        cat_socket_crypto_options_init(&options, false);
        options.context = (void *)&certs;
        options.load_certficate = load_cert_test_callback;
        ASSERT_TRUE(cat_socket_enable_crypto(connSocket, &options));
        char buffer[TEST_BUFFER_SIZE_STD];
        ASSERT_EQ(cat_socket_recv(connSocket, CAT_STRS(buffer)), 6);
    });

    // cat_time_sleep(5);

    clientSocket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
    ASSERT_NE(clientSocket, nullptr);
    DEFER(cat_socket_close(clientSocket));
    ASSERT_TRUE(
        cat_socket_connect_to(clientSocket, CAT_STRL(TEST_LISTEN_IPV4), cat_socket_get_port(serverSocket, false)));
    // ASSERT_TRUE(cat_socket_connect_to(clientSocket, CAT_STRL(TEST_LISTEN_IPV4), 1443));
    cat_socket_crypto_options_t options;
    cat_socket_crypto_options_init(&options, true);
    options.ca_file = "."; // must fail
    options.context = (void *)&certs;
    options.load_ca = load_ca_test_callback;
    options.peer_name = "localhost";
    ASSERT_TRUE(cat_socket_enable_crypto(clientSocket, &options));
    ASSERT_TRUE(cat_socket_send(clientSocket, "hello!", 6));
}

#endif
