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
    // printf("load_ca_test_callback\n");
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

    X509KeyCertPairConfig serverConfig = {
        {"issuer", publicCAPair},
        {"keyType", "RSA2048"},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "localhost"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature"},
        {"extKeyUsage", "critical,serverAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:localhost,IP:127.0.0.1"},
    };
    auto serverPair = X509KeyCertPair::create(serverConfig);

    test_load_cert_t certs = {
        .caCert = strdup(publicCAPair->certPEMString().c_str()),
        .caKey = strdup(publicCAPair->privKeyPEMString().c_str()),
        .severCert = strdup(serverPair->certPEMString().c_str()),
        .severKey = strdup(serverPair->privKeyPEMString().c_str()),
        .clientCert = nullptr,
        .clientKey = nullptr
    };
    DEFER([certs] {
        free((void *)certs.caCert);
        free((void *)certs.caKey);
        free((void *)certs.severCert);
        free((void *)certs.severKey);
    }());

    serverSocket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
    ASSERT_NE(serverSocket, nullptr);
    DEFER(cat_socket_close(serverSocket));
    ASSERT_TRUE(cat_socket_bind_to(serverSocket, CAT_STRL(TEST_LISTEN_IPV4), 0));
    ASSERT_TRUE(cat_socket_listen(serverSocket, TEST_SERVER_BACKLOG));

    cat_socket_t *connSocket = cat_socket_create(nullptr, cat_socket_get_simple_type(serverSocket));
    co([serverSocket, connSocket, certs] {
        ASSERT_TRUE(cat_socket_accept(serverSocket, connSocket));
        DEFER(cat_socket_close(connSocket));
        cat_socket_crypto_options_t options;
        cat_socket_crypto_options_init(&options, false);
        options.context = (void *)&certs;
        options.load_certificate = load_cert_test_callback;
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

TEST(cat_ssl, x509utils)
{
    X509KeyCertPairConfig caConfig = {
        {"keyType", "RSA4096"},
        {"C", "CN"},
        {"O", "TestCA"},
        {"CN", "TestCA"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 365 * 86400 /* 1 year */},
        {"keyUsage", "critical,digitalSignature,keyEncipherment,keyAgreement"},
        {"extKeyUsage", "critical,serverAuth,clientAuth"},
        {"basicConstraints", "critical,CA:TRUE"},
    };
    auto caPair = X509KeyCertPair::create(caConfig);
    ASSERT_NE(caPair, nullptr);
    ASSERT_GT(caPair->privKeyPEMString().length(), 0);
    ASSERT_GT(caPair->privKeyPEMString("123456").length(), 0);
    ASSERT_GT(caPair->certPEMString().length(), 0);
    auto caPEMsPath = caPair->exportPEMs();
    ASSERT_NE(caPEMsPath.key, nullptr);
    ASSERT_NE(caPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(caPEMsPath.key));
    ASSERT_TRUE(file_exists(caPEMsPath.cert));
    ASSERT_THROW(caPair->exportPEMs("123456"), std::runtime_error);

    X509KeyCertPairConfig serverConfig = {
        {"issuer", std::shared_ptr<X509KeyCertPair>(caPair)},
        {"keyType", "RSA2048"},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "localhost"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature,dataEncipherment"},
        {"extKeyUsage", "critical,serverAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:localhost,IP:127.0.0.1"},
    };
    auto serverPair = X509KeyCertPair::create(serverConfig);
    ASSERT_NE(serverPair, nullptr);
    ASSERT_GT(serverPair->privKeyPEMString().length(), 0);
    ASSERT_GT(serverPair->privKeyPEMString("123456").length(), 0);
    ASSERT_GT(serverPair->certPEMString().length(), 0);
    auto serverPEMsPath = serverPair->exportPEMs();
    ASSERT_NE(serverPEMsPath.key, nullptr);
    ASSERT_NE(serverPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(serverPEMsPath.key));
    ASSERT_TRUE(file_exists(serverPEMsPath.cert));

    X509KeyCertPairConfig clientConfig = {
        {"issuer", std::shared_ptr<X509KeyCertPair>(caPair)},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "client"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature,dataEncipherment"},
        {"extKeyUsage", "critical,clientAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:localhost,IP:127.0.0.2"},
    };
    if (OBJ_txt2nid("SM2") != NID_undef) {
        clientConfig["keyType"] = "ECSM2";
    } else if (OBJ_txt2nid("secp384r1") != NID_undef) {
        clientConfig["keyType"] = "ECsecp384r1";
    } else {
        clientConfig["keyType"] = "RSA2048";
    }
    auto clientPair = X509KeyCertPair::create(clientConfig);
    ASSERT_NE(clientPair, nullptr);
    ASSERT_GT(clientPair->privKeyPEMString().length(), 0);
    ASSERT_GT(clientPair->privKeyPEMString("123456").length(), 0);
    ASSERT_GT(clientPair->certPEMString().length(), 0);
    auto clientPEMsPath = clientPair->exportPEMs();
    std::string clientKeyPath = clientPEMsPath.key;
    std::string clientCertPath = clientPEMsPath.cert;
    ASSERT_TRUE(file_exists(clientPEMsPath.key));
    ASSERT_TRUE(file_exists(clientPEMsPath.cert));
    clientPair.reset();
    // check if clientKeyPath released
    ASSERT_FALSE(file_exists(clientKeyPath.c_str()));
    ASSERT_FALSE(file_exists(clientCertPath.c_str()));

    auto myPrivKey = EVP_RSA_gen(3072);
    X509KeyCertPairConfig myConfig = {
        {"issuer", std::shared_ptr<X509KeyCertPair>(caPair)},
        {"pkey", myPrivKey},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "client2"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature,dataEncipherment"},
        {"extKeyUsage", "critical,clientAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:client2,IP:127.0.0.3"},
    };
    auto myPair = X509KeyCertPair::create(myConfig);
    ASSERT_NE(myPair, nullptr);
    ASSERT_GT(myPair->privKeyPEMString().length(), 0);
    ASSERT_GT(myPair->privKeyPEMString("123456").length(), 0);
    ASSERT_GT(myPair->certPEMString().length(), 0);
    auto myPEMsPath = myPair->exportPEMs();
    ASSERT_NE(myPEMsPath.key, nullptr);
    ASSERT_NE(myPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(myPEMsPath.key));
    ASSERT_TRUE(file_exists(myPEMsPath.cert));
    ASSERT_THROW(myPair->exportPEMs("123456"), std::runtime_error);
}

TEST(cat_ssl, enable_crypto)
{
    auto publicCAPath = publicCAPair->exportPEMs();

    X509KeyCertPairConfig caConfig = {
        {"keyType", "RSA4096"},
        {"C", "CN"},
        {"O", "TestCA"},
        {"CN", "TestCA"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 365 * 86400 /* 1 year */},
        {"keyUsage", "critical,digitalSignature,keyCertSign"},
        {"basicConstraints", "critical,CA:TRUE"},
    };
    auto anotherCAPair = X509KeyCertPair::create(caConfig);
    ASSERT_NE(anotherCAPair, nullptr);
    auto anotherPEMsPath = anotherCAPair->exportPEMs();
    ASSERT_NE(anotherPEMsPath.key, nullptr);
    ASSERT_NE(anotherPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(anotherPEMsPath.key));
    ASSERT_TRUE(file_exists(anotherPEMsPath.cert));

    X509KeyCertPairConfig serverConfig = {
        {"issuer", publicCAPair},
        {"keyType", "RSA2048"},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "localhost"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature,dataEncipherment"},
        {"extKeyUsage", "critical,serverAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:localhost,IP:127.0.0.1"},
    };
    auto serverPair = X509KeyCertPair::create(serverConfig);
    ASSERT_NE(serverPair, nullptr);
    auto serverPEMsPath = serverPair->exportPEMs();
    ASSERT_NE(serverPEMsPath.key, nullptr);
    ASSERT_NE(serverPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(serverPEMsPath.key));
    ASSERT_TRUE(file_exists(serverPEMsPath.cert));
    // get cert fingerprints
    unsigned char server_md5_fingerprint[16];
    unsigned char server_md5_fingerprint_bad[16];
    unsigned char server_sha1_fingerprint[20];
    unsigned char server_sha1_fingerprint_bad[20];
    unsigned char server_sha256_fingerprint[32];
    unsigned char server_sha256_fingerprint_bad[32];
    unsigned int len;
    len = 16;
    serverPair->getFingerprint(server_md5_fingerprint, &len, "md5");
    ASSERT_EQ(len, 16);
    len = 20;
    serverPair->getFingerprint(server_sha1_fingerprint, &len, "sha1");
    ASSERT_EQ(len, 20);
    len = 32;
    serverPair->getFingerprint(server_sha256_fingerprint, &len, "sha256");
    ASSERT_EQ(len, 32);
    memcpy(server_md5_fingerprint_bad, server_md5_fingerprint, 16);
    server_md5_fingerprint_bad[0] = ~server_md5_fingerprint_bad[0];
    memcpy(server_sha1_fingerprint_bad, server_sha1_fingerprint, 20);
    server_sha1_fingerprint_bad[0] = ~server_sha1_fingerprint_bad[0];
    memcpy(server_sha256_fingerprint_bad, server_sha256_fingerprint, 32);
    server_sha256_fingerprint_bad[0] = ~server_sha256_fingerprint_bad[0];

    X509KeyCertPairConfig clientConfig = {
        {"issuer", publicCAPair},
        {"keyType", "RSA2048"},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "client"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature,dataEncipherment"},
        {"extKeyUsage", "critical,clientAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:client,IP:127.0.0.1"},
    };
    auto clientPair = X509KeyCertPair::create(clientConfig);
    ASSERT_NE(clientPair, nullptr);
    auto clientPEMsPath = clientPair->exportPEMs();
    ASSERT_NE(clientPEMsPath.key, nullptr);
    ASSERT_NE(clientPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(clientPEMsPath.key));
    ASSERT_TRUE(file_exists(clientPEMsPath.cert));
    // get cert fingerprints
    unsigned char client_sha1_fingerprint[20];
    unsigned char client_sha1_fingerprint_bad[20];
    len = 20;
    clientPair->getFingerprint(client_sha1_fingerprint, &len, "sha1");
    ASSERT_EQ(len, 20);
    memcpy(client_sha1_fingerprint_bad, client_sha1_fingerprint, 20);
    client_sha1_fingerprint_bad[0] = ~client_sha1_fingerprint_bad[0];

    X509KeyCertPairConfig anotherClientConfig = {
        {"issuer", anotherCAPair},
        {"keyType", "RSA2048"},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "client"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature,dataEncipherment"},
        {"extKeyUsage", "critical,clientAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:client,IP:127.0.0.1"},
    };
    auto anotherClientPair = X509KeyCertPair::create(anotherClientConfig);
    ASSERT_NE(anotherClientPair, nullptr);
    auto anotherClientPEMsPath = anotherClientPair->exportPEMs();
    ASSERT_NE(anotherClientPEMsPath.key, nullptr);
    ASSERT_NE(anotherClientPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(anotherClientPEMsPath.key));
    ASSERT_TRUE(file_exists(anotherClientPEMsPath.cert));

    X509KeyCertPairConfig selfsignedConfig = {
        {"keyType", "RSA2048"},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "localhost"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature,dataEncipherment"},
        {"extKeyUsage", "critical,serverAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:localhost,IP:127.0.0.1"},
    };
    auto selfsignedPair = X509KeyCertPair::create(selfsignedConfig);
    ASSERT_NE(selfsignedPair, nullptr);
    auto selfsignedPEMsPath = selfsignedPair->exportPEMs();
    ASSERT_NE(selfsignedPEMsPath.key, nullptr);
    ASSERT_NE(selfsignedPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(selfsignedPEMsPath.key));
    ASSERT_TRUE(file_exists(selfsignedPEMsPath.cert));

    // default verify depth is 9
    std::vector<std::shared_ptr<X509KeyCertPair>> intermediatePairs;
    std::shared_ptr<X509KeyCertPair> issuerPair = publicCAPair;
    for (int i = 0; i < 12; i++) {
        X509KeyCertPairConfig intermediateConfig = {
            {"issuer", issuerPair},
            {"keyType", "RSA2048"},
            {"C", "CN"},
            {"O", "Test"},
            {"CN", string_format("intermediate %d", i + 1)},
            {"notBeforeOffsetSeconds", 0},
            {"notAfterOffsetSeconds", 90 * 86400 /* 90 days */},
            {"keyUsage", "critical,digitalSignature,keyCertSign"},
            {"basicConstraints", "critical,CA:TRUE"},
        };
        auto intermediatePair = X509KeyCertPair::create(intermediateConfig);
        ASSERT_NE(intermediatePair, nullptr);
        auto intermediatePEMsPath = intermediatePair->exportPEMs();
        ASSERT_NE(intermediatePEMsPath.key, nullptr);
        ASSERT_NE(intermediatePEMsPath.cert, nullptr);
        ASSERT_TRUE(file_exists(intermediatePEMsPath.key));
        ASSERT_TRUE(file_exists(intermediatePEMsPath.cert));
        issuerPair = intermediatePair;
        intermediatePairs.push_back(intermediatePair);
    }

    X509KeyCertPairConfig verydeepConfig = {
        {"issuer", intermediatePairs.back()},
        {"keyType", "RSA2048"},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "localhost"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature,dataEncipherment"},
        {"extKeyUsage", "critical,serverAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:localhost,IP:127.0.0.1"},
    };
    auto verydeepPair = X509KeyCertPair::create(verydeepConfig);
    ASSERT_NE(verydeepPair, nullptr);
    auto verydeepPEMsPath = verydeepPair->exportPEMs();
    ASSERT_NE(verydeepPEMsPath.key, nullptr);
    ASSERT_NE(verydeepPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(verydeepPEMsPath.key));
    ASSERT_TRUE(file_exists(verydeepPEMsPath.cert));

    // append intermediates into cert for chained
    FILE *fp = fopen(verydeepPEMsPath.cert, "a"
#ifdef CAT_OS_WINDOWS
        "b"
#endif
    );
    for (auto &intermediatePair : intermediatePairs) {
        std::string intermediateCert = intermediatePair->certPEMString();
        fwrite(intermediateCert.c_str(), intermediateCert.length(), 1, fp);
    }
    fclose(fp);

    typedef std::unordered_map<std::string, std::variant<
        const char*,
        int
    >> options_map_t;
    std::unordered_map<std::string, std::variant<
        std::string,
        bool,
        options_map_t
    >> test_cases[] = {
        {
            {"name", "1. server no options, client no options"},
            {"expectSuccess", false},
        },
        {
            {"name", "2a. server use valid certs, client have no ca config"},
            {"expectSuccess", false},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"verify_peer_name", false},
            }},
        },
        {
            {"name", "2b. server use valid certs, client do not accept ca"},
            {"expectSuccess", false},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", anotherPEMsPath.cert},
                {"verify_peer", true},
                {"verify_peer_name", false},
            }},
        },
        {
            {"name", "2c. server use valid certs, client accepts ca"},
            {"expectSuccess", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"verify_peer", true},
                {"verify_peer_name", false},
            }},
        },
        {
            {"name", "3a. server use valid certs, client accepts ca, bad peer name"},
            {"expectSuccess", false},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "notlocalhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
        {
            {"name", "3b. server use valid certs, client accepts ca, bad peer name, no check peer name"},
            {"expectSuccess", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "notlocalhost"},
                {"verify_peer", true},
                {"verify_peer_name", false},
            }},
        },
        {
            {"name", "4a. server checks peer, but client have no cert"},
            {"expectSuccess", false},
            {"clientSuccessQuirk", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", true},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
        {
            {"name", "4b. server checks peer, client have cert, but peer name not match"},
            {"expectSuccess", false},
            {"clientSuccessQuirk", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"peer_name", "notclient"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", clientPEMsPath.cert},
                {"certificate_key", clientPEMsPath.key},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
        {
            {"name", "4c. server checks peer, client have cert"},
            {"expectSuccess", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", true},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", clientPEMsPath.cert},
                {"certificate_key", clientPEMsPath.key},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
        {
            {"name", "4d. server checks peer, client have bad cert"},
            {"expectSuccess", false},
            {"clientSuccessQuirk", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", true},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", anotherClientPEMsPath.cert},
                {"certificate_key", anotherClientPEMsPath.key},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
        {
            {"name", "5a. server use selfsigned cert"},
            {"expectSuccess", false},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", selfsignedPEMsPath.cert},
                {"certificate_key", selfsignedPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
        {
            {"name", "5b. server use selfsigned cert, but client accept selfsigned"},
            {"expectSuccess", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", selfsignedPEMsPath.cert},
                {"certificate_key", selfsignedPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
                {"allow_self_signed", true},
            }},
        },
        {
            {"name", "6a. server use very deep certificate chain"},
            {"expectSuccess", false},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", verydeepPEMsPath.cert},
                {"certificate_key", verydeepPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
        {
            {"name", "6b. server use very deep certificate chain, but client accept"},
            {"expectSuccess", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", verydeepPEMsPath.cert},
                {"certificate_key", verydeepPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
                {"verify_depth", 99},
            }},
        },
        {
            {"name", "7a. client check with md5 fingerprint, matched"},
            {"expectSuccess", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
                {"peer_md5_fingerprint", (const char *)server_md5_fingerprint},
            }},
        },
        {
            {"name", "7b. client check with md5 fingerprint, not match"},
            {"expectSuccess", false},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
                {"peer_md5_fingerprint", (const char *)server_md5_fingerprint_bad},
            }},
        },
        {
            {"name", "7c. client check with fingerprint, md5 match sha1 not match"},
            {"expectSuccess", false},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
                {"peer_md5_fingerprint", (const char *)server_md5_fingerprint},
                {"peer_sha1_fingerprint", (const char *)server_sha1_fingerprint_bad},
            }},
        },
        {
            {"name", "7d. client check with fingerprint, all set and match"},
            {"expectSuccess", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
                {"peer_md5_fingerprint", (const char *)server_md5_fingerprint},
                {"peer_sha1_fingerprint", (const char *)server_sha1_fingerprint},
                {"peer_sha256_fingerprint", (const char *)server_sha256_fingerprint},
            }},
        },
        {
            {"name", "7e. client check with fingerprint, sha256 not match"},
            {"expectSuccess", false},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", false},
                {"verify_peer_name", false},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
                {"peer_sha256_fingerprint", (const char *)server_sha256_fingerprint_bad},
            }},
        },
        {
            {"name", "7f. server check with fingerprint, client donot provide certificate"},
            {"expectSuccess", false},
            {"clientSuccessQuirk", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", true},
                {"verify_peer_name", false},
                {"peer_sha1_fingerprint", (const char *)client_sha1_fingerprint},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
        {
            {"name", "7g. server check with fingerprint, not match"},
            {"expectSuccess", false},
            {"clientSuccessQuirk", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", true},
                {"verify_peer_name", false},
                {"peer_sha1_fingerprint", (const char *)client_sha1_fingerprint_bad},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", clientPEMsPath.cert},
                {"certificate_key", clientPEMsPath.key},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
        {
            {"name", "7h. server check with fingerprint, matched"},
            {"expectSuccess", true},
            {"serverOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", serverPEMsPath.cert},
                {"certificate_key", serverPEMsPath.key},
                {"verify_peer", true},
                {"verify_peer_name", false},
                {"peer_sha1_fingerprint", (const char *)client_sha1_fingerprint},
            }},
            {"clientOptions", options_map_t{
                {"ca_file", publicCAPath.cert},
                {"certificate", clientPEMsPath.cert},
                {"certificate_key", clientPEMsPath.key},
                {"peer_name", "localhost"},
                {"verify_peer", true},
                {"verify_peer_name", true},
            }},
        },
    };

    auto assign_options = [](cat_socket_crypto_options_t *options, const options_map_t &options_map) {
        for (auto &option : options_map) {
            // std::cout << option.first << ": " << option.second.index() << std::endl;
            if (option.first == "ca_file") {
                options->ca_file = std::get<const char*>(option.second);
            } else if (option.first == "certificate") {
                options->certificate = std::get<const char*>(option.second);
            } else if (option.first == "certificate_key") {
                options->certificate_key = std::get<const char*>(option.second);
            } else if (option.first == "verify_peer") {
                options->verify_peer = (cat_bool_t)std::get<int>(option.second);
            } else if (option.first == "verify_peer_name") {
                options->verify_peer_name = (cat_bool_t)std::get<int>(option.second);
            } else if (option.first == "allow_self_signed") {
                options->allow_self_signed = (cat_bool_t)std::get<int>(option.second);
            } else if (option.first == "peer_name") {
                options->peer_name = std::get<const char*>(option.second);
            } else if (option.first == "peer_md5_fingerprint") {
                options->verify_peer_md5_fingerprint = cat_true;
                const char *fingerprint = std::get<const char*>(option.second);
                memcpy(options->peer_md5_fingerprint, fingerprint, 16);
            } else if (option.first == "peer_sha1_fingerprint") {
                options->verify_peer_sha1_fingerprint = cat_true;
                const char *fingerprint = std::get<const char*>(option.second);
                memcpy(options->peer_sha1_fingerprint, fingerprint, 20);
            } else if (option.first == "peer_sha256_fingerprint") {
                options->verify_peer_sha256_fingerprint = cat_true;
                const char *fingerprint = std::get<const char*>(option.second);
                memcpy(options->peer_sha256_fingerprint, fingerprint, 32);
            } else if (option.first == "verify_depth") {
                options->verify_depth = std::get<int>(option.second);
            } else {
                throw std::runtime_error("Invalid option: " + option.first);
            }
        }
    };

    for (auto &test_case : test_cases) {
        for (auto &server_send_first : {false, true}) {
            // fprintf(stderr, "test_case: %s\n", std::get<std::string>(test_case["name"]).c_str());
            cat_socket_t *serverSocket;
            serverSocket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
            ASSERT_NE(serverSocket, nullptr);
            DEFER(cat_socket_close(serverSocket));
            ASSERT_TRUE(cat_socket_bind_to(serverSocket, CAT_STRL(TEST_LISTEN_IPV4), 0));
            ASSERT_TRUE(cat_socket_listen(serverSocket, TEST_SERVER_BACKLOG));
            unsigned short port = cat_socket_get_port(serverSocket, false);

            auto read_assert = [](cat_socket_t *socket, const char *expected, size_t expected_size) {
                // 16 for protect
                ssize_t nread = 0, n;
                char *buffer = (char *)calloc(expected_size + 16, 1);
                memcpy(buffer + expected_size, "thisisprotected!", 16);
                while (nread < expected_size) {
                    n = cat_socket_recv(socket, buffer + nread, 16 - nread);
                    // assert read encrypted bytes successfully
                    ASSERT_GT(n, 0);
                    // assert the read data is not corrupted (avoid read buffers problem)
                    ASSERT_LE(n + nread, expected_size);
                    nread += n;
                }
                ASSERT_EQ(nread, expected_size);
                ASSERT_EQ(std::string(buffer, nread), expected);
                ASSERT_EQ(std::string(buffer + nread, 16), "thisisprotected!");
                free(buffer);
            };

            cat_socket_t *connSocket = cat_socket_create(nullptr, cat_socket_get_simple_type(serverSocket));
            ASSERT_NE(connSocket, nullptr);
            DEFER(cat_socket_close(connSocket));
            wait_group wg;
            co([&test_case, serverSocket, &connSocket, assign_options, &wg, server_send_first, read_assert]{
                wg++;
                DEFER(wg--);

                ASSERT_TRUE(cat_socket_accept(serverSocket, connSocket));

                cat_socket_crypto_options_t serverOptions, *options = nullptr;
                cat_socket_crypto_options_init(&serverOptions, false);
                if (test_case.find("serverOptions") != test_case.end()) {
                    options = &serverOptions;
                    assign_options(options, std::get<options_map_t>(test_case["serverOptions"]));
                }
                cat_bool_t success = cat_socket_enable_crypto(connSocket, options);
                if (std::get<bool>(test_case["expectSuccess"])) {
                    ASSERT_TRUE(success);
                    if (server_send_first) {
                        ASSERT_EQ(cat_socket_send(connSocket, "serverHello", 11), cat_true);
                        read_assert(connSocket, "clientHello", 11);
                    } else {
                        read_assert(connSocket, "clientHello", 11);
                        ASSERT_EQ(cat_socket_send(connSocket, "serverHello", 11), cat_true);
                    }
                } else {
                    ASSERT_FALSE(success);
                }
            });

            // scope for clientSocket
            {
                cat_socket_t *clientSocket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
                ASSERT_NE(clientSocket, nullptr);
                DEFER(cat_socket_close(clientSocket));
                ASSERT_TRUE(cat_socket_connect_to(clientSocket, CAT_STRL(TEST_LISTEN_IPV4), port));
            
                cat_socket_crypto_options_t clientOptions, *options = nullptr;
                cat_socket_crypto_options_init(&clientOptions, true);
                if (test_case.find("clientOptions") != test_case.end()) {
                    options = &clientOptions;
                    assign_options(options, std::get<options_map_t>(test_case["clientOptions"]));
                }
                cat_bool_t success = cat_socket_enable_crypto(clientSocket, options);
                if (
                    test_case.find("clientSuccessQuirk") != test_case.end() &&
                    std::get<bool>(test_case["clientSuccessQuirk"])
                ) {
                    ASSERT_TRUE(success);
                    // openssl quirk here: cat_socket_send() should fail, but it may return success
                    // ASSERT_FALSE(cat_socket_send(clientSocket, "clientHello", 11));
                    char buffer[16];
                    ASSERT_EQ(cat_socket_recv(clientSocket, CAT_STRS(buffer)), 0);
                } else if (std::get<bool>(test_case["expectSuccess"])) {
                    ASSERT_TRUE(success);
                    if (server_send_first) {
                        read_assert(clientSocket, "serverHello", 11);
                        ASSERT_EQ(cat_socket_send(clientSocket, "clientHello", 11), cat_true);
                    } else {
                        ASSERT_EQ(cat_socket_send(clientSocket, "clientHello", 11), cat_true);
                        read_assert(clientSocket, "serverHello", 11);
                    }
                } else {
                    ASSERT_FALSE(success);
                }
            }
            wg();
        }
    }

}

TEST(cat_ssl, truncate_256k)
{
    auto publicCAPath = publicCAPair->exportPEMs();
    X509KeyCertPairConfig serverConfig = {
        {"issuer", publicCAPair},
        {"keyType", "RSA2048"},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", "localhost"},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 30 * 86400 /* 30 days */},
        {"keyUsage", "critical,digitalSignature,dataEncipherment"},
        {"extKeyUsage", "critical,serverAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
        {"subjectAltName", "DNS:localhost,IP:127.0.0.1"},
    };
    auto serverPair = X509KeyCertPair::create(serverConfig);
    ASSERT_NE(serverPair, nullptr);
    auto serverPEMsPath = serverPair->exportPEMs();
    ASSERT_NE(serverPEMsPath.key, nullptr);
    ASSERT_NE(serverPEMsPath.cert, nullptr);
    ASSERT_TRUE(file_exists(serverPEMsPath.key));
    ASSERT_TRUE(file_exists(serverPEMsPath.cert));

    for (auto tailLength : {1, 2048, 8192, 32768}) {
        for (auto &server_send : {true, false}) {
            cat_socket_t *serverSocket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
            ASSERT_NE(serverSocket, nullptr);
            DEFER(cat_socket_close(serverSocket));
            ASSERT_TRUE(cat_socket_bind_to(serverSocket, CAT_STRL(TEST_LISTEN_IPV4), 0));
            ASSERT_TRUE(cat_socket_listen(serverSocket, TEST_SERVER_BACKLOG));
            unsigned short port = cat_socket_get_port(serverSocket, false);
        
            wait_group wg;
            cat_socket_t *connSocket = cat_socket_create(nullptr, cat_socket_get_simple_type(serverSocket));
            ASSERT_NE(connSocket, nullptr);
            DEFER(cat_socket_close(connSocket));
            co([&serverSocket, &connSocket, &serverPEMsPath, &publicCAPath, &wg, server_send, tailLength]{
                wg++;
                DEFER(wg--);
                ASSERT_TRUE(cat_socket_accept(serverSocket, connSocket));
                cat_socket_crypto_options_t options;
                cat_socket_crypto_options_init(&options, false);
                options.ca_file = publicCAPath.cert;
                options.certificate = serverPEMsPath.cert;
                options.certificate_key = serverPEMsPath.key;
                options.verify_peer = false;
                ASSERT_TRUE(cat_socket_enable_crypto(connSocket, &options));

                char *buffer = (char *)calloc(256 * 1024 + tailLength, 1);
                DEFER(free(buffer));
                memset(buffer, 'a', 256 * 1024);
                memset(buffer + (256 * 1024), 'b', tailLength);

                if (server_send) {
                    ASSERT_EQ(cat_socket_send(connSocket, buffer, 256 * 1024 + tailLength), cat_true);
                } else {
                    ASSERT_EQ(cat_socket_read(connSocket, buffer, 256 * 1024 + tailLength), 256 * 1024 + tailLength);
                    for (size_t offset = 0; offset < 256 * 1024; offset++) {
                        ASSERT_EQ(buffer[offset], 'a');
                    }
                    for (size_t offset = 0; offset < tailLength; offset++) {
                        ASSERT_EQ(buffer[256 * 1024 + offset], 'b');
                    }
                }
            });

            cat_socket_t *clientSocket = cat_socket_create(nullptr, CAT_SOCKET_TYPE_TCP);
            ASSERT_NE(clientSocket, nullptr);
            DEFER(cat_socket_close(clientSocket));
            ASSERT_TRUE(cat_socket_connect_to(clientSocket, CAT_STRL(TEST_LISTEN_IPV4), port));

            cat_socket_crypto_options_t options;
            cat_socket_crypto_options_init(&options, true);
            options.ca_file = publicCAPath.cert;
            options.verify_peer = true;
            options.peer_name = "localhost";
            options.verify_peer_name = true;
            ASSERT_TRUE(cat_socket_enable_crypto(clientSocket, &options));
            
            char *buffer = (char *)calloc(256 * 1024 + tailLength, 1);
            DEFER(free(buffer));
            memset(buffer, 'a', 256 * 1024);
            memset(buffer + (256 * 1024), 'b', tailLength);

            if (server_send) {
                ASSERT_EQ(cat_socket_read(clientSocket, buffer, 256 * 1024 + tailLength), 256 * 1024 + tailLength);
                for (size_t offset = 0; offset < 256 * 1024; offset++) {
                    ASSERT_EQ(buffer[offset], 'a');
                }
                for (size_t offset = 0; offset < tailLength; offset++) {
                    ASSERT_EQ(buffer[256 * 1024 + offset], 'b');
                }
            } else {
                ASSERT_EQ(cat_socket_send(clientSocket, buffer, 256 * 1024 + tailLength), cat_true);
            }
            wg();
        }
    }
}

#endif
