#include "cnet_uri.h"
#include "tinytest.h"

#include <string.h>

spec("CNet strict transport URI") {
  it("parses bounded TCP UDP and TLS authorities") {
    cnet_uri uri = {0};

    check_equal(cnet_uri_parse("tcp://example.com:443", &uri), SALTS_OK);
    check_equal(uri.scheme, CNET_URI_TCP);
    check_equal(strcmp(uri.host, "example.com"), 0);
    check_equal(uri.port, 443u);
    check_equal(uri.path[0], '\0');

    check_equal(cnet_uri_parse("udp://[::1]:9000", &uri), SALTS_OK);
    check_equal(uri.scheme, CNET_URI_UDP);
    check_equal(strcmp(uri.host, "::1"), 0);
    check_equal(uri.port, 9000u);

    check_equal(cnet_uri_parse("tls://example.com:443", &uri), SALTS_OK);
    check_equal(uri.scheme, CNET_URI_TLS);
    check_equal(strcmp(uri.host, "example.com"), 0);
    check_equal(uri.port, 443u);
  }

  it("parses a bounded pipe name without inventing an authority") {
    cnet_uri uri = {0};
    check_equal(cnet_uri_parse("pipe://service/control", &uri), SALTS_OK);
    check_equal(uri.scheme, CNET_URI_PIPE);
    check_equal(strcmp(uri.path, "service/control"), 0);
    check_equal(uri.host[0], '\0');
    check_equal(uri.port, 0u);

    check_equal(cnet_uri_parse("pipe://[service]", &uri), SALTS_OK);
    check_equal(strcmp(uri.path, "[service]"), 0);

    check_equal(cnet_uri_parse("pipe://name:segment", &uri), SALTS_OK);
    check_equal(strcmp(uri.path, "name:segment"), 0);
  }

  it("rejects ambiguous unsupported and overflowing forms") {
    enum { TEST_OVERSIZED_NETWORK_HOST_BYTES = 300 };
    cnet_uri uri = {0};
    char oversized[CNET_URI_MAX_BYTES + 2u];
    char oversized_network[sizeof("tcp://") - 1u + TEST_OVERSIZED_NETWORK_HOST_BYTES +
                           sizeof(":80")];
    memset(oversized, 'a', sizeof(oversized));
    oversized[sizeof(oversized) - 1u] = '\0';
    memcpy(oversized_network, "tcp://", sizeof("tcp://") - 1u);
    memset(oversized_network + sizeof("tcp://") - 1u, 'h', TEST_OVERSIZED_NETWORK_HOST_BYTES);
    memcpy(oversized_network + sizeof("tcp://") - 1u + TEST_OVERSIZED_NETWORK_HOST_BYTES, ":80",
           sizeof(":80"));

    check_equal(cnet_uri_parse(NULL, &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://:80", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://host", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://host:0", &uri), SALTS_ERANGE);
    check_equal(cnet_uri_parse("tcp://host:65536", &uri), SALTS_ERANGE);
    check_equal(cnet_uri_parse("tcp://host:999999999999999999999", &uri), SALTS_ERANGE);
    check_equal(cnet_uri_parse("tcp://host:80/path", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://host:80?query", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://host:80#fragment", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://user@host:80", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://@host:80", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://host:80?", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://host:80#", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://host:", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tcp://bad%zz:80", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("udp://[::1:80", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("pipe://", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse("tls://example.com", &uri), SALTS_EINVAL);
    check_equal(cnet_uri_parse(oversized_network, &uri), SALTS_ERANGE);
    check_equal(cnet_uri_parse(oversized, &uri), SALTS_ERANGE);
    check_equal(uri.scheme, CNET_URI_NONE);
  }
}
