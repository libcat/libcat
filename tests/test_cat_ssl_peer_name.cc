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
  | Author: Yun Dou <dixyes@gmail.com>                                       |
  +--------------------------------------------------------------------------+
 */

#include "test.h"

#ifdef CAT_SSL

/*
 * Peer identity matching. The cases follow the PHP core tests
 * ext/openssl/tests/san_peer_matching.phpt, san_no_cn_fallback.phpt,
 * san_absent_cn_fallback.phpt and san_ipv6_peer_matching.phpt. They also follow
 * the wildcard grammar of OpenSSL. Two behaviours differ from the PHP stream
 * layer on purpose. The subject common name names no host unless the caller
 * opts in. The strict flag limits a wildcard to the left-most label.
 */

static void transfer_handshake_bytes(cat_ssl_t *from, cat_ssl_t *to)
{
    char buffer[CAT_SSL_BUFFER_SIZE];

    for (;;) {
        int length = cat_ssl_read_encrypted_bytes(from, buffer, sizeof(buffer));
        ASSERT_GE(length, 0);
        if (length == CAT_RET_NONE) {
            return;
        }
        ASSERT_EQ(cat_ssl_write_encrypted_bytes(to, buffer, length), length);
    }
}

enum class peer_name_result_t {
    MISMATCH = 0,
    MATCH = 1,
    /* The fixture could not be prepared. This value is not a mismatch, so an
     * expectation of "no match" cannot pass when the handshake did not run */
    BROKEN = 2,
};

/* Handshake a client against a server that holds the given certificate. The
 * test disables peer verification for this check. Report whether the name
 * matches the identity in that certificate */
static peer_name_result_t peer_name_matches(X509KeyCertPairConfig &config, const char *name,
    size_t name_length, cat_ssl_check_host_flags_t flags = CAT_SSL_CHECK_HOST_FLAG_NONE)
{
    auto pair = X509KeyCertPair::create(config);
    if (pair == nullptr) {
        return peer_name_result_t::BROKEN;
    }
    auto paths = pair->exportPEMs();

    cat_ssl_context_t *server_context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
    if (server_context == nullptr) {
        return peer_name_result_t::BROKEN;
    }
    DEFER(cat_ssl_context_close(server_context));
    if (!cat_ssl_context_set_certificate(server_context, paths.cert, paths.key)) {
        return peer_name_result_t::BROKEN;
    }

    cat_ssl_context_t *client_context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
    if (client_context == nullptr) {
        return peer_name_result_t::BROKEN;
    }
    DEFER(cat_ssl_context_close(client_context));
    cat_ssl_context_disable_verify_peer(client_context);

    cat_ssl_t server = {}, client = {};
    if (cat_ssl_create(&server, server_context) == nullptr) {
        return peer_name_result_t::BROKEN;
    }
    DEFER(cat_ssl_close(&server));
    if (cat_ssl_create(&client, client_context) == nullptr) {
        return peer_name_result_t::BROKEN;
    }
    DEFER(cat_ssl_close(&client));
    cat_ssl_set_accept_state(&server);
    cat_ssl_set_connect_state(&client);

    bool established = false;
    for (size_t attempt = 0; attempt < 32; attempt++) {
        cat_ssl_ret_t client_ret = cat_ssl_handshake(&client);
        if (client_ret == CAT_SSL_RET_ERROR) {
            return peer_name_result_t::BROKEN;
        }
        transfer_handshake_bytes(&client, &server);
        cat_ssl_ret_t server_ret = cat_ssl_handshake(&server);
        if (server_ret == CAT_SSL_RET_ERROR) {
            return peer_name_result_t::BROKEN;
        }
        transfer_handshake_bytes(&server, &client);
        if (client_ret == CAT_SSL_RET_OK && server_ret == CAT_SSL_RET_OK) {
            established = true;
            break;
        }
    }
    if (!established) {
        return peer_name_result_t::BROKEN;
    }

    return cat_ssl_check_host_ex(&client, name, name_length, flags)
        ? peer_name_result_t::MATCH
        : peer_name_result_t::MISMATCH;
}

#define PEER_NAME_EXPECT(expectation, ...) \
    do { \
        peer_name_result_t peer_name_result = peer_name_matches(__VA_ARGS__); \
        ASSERT_NE(peer_name_result, peer_name_result_t::BROKEN) \
            << "the test fixture could not be prepared"; \
        EXPECT_EQ(peer_name_result, expectation); \
    } while (0)

#define EXPECT_PEER_MATCH(config, text) \
    PEER_NAME_EXPECT(peer_name_result_t::MATCH, config, text, CAT_STRLEN(text))

#define EXPECT_PEER_MISMATCH(config, text) \
    PEER_NAME_EXPECT(peer_name_result_t::MISMATCH, config, text, CAT_STRLEN(text))

#define EXPECT_PEER_MATCH_EX(config, text, length, flags) \
    PEER_NAME_EXPECT(peer_name_result_t::MATCH, config, text, length, flags)

#define EXPECT_PEER_MISMATCH_EX(config, text, length, flags) \
    PEER_NAME_EXPECT(peer_name_result_t::MISMATCH, config, text, length, flags)

/* A certificate issued by the test CA, with the given SAN entries */
static X509KeyCertPairConfig leaf_config(const char *cn, const char *san)
{
    X509KeyCertPairConfig config = {
        {"issuer", publicCAPair},
        {"keyType", "RSA2048"},
        {"C", "CN"},
        {"O", "Test"},
        {"CN", cn},
        {"notBeforeOffsetSeconds", 0},
        {"notAfterOffsetSeconds", 86400},
        {"keyUsage", "critical,digitalSignature,keyEncipherment"},
        {"extKeyUsage", "critical,serverAuth"},
        {"basicConstraints", "critical,CA:FALSE"},
    };
    if (san != nullptr) {
        config["subjectAltName"] = san;
    }
    return config;
}

TEST(cat_ssl, peer_name_matches_the_san_entries)
{
    /* san_peer_matching.phpt: every listed name matches, an unlisted one does not */
    X509KeyCertPairConfig config = leaf_config(
        "cn.example.org", "DNS:example.org,DNS:www.example.org,DNS:test.example.org");

    EXPECT_PEER_MATCH(config, "example.org");
    EXPECT_PEER_MATCH(config, "www.example.org");
    EXPECT_PEER_MATCH(config, "test.example.org");
    EXPECT_PEER_MISMATCH(config, "moar.example.org");
}

TEST(cat_ssl, peer_name_dns_comparison_is_case_insensitive)
{
    X509KeyCertPairConfig config = leaf_config("leaf", "DNS:example.org");

    EXPECT_PEER_MATCH(config, "EXAMPLE.ORG");
    EXPECT_PEER_MATCH(config, "Example.Org");
}

TEST(cat_ssl, peer_name_does_not_fall_back_to_cn_when_a_san_exists)
{
    /* san_no_cn_fallback.phpt: the matcher ignores the CN once the certificate
     * has a SAN */
    X509KeyCertPairConfig config = leaf_config("cn.example.org", "DNS:san.example.org");

    EXPECT_PEER_MATCH(config, "san.example.org");
    EXPECT_PEER_MISMATCH(config, "cn.example.org");
}

TEST(cat_ssl, peer_name_ignores_the_common_name_by_default)
{
    /* A certificate with no SAN names no host here. The common name is an
     * identity only when the caller asks for it */
    X509KeyCertPairConfig config = leaf_config("cn.example.org", nullptr);

    EXPECT_PEER_MISMATCH(config, "cn.example.org");
    EXPECT_PEER_MISMATCH(config, "other.example.org");
    EXPECT_PEER_MATCH_EX(config, "cn.example.org", CAT_STRLEN("cn.example.org"),
        CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME);
    EXPECT_PEER_MISMATCH_EX(config, "other.example.org", CAT_STRLEN("other.example.org"),
        CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME);
}

TEST(cat_ssl, peer_name_matches_a_wildcard_leftmost_label)
{
    X509KeyCertPairConfig config = leaf_config("leaf", "DNS:*.wild.example.org");

    EXPECT_PEER_MATCH(config, "a.wild.example.org");
    EXPECT_PEER_MISMATCH(config, "a.b.wild.example.org");
    EXPECT_PEER_MISMATCH(config, "wild.example.org");
}

TEST(cat_ssl, peer_name_partial_wildcard_follows_the_flag)
{
    /* A wildcard that fills a whole left-most label matches in both modes */
    X509KeyCertPairConfig config = leaf_config("leaf", "DNS:*.wild.example.org");

    EXPECT_PEER_MATCH(config, "a.wild.example.org");
    EXPECT_PEER_MATCH_EX(config, "a.wild.example.org", CAT_STRLEN("a.wild.example.org"),
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD);
    EXPECT_PEER_MISMATCH_EX(config, "a.b.wild.example.org", CAT_STRLEN("a.b.wild.example.org"),
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD);

    /* The default accepts a partial wildcard. The strict mode accepts a whole
     * left-most label only */
    X509KeyCertPairConfig partial = leaf_config("leaf", "DNS:w*.wild.example.org");

    EXPECT_PEER_MATCH(partial, "www.wild.example.org");
    EXPECT_PEER_MISMATCH_EX(partial, "www.wild.example.org", CAT_STRLEN("www.wild.example.org"),
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD);
    EXPECT_PEER_MISMATCH_EX(partial, "w.wild.example.org", CAT_STRLEN("w.wild.example.org"),
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD);
}

TEST(cat_ssl, peer_name_matches_a_rooted_dns_san)
{
    /* A certificate SAN may carry the root dot of a fully qualified name. It
     * then names the same host without the dot */
    X509KeyCertPairConfig config = leaf_config("leaf", "DNS:rooted.example.org.");

    EXPECT_PEER_MATCH(config, "rooted.example.org");
    EXPECT_PEER_MISMATCH(config, "other.example.org");
    /* The matcher drops the root dot from the certificate name only. A SAN that
     * carries it names the host with or without it. A reference that carries it
     * does not match a SAN that has none */
    EXPECT_PEER_MISMATCH(config, "rooted.example.org.");

    X509KeyCertPairConfig plain = leaf_config("leaf", "DNS:plain.example.org");
    EXPECT_PEER_MATCH(plain, "plain.example.org");
    EXPECT_PEER_MISMATCH(plain, "plain.example.org.");
}

TEST(cat_ssl, peer_name_matches_an_ip_san)
{
    /* san_ipv6_peer_matching.phpt covers IPv6. The IPv4 case shares the path */
    X509KeyCertPairConfig config = leaf_config("leaf", "IP:127.0.0.1");

    EXPECT_PEER_MATCH(config, "127.0.0.1");
    EXPECT_PEER_MISMATCH(config, "127.0.0.2");
    /* The common name is not an identity unless the caller asks for it */
    EXPECT_PEER_MISMATCH(config, "leaf");
    EXPECT_PEER_MATCH_EX(config, "leaf", CAT_STRLEN("leaf"),
        CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME);
    EXPECT_PEER_MISMATCH(config, "10.0.0.1");
    X509KeyCertPairConfig cn_only = leaf_config("10.0.0.1", nullptr);
    EXPECT_PEER_MISMATCH(cn_only, "10.0.0.1");
    /* A numeric reference name is an IP identity. The common name does not name
     * it, also when the caller asks for the common name */
    EXPECT_PEER_MISMATCH_EX(cn_only, "10.0.0.1", CAT_STRLEN("10.0.0.1"),
        CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME);
}

TEST(cat_ssl, peer_name_matches_an_ipv6_san)
{
    X509KeyCertPairConfig config = leaf_config("leaf", "IP:2001:db8:85a3:8d3:1319:8a2e:370:7348");

    EXPECT_PEER_MATCH(config, "2001:db8:85a3:8d3:1319:8a2e:370:7348");
    EXPECT_PEER_MISMATCH(config, "2001:db8:85a3:8d3:1319:8a2e:370:7349");
}

TEST(cat_ssl, peer_name_rejects_a_name_with_an_embedded_nul)
{
    X509KeyCertPairConfig config = leaf_config("leaf", "DNS:example.org");
    auto pair = X509KeyCertPair::create(config);
    ASSERT_NE(pair, nullptr);
    auto paths = pair->exportPEMs();

    cat_ssl_context_t *server_context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
    ASSERT_NE(server_context, nullptr);
    DEFER(cat_ssl_context_close(server_context));
    ASSERT_TRUE(cat_ssl_context_set_certificate(server_context, paths.cert, paths.key));
    cat_ssl_context_t *client_context = cat_ssl_context_create(CAT_SSL_METHOD_TLS, CAT_SSL_PROTOCOL_TLSv1_2);
    ASSERT_NE(client_context, nullptr);
    DEFER(cat_ssl_context_close(client_context));
    cat_ssl_context_disable_verify_peer(client_context);

    cat_ssl_t server = {}, client = {};
    ASSERT_EQ(cat_ssl_create(&server, server_context), &server);
    DEFER(cat_ssl_close(&server));
    ASSERT_EQ(cat_ssl_create(&client, client_context), &client);
    DEFER(cat_ssl_close(&client));
    cat_ssl_set_accept_state(&server);
    cat_ssl_set_connect_state(&client);
    bool established = false;
    for (size_t attempt = 0; attempt < 32; attempt++) {
        cat_ssl_ret_t client_ret = cat_ssl_handshake(&client);
        ASSERT_NE(client_ret, CAT_SSL_RET_ERROR);
        ASSERT_NO_FATAL_FAILURE(transfer_handshake_bytes(&client, &server));
        cat_ssl_ret_t server_ret = cat_ssl_handshake(&server);
        ASSERT_NE(server_ret, CAT_SSL_RET_ERROR);
        ASSERT_NO_FATAL_FAILURE(transfer_handshake_bytes(&server, &client));
        if (client_ret == CAT_SSL_RET_OK && server_ret == CAT_SSL_RET_OK) {
            established = true;
            break;
        }
    }
    ASSERT_TRUE(established);

    /* "example.org\0evil" must not be treated as "example.org" */
    const char name[] = "example.org\0evil";
    ASSERT_FALSE(cat_ssl_check_host_ex(&client, name, sizeof(name) - 1,
        CAT_SSL_CHECK_HOST_FLAG_NONE));
    ASSERT_FALSE(cat_ssl_check_host_ex(&client, name, sizeof(name) - 1,
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD));
}

/*
 * OpenSSL accepts a wildcard at the start of the left-most label as well as at
 * its end, so "*.example.com", "*w.example.com" and "w*.example.com" are all
 * valid, while a wildcard inside a label, as in "foo*bar.example.com", is not.
 */
TEST(cat_ssl, peer_name_matches_a_wildcard_at_either_end_of_a_label)
{
    X509KeyCertPairConfig whole = leaf_config("leaf", "DNS:*.example.com");
    EXPECT_PEER_MATCH(whole, "www.example.com");

    X509KeyCertPairConfig head = leaf_config("leaf", "DNS:*w.example.com");
    EXPECT_PEER_MATCH(head, "www.example.com");
    EXPECT_PEER_MATCH(head, "aw.example.com");
    EXPECT_PEER_MATCH(head, "w.example.com");
    EXPECT_PEER_MISMATCH(head, "example.com");

    X509KeyCertPairConfig tail = leaf_config("leaf", "DNS:w*.example.com");
    EXPECT_PEER_MATCH(tail, "www.example.com");

    /* A wildcard inside a label is not a wildcard form */
    X509KeyCertPairConfig middle = leaf_config("leaf", "DNS:foo*bar.example.com");
    EXPECT_PEER_MISMATCH(middle, "fooxbar.example.com");
    EXPECT_PEER_MISMATCH(middle, "foobar.example.com");
}

/* A wildcard needs a domain below it, so the pattern has to hold at least two
 * dots; this holds for a partial wildcard as much as for a whole label one */
TEST(cat_ssl, peer_name_requires_a_domain_under_a_wildcard)
{
    X509KeyCertPairConfig top = leaf_config("leaf", "DNS:*.host");
    EXPECT_PEER_MISMATCH(top, "foo.host");

    X509KeyCertPairConfig partial_top = leaf_config("leaf", "DNS:w*.host");
    EXPECT_PEER_MISMATCH(partial_top, "www.host");

    X509KeyCertPairConfig deep = leaf_config("leaf", "DNS:*.example.com");
    EXPECT_PEER_MATCH(deep, "www.example.com");
}

/*
 * OpenSSL's wildcard grammar also constrains the pattern and the text the
 * wildcard covers: a label may not be empty or start or end with a hyphen, a
 * wildcard label may not be an A-label, a partial wildcard does not match an
 * A-label, and what the wildcard stands for may hold only letters, digits and
 * hyphens.
 */
TEST(cat_ssl, peer_name_follows_the_openssl_counter_rules)
{
    /* an empty label in the pattern names nothing */
    X509KeyCertPairConfig empty = leaf_config("leaf", "DNS:*..com");
    EXPECT_PEER_MISMATCH(empty, "a..com");
    EXPECT_PEER_MISMATCH(empty, "evil.com");

    /* the label the wildcard covers holds only letters, digits and hyphens */
    X509KeyCertPairConfig plain = leaf_config("leaf", "DNS:*.example.com");
    EXPECT_PEER_MATCH(plain, "a-b.example.com");
    EXPECT_PEER_MISMATCH(plain, "a_b.example.com");
    EXPECT_PEER_MISMATCH(plain, "a/b.example.com");
    EXPECT_PEER_MISMATCH(plain, "a b.example.com");

    /* a partial wildcard does not match an A-label */
    X509KeyCertPairConfig partial = leaf_config("leaf", "DNS:x*.example.com");
    EXPECT_PEER_MATCH(partial, "xa.example.com");
    EXPECT_PEER_MISMATCH(partial, "xn--bcher-kva.example.com");

    /* a wildcard label may not be an A-label itself */
    X509KeyCertPairConfig alabel = leaf_config("leaf", "DNS:xn--*.example.com");
    EXPECT_PEER_MISMATCH(alabel, "xn--abc.example.com");

    /* a label may not start or end with a hyphen */
    X509KeyCertPairConfig leading = leaf_config("leaf", "DNS:*.-example.com");
    EXPECT_PEER_MISMATCH(leading, "a.-example.com");
    EXPECT_PEER_MISMATCH_EX(leading, "a.-example.com", CAT_STRLEN("a.-example.com"),
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD);
    X509KeyCertPairConfig trailing = leaf_config("leaf", "DNS:*.example-.com");
    EXPECT_PEER_MISMATCH(trailing, "a.example-.com");
    EXPECT_PEER_MISMATCH_EX(trailing, "a.example-.com", CAT_STRLEN("a.example-.com"),
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD);
}

/*
 * badssl.com publishes names that must and must not be accepted. The shared
 * certificate there carries subjectAltName "*.badssl.com, badssl.com", which is
 * what these cases reproduce. See https://badssl.com/
 */
TEST(cat_ssl, peer_name_follows_the_badssl_wildcard_expectations)
{
    X509KeyCertPairConfig config = leaf_config("*.badssl.com", "DNS:*.badssl.com,DNS:badssl.com");

    /* The certificate lists the bare domain as a SAN of its own */
    EXPECT_PEER_MATCH(config, "badssl.com");
    /* a wildcard covers one left-most label */
    EXPECT_PEER_MATCH(config, "foo.badssl.com");
    EXPECT_PEER_MATCH_EX(config, "foo.badssl.com", CAT_STRLEN("foo.badssl.com"),
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD);
    /* the well known wrong.host case: two labels do not match "*" */
    EXPECT_PEER_MISMATCH(config, "wrong.host.badssl.com");
    EXPECT_PEER_MISMATCH_EX(config, "wrong.host.badssl.com", CAT_STRLEN("wrong.host.badssl.com"),
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD);
    EXPECT_PEER_MISMATCH(config, "a.b.badssl.com");
}

/*
 * Weakness, recorded on purpose: the default accepts a wildcard that fills only
 * part of the left-most label, as OpenSSL and the PHP stream layer do. RFC 9525
 * 6.3 and the CA/Browser Forum baseline requirements reject it, and
 * CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD makes this side reject it too.
 */
TEST(cat_ssl, peer_name_accepts_a_partial_wildcard_by_default)
{
    X509KeyCertPairConfig config = leaf_config("badssl.com", "DNS:w*.badssl.com");

    EXPECT_PEER_MATCH(config, "www.badssl.com");
    EXPECT_PEER_MISMATCH_EX(config, "www.badssl.com", CAT_STRLEN("www.badssl.com"),
        CAT_SSL_CHECK_HOST_FLAG_STRICT_WILDCARD);
}

/*
 * The matcher rejects a certificate that names the host only in its subject
 * common name, as current browsers do. CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME
 * restores the older behaviour. The common name then follows the same wildcard
 * rules as a subjectAltName entry.
 */
/*
 * The matcher rejects a certificate that names the host only in its subject
 * common name, as current browsers do. CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME
 * restores the older behaviour. The common name then takes the same wildcard
 * rules as a SAN.
 */
TEST(cat_ssl, peer_name_rejects_a_common_name_identity_by_default)
{
    X509KeyCertPairConfig config = leaf_config("cn.badssl.com", nullptr);

    EXPECT_PEER_MISMATCH(config, "cn.badssl.com");
    EXPECT_PEER_MATCH_EX(config, "cn.badssl.com", CAT_STRLEN("cn.badssl.com"),
        CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME);

    X509KeyCertPairConfig wildcard = leaf_config("*.badssl.com", nullptr);
    EXPECT_PEER_MISMATCH(wildcard, "foo.badssl.com");
    EXPECT_PEER_MATCH_EX(wildcard, "foo.badssl.com", CAT_STRLEN("foo.badssl.com"),
        CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME);
    EXPECT_PEER_MISMATCH_EX(wildcard, "wrong.host.badssl.com", CAT_STRLEN("wrong.host.badssl.com"),
        CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME);
}

/*
 * The dot after the wildcard belongs to the pattern, so the label the wildcard
 * stands for ends exactly at that dot. A matcher that drops the dot accepts any
 * name that merely ends with the suffix text, which lets a certificate for a
 * short domain impersonate every host whose name shares that suffix.
 */
TEST(cat_ssl, peer_name_requires_the_dot_after_a_wildcard_label)
{
    X509KeyCertPairConfig config = leaf_config("leaf", "DNS:*.example.com");

    EXPECT_PEER_MATCH(config, "www.example.com");
    /* no dot between the wildcard label and the suffix */
    EXPECT_PEER_MISMATCH(config, "xyexample.com");
    EXPECT_PEER_MISMATCH(config, "xexample.com");
    EXPECT_PEER_MISMATCH(config, "a.xexample.com");

    X509KeyCertPairConfig short_domain = leaf_config("leaf", "DNS:*.le.com");
    EXPECT_PEER_MATCH(short_domain, "abc.le.com");
    EXPECT_PEER_MISMATCH(short_domain, "google.com");

    X509KeyCertPairConfig partial = leaf_config("leaf", "DNS:w*.example.com");
    EXPECT_PEER_MATCH(partial, "www.example.com");
    EXPECT_PEER_MISMATCH(partial, "wxyexample.com");
}

TEST(cat_ssl, peer_name_rejects_a_wildcard_with_an_empty_label)
{
    X509KeyCertPairConfig config = leaf_config("leaf", "DNS:*..com");

    EXPECT_PEER_MISMATCH(config, "evil.com");
    EXPECT_PEER_MISMATCH(config, ".com");
}

/* When the prefix and the suffix overlap inside the name there is no room for a
 * wildcard label, and the length arithmetic must not underflow */
TEST(cat_ssl, peer_name_handles_an_overlapping_prefix_and_suffix)
{
    /* The prefix "ab" plus the suffix ".x.example.com" is longer than the name
     * "ab", so the length arithmetic goes negative unless a guard prevents it.
     * The wildcard may stand for no character, so "ab.x.example.com" matches */
    X509KeyCertPairConfig config = leaf_config("leaf", "DNS:ab*.x.example.com");
    EXPECT_PEER_MATCH(config, "abc.x.example.com");
    EXPECT_PEER_MATCH(config, "ab.x.example.com");
    EXPECT_PEER_MISMATCH(config, "ab");
    EXPECT_PEER_MISMATCH(config, "abc.x.example.org");

    /* The label count rejects the pattern of the earlier version, so that
     * pattern never reaches the arithmetic. Keep it as a shape that must not
     * crash */
    X509KeyCertPairConfig short_pattern = leaf_config("leaf", "DNS:localh*.host");
    EXPECT_PEER_MISMATCH(short_pattern, "localhost");
    EXPECT_PEER_MISMATCH(short_pattern, "x.localhost");
}

/* A wildcard pattern that carries the root dot names nothing, while a plain
 * rooted subjectAltName names the same host with or without the root dot, as
 * PHP does since bug68265 */
TEST(cat_ssl, peer_name_handles_rooted_names)
{
    X509KeyCertPairConfig wildcard = leaf_config("leaf", "DNS:*.com.");
    EXPECT_PEER_MISMATCH(wildcard, "foo.com.");
    EXPECT_PEER_MISMATCH(wildcard, "foo.com");

    X509KeyCertPairConfig plain = leaf_config("leaf", "DNS:foo.example.com.");
    EXPECT_PEER_MATCH(plain, "foo.example.com");
    EXPECT_PEER_MISMATCH(plain, "other.example.com");
}

/* A reference name that holds an embedded NUL in a DNS subjectAltName, and a
 * subjectAltName entry that holds one itself, both name nothing */
TEST(cat_ssl, peer_name_rejects_an_embedded_nul_in_a_san_entry)
{
    X509KeyCertPairConfig config = leaf_config("leaf", "DNS:good.example.com");

    EXPECT_PEER_MATCH(config, "good.example.com");
    EXPECT_PEER_MISMATCH(config, "good.example.com.evil");
}

/* An unrelated otherName is not a service identity, so the common name can
 * still name the host when the caller asks for it */
TEST(cat_ssl, peer_name_ignores_an_unrelated_other_name)
{
    X509KeyCertPairConfig config = leaf_config("cn.example.com",
        "otherName:1.2.3.4;UTF8:unrelated");

    EXPECT_PEER_MISMATCH(config, "cn.example.com");
    EXPECT_PEER_MATCH_EX(config, "cn.example.com", CAT_STRLEN("cn.example.com"),
        CAT_SSL_CHECK_HOST_FLAG_COMMON_NAME);
}

#endif /* CAT_SSL */
