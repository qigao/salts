#include <cnet/name_lookup.h>
#include <salts/error_codes.h>
#include <tinytest.h>
#include <string.h>

spec("CNet public bounded address streams") {
  it("normalizes ASCII names and preserves existing A-labels") {
    cnet_name_lookup l = {0};
    cnet_name_lookup_config c;
    char ascii[256] = {0};
    size_t n = 0u;
    bool numeric = true;
    cnet_name_lookup_config_init(&c);
    c.query_capacity = 1;
    check_equal(cnet_name_lookup_init(&l, &c), SALTS_OK);
    check_equal(cnet_name_lookup_normalize(
        &l, "Example.COM.", 12, ascii, sizeof(ascii), &n, &numeric, NULL), SALTS_OK);
    check_equal(strcmp(ascii, "example.com."), 0);
    check_equal(n, 12u);
    check_false(numeric);
    check_equal(cnet_name_lookup_normalize(
        &l, "xn--bcher-kva.de", 16, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_OK);
    check_equal(strcmp(ascii, "xn--bcher-kva.de"), 0);
    check_equal(n, 16u);
    check_equal(cnet_name_lookup_close(&l), SALTS_OK);
    check_equal(cnet_name_lookup_destroy(&l), SALTS_OK);
  }
  it("rejects non-ASCII, malformed labels and oversized names without partial output") {
    cnet_name_lookup l = {0};
    cnet_name_lookup_config c;
    char ascii[256] = "unchanged";
    char long_label[65];
    size_t n = 23u;
    cnet_name_lookup_config_init(&c);
    c.query_capacity = 1;
    check_equal(cnet_name_lookup_init(&l, &c), SALTS_OK);
    memset(long_label, 'a', 64);
    long_label[64] = '\0';
    check_equal(cnet_name_lookup_normalize(
        &l, "b\xc3\xbc" "cher.de", 10, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
    check_equal(cnet_name_lookup_normalize(
        &l, "a\0b", 3, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
    check_equal(cnet_name_lookup_normalize(
        &l, "\xff", 1, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
    check_equal(cnet_name_lookup_normalize(
        &l, "foo..bar", 8, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
    check_equal(cnet_name_lookup_normalize(
        &l, "-bad.example", 12, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
    check_equal(cnet_name_lookup_normalize(
        &l, "bad-.example", 12, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
    check_equal(cnet_name_lookup_normalize(
        &l, "foo_bar.example", 15, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
    check_equal(cnet_name_lookup_normalize(
        &l, long_label, 64, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
    check_equal(n, 23u);
    check_equal(strcmp(ascii, "unchanged"), 0);
    check_equal(cnet_name_lookup_close(&l), SALTS_OK);
    check_equal(cnet_name_lookup_destroy(&l), SALTS_OK);
  }
  it("publishes numeric addresses without DNS and keeps ready results after cancel") {
    cnet_name_lookup l = {0}, other = {0}; cnet_name_lookup_config c;
    cnet_name_query q = {0}, q2 = {0}; cnet_ip_address a; bool ready = false;
    cnet_name_lookup_config_init(&c); c.query_capacity = 1;
    check_equal(cnet_name_lookup_init(&l, &c), SALTS_OK);
    check_equal(cnet_name_lookup_init(&other, &c), SALTS_OK);
    check_equal(cnet_name_lookup_submit(&l, "127.0.0.1", 9, &q), SALTS_OK);
    check_equal(cnet_name_lookup_ready(&l, q, &ready), SALTS_OK); check(ready);
    check_equal(cnet_name_lookup_ready(&other, q, &ready), SALTS_EINVAL);
    check_equal(cnet_name_lookup_cancel(&l, q), SALTS_EALREADY);
    check_equal(cnet_name_lookup_next(&l, q, &a), SALTS_OK);
    check_equal(a.family, CNET_DATAGRAM_ADDRESS_IPV4); check_equal(a.address[0], 127);
    check_equal(cnet_name_lookup_next(&l, q, &a), SALTS_EOF);
    check_equal(cnet_name_lookup_submit(&l, "::1", 3, &q2), SALTS_ENOBUFS);
    check_equal(cnet_name_lookup_query_drop(&l, &q), SALTS_OK);
    check_equal(cnet_name_lookup_submit(&l, "::ffff:127.0.0.1", 16, &q2), SALTS_OK);
    check_equal(cnet_name_lookup_next(&l, q2, &a), SALTS_OK); check_equal(a.family, CNET_DATAGRAM_ADDRESS_IPV4);
    check_equal(cnet_name_lookup_query_drop(&l, &q2), SALTS_OK);
    check_equal(cnet_name_lookup_close(&l), SALTS_OK); check_equal(cnet_name_lookup_destroy(&l), SALTS_OK);
    check_equal(cnet_name_lookup_close(&other), SALTS_OK); check_equal(cnet_name_lookup_destroy(&other), SALTS_OK);
  }
}
