#include <cnet/name_lookup.h>
#include <salts/error_codes.h>
#include <tinytest.h>
#include <string.h>

spec("CNet public bounded address streams") {
  it("normalizes IDNA and rejects invalid names before admission") {
    cnet_name_lookup l = {0}; cnet_name_lookup_config c; char ascii[256]; size_t n = 0;
    cnet_name_lookup_config_init(&c); c.query_capacity = 1;
    check_equal(cnet_name_lookup_init(&l, &c), SALTS_OK);
    check_equal(cnet_name_lookup_normalize(&l, "b\xc3\xbc" "cher.de", 10, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_OK);
    check_equal(strcmp(ascii, "xn--bcher-kva.de"), 0);
    check_equal(cnet_name_lookup_normalize(&l, "a\0b", 3, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
    check_equal(cnet_name_lookup_normalize(&l, "\xff", 1, ascii, sizeof(ascii), &n, NULL, NULL), SALTS_EINVAL);
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
