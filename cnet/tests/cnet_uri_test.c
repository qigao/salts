#include "cnet_uri.h"
#include "tinytest.h"

#include <string.h>

spec("CNet strict transport URI") {
  it("parses bounded TCP UDP and TLS authorities") {
    cnet_uri uri = {0};

    check_equal(cnet_uri_parse("tcp://example.com:443", &uri), TURBO_OK);
    check_equal(uri.scheme, CNET_URI_TCP);
    check_equal(strcmp(uri.host, "example.com"), 0);
    check_equal(uri.port, 443u);
    check_equal(uri.path[0], '\0');

    check_equal(cnet_uri_parse("udp://[::1]:9000", &uri), TURBO_OK);
    check_equal(uri.scheme, CNET_URI_UDP);
    check_equal(strcmp(uri.host, "::1"), 0);
    check_equal(uri.port, 9000u);

    check_equal(cnet_uri_parse("tls://example.com:443", &uri), TURBO_OK);
    check_equal(uri.scheme, CNET_URI_TLS);
    check_equal(strcmp(uri.host, "example.com"), 0);
    check_equal(uri.port, 443u);
  }

  it("parses a bounded pipe name without inventing an authority") {
    cnet_uri uri = {0};
    check_equal(cnet_uri_parse("pipe://service/control", &uri), TURBO_OK);
    check_equal(uri.scheme, CNET_URI_PIPE);
    check_equal(strcmp(uri.path, "service/control"), 0);
    check_equal(uri.host[0], '\0');
    check_equal(uri.port, 0u);
  }

  it("rejects ambiguous unsupported and overflowing forms") {
    cnet_uri uri = {0};
    char oversized[CNET_URI_MAX_BYTES + 2u];
    memset(oversized, 'a', sizeof(oversized));
    oversized[sizeof(oversized) - 1u] = '\0';

    check_equal(cnet_uri_parse(NULL, &uri), TURBO_EINVAL);
    check_equal(cnet_uri_parse("tcp://:80", &uri), TURBO_EINVAL);
    check_equal(cnet_uri_parse("tcp://host", &uri), TURBO_EINVAL);
    check_equal(cnet_uri_parse("tcp://host:0", &uri), TURBO_ERANGE);
    check_equal(cnet_uri_parse("tcp://host:65536", &uri), TURBO_ERANGE);
    check_equal(cnet_uri_parse("tcp://host:80/path", &uri), TURBO_EINVAL);
    check_equal(cnet_uri_parse("tcp://user@host:80", &uri), TURBO_EINVAL);
    check_equal(cnet_uri_parse("udp://[::1:80", &uri), TURBO_EINVAL);
    check_equal(cnet_uri_parse("pipe://", &uri), TURBO_EINVAL);
    check_equal(cnet_uri_parse("tls://example.com", &uri), TURBO_EINVAL);
    check_equal(cnet_uri_parse(oversized, &uri), TURBO_ERANGE);
    check_equal(uri.scheme, CNET_URI_NONE);
  }
}
