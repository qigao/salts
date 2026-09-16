#include "tinytest.h"
#include "uri_parser.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

enum { URI_T_ABI_SIZE = 2856 };
_Static_assert(sizeof(uri_t) == URI_T_ABI_SIZE, "uri_t ABI size changed");
_Static_assert(offsetof(uri_t, port) == 2848u, "uri_t port offset changed");
_Static_assert(offsetof(uri_t, host_type) == 2852u, "uri_t host_type offset changed");
_Static_assert(offsetof(uri_t, valid) == 2853u, "uri_t valid offset changed");
_Static_assert(offsetof(uri_t, component_flags) == 2854u, "uri_t tail padding was not reused");
_Static_assert(offsetof(uri_t, overflow_flags) == 2855u, "uri_t tail padding was not reused");

spec("uri_parser") {
  describe("Basic HTTP/HTTPS Parsing") {
    it("should parse a simple HTTP URL correctly") {
      uri_t uri;
      int result = uri_parse("http://example.com", &uri);

      check_equal(result, 1);
      check_equal(uri.valid, 1);
      check_equal(uri.scheme, "http");
      check_equal(uri.host, "example.com");
      check_equal(uri.host_type, URI_HOST_REGNAME);
      check_equal(uri.component_flags & URI_COMPONENT_PORT, 0);
    }

    it("should parse an HTTPS URL correctly") {
      uri_t uri;
      int result = uri_parse("https://secure.example.com/login", &uri);

      check_equal(result, 1);
      check_equal(uri.valid, 1);
      check_equal(uri.scheme, "https");
      check_equal(uri.host, "secure.example.com");
      check_equal(uri.path, "/login");
    }
  }

  describe("URL Components") {
    it("should parse a URL with a port correctly") {
      uri_t uri;
      int result = uri_parse("http://example.com:8080", &uri);

      check_equal(result, 1);
      check_equal(uri.valid, 1);
      check_equal(uri.scheme, "http");
      check_equal(uri.host, "example.com");
      check_equal(uri.port, 8080);
      check_equal(uri.component_flags & URI_COMPONENT_PORT, URI_COMPONENT_PORT);
    }

    it("should parse a URL with a path correctly") {
      uri_t uri;
      int result = uri_parse("http://example.com/path/to/resource", &uri);

      check_equal(result, 1);
      check_equal(uri.valid, 1);
      check_equal(uri.scheme, "http");
      check_equal(uri.host, "example.com");
      check_equal(uri.path, "/path/to/resource");
    }

    it("should parse a URL with a query string correctly") {
      uri_t uri;
      int result = uri_parse("http://example.com/path?foo=bar&baz=qux", &uri);

      check_equal(result, 1);
      check_equal(uri.valid, 1);
      check_equal(uri.scheme, "http");
      check_equal(uri.host, "example.com");
      check_equal(uri.path, "/path");
      check_equal(uri.query, "foo=bar&baz=qux");
    }

    it("should parse a URL with a fragment correctly") {
      uri_t uri;
      int result = uri_parse("http://example.com/path#section", &uri);

      check_equal(result, 1);
      check_equal(uri.scheme, "http");
      check_equal(uri.host, "example.com");
      check_equal(uri.path, "/path");
      check_equal(uri.fragment, "section");
    }

    it("should parse a URL with user info correctly") {
      uri_t uri;
      int result = uri_parse("http://user:pass@example.com/path", &uri);

      check_equal(result, 1);
      check_equal(uri.valid, 1);
      check_equal(uri.scheme, "http");
      check_equal(uri.userinfo, "user:pass");
      check_equal(uri.component_flags & URI_COMPONENT_USERINFO, URI_COMPONENT_USERINFO);
      check_equal(uri.host, "example.com");
      check_equal(uri.path, "/path");
    }

    it("preserves empty component presence") {
      uri_t uri;

      check_equal(uri_parse("tcp://@example.com:80", &uri), 1);
      check_equal(uri.userinfo, "");
      check_equal(uri.component_flags & URI_COMPONENT_USERINFO, URI_COMPONENT_USERINFO);
      check_equal(uri_parse("tcp://example.com:80?", &uri), 1);
      check_equal(uri.query, "");
      check_equal(uri.component_flags & URI_COMPONENT_QUERY, URI_COMPONENT_QUERY);
      check_equal(uri_parse("tcp://example.com:80#", &uri), 1);
      check_equal(uri.fragment, "");
      check_equal(uri.component_flags & URI_COMPONENT_FRAGMENT, URI_COMPONENT_FRAGMENT);
    }
  }

  describe("IP Address Parsing") {
    it("should parse IPv4 addresses in URLs correctly") {
      uri_t uri;
      int result = uri_parse("http://192.168.1.1:8080/path", &uri);

      check_equal(result, 1);
      check_equal(uri.valid, 1);
      check_equal(uri.host, "192.168.1.1");
      check_equal(uri.host_type, URI_HOST_IPV4ADDR);
      check_equal(uri.port, 8080);
    }

    it("should parse IPv6 addresses in URLs correctly") {
      uri_t uri;
      int result = uri_parse("http://[::1]:8080/path", &uri);

      check_equal(result, 1);
      check_equal(uri.valid, 1);
      check_equal(uri.host, "::1");
      check_equal(uri.host_type, URI_HOST_IPV6ADDR);
      check_equal(uri.port, 8080);
    }
  }

  describe("Complex URLs") {
    it("should parse a full URL with all components correctly") {
      uri_t uri;
      int result = uri_parse(
          "https://user:pass@example.com:443/path/to/resource?query=value#fragment", &uri);

      check_equal(result, 1);
      check_equal(uri.valid, 1);
      check_equal(uri.scheme, "https");
      check_equal(uri.userinfo, "user:pass");
      check_equal(uri.host, "example.com");
      check_equal(uri.port, 443);
      check_equal(uri.path, "/path/to/resource");
      check_equal(uri.query, "query=value");
      check_equal(uri.fragment, "fragment");
    }
  }

  describe("Error Handling") {
    it("should return error for NULL input") {
      uri_t uri;
      check_equal(uri_parse(NULL, &uri), 0);
      check_equal(uri_parse("http://example.com", NULL), 0);
    }

    it("should return error for URLs missing a scheme") {
      uri_t uri;
      /* Missing scheme */
      check_equal(uri_parse("example.com", &uri), 0);
    }

    it("rejects an empty port instead of normalizing it to zero") {
      uri_t uri;
      check_equal(uri_parse("tcp://example.com:", &uri), 0);
      check_equal(uri.valid, 0);
    }

    it("preserves syntactically valid numeric port overflow for policy validation") {
      uri_t uri;
      check_equal(uri_parse("tcp://example.com:2147483647", &uri), 1);
      check_equal(uri.port, INT_MAX);
      check_equal(uri.overflow_flags & URI_OVERFLOW_PORT, 0);
      check_equal(uri_parse("tcp://example.com:999999999999999999999", &uri), 1);
      check_equal(uri.valid, 1);
      check_equal(uri.component_flags & URI_COMPONENT_PORT, URI_COMPONENT_PORT);
      check_equal(uri.overflow_flags & URI_OVERFLOW_PORT, URI_OVERFLOW_PORT);
    }

    it("rejects components that cannot fit without truncation") {
      uri_t uri;
      char oversized_scheme[96];
      char oversized_host[320];
      char oversized_path[1200];
      size_t index;

      memset(oversized_scheme, 'a', 40u);
      memcpy(oversized_scheme + 40u, "://example.com", 15u);
      oversized_scheme[55] = '\0';
      check_equal(uri_parse(oversized_scheme, &uri), 0);
      check_equal(uri.valid, 0);
      check_equal(uri.overflow_flags & URI_OVERFLOW_COMPONENT, URI_OVERFLOW_COMPONENT);

      memcpy(oversized_host, "tcp://", 6u);
      for (index = 6u; index < sizeof(oversized_host) - 4u; ++index)
        oversized_host[index] = 'a';
      memcpy(oversized_host + sizeof(oversized_host) - 4u, ":80", 4u);
      check_equal(uri_parse(oversized_host, &uri), 0);
      check_equal(uri.valid, 0);
      check_equal(uri.overflow_flags & URI_OVERFLOW_COMPONENT, URI_OVERFLOW_COMPONENT);

      memcpy(oversized_path, "http://example.com/", 19u);
      memset(oversized_path + 19u, 'p', sizeof(oversized_path) - 20u);
      oversized_path[sizeof(oversized_path) - 1u] = '\0';
      check_equal(uri_parse(oversized_path, &uri), 0);
      check_equal(uri.valid, 0);
      check_equal(uri.overflow_flags & URI_OVERFLOW_COMPONENT, URI_OVERFLOW_COMPONENT);
    }
  }
}
