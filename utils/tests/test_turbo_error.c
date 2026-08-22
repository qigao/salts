#include "turbo_error.h"
#include "tinytest.h"

#include <errno.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define TEST_ERROR_DOMAIN 42
#define TEST_ERROR_AUTH TURBO_ERROR_CUSTOM(TEST_ERROR_DOMAIN, 1)
#define TEST_ERROR_PARSE TURBO_ERROR_CUSTOM(TEST_ERROR_DOMAIN, 2)

static const turbo_error_entry_t test_errors[] = {
    {TEST_ERROR_AUTH, "TEST_EAUTH", "test authentication failed"},
    {TEST_ERROR_PARSE, "TEST_EPARSE", "test parse failed"},
};

static const turbo_error_domain_desc_t test_domain = {
    TEST_ERROR_DOMAIN,
    "test",
    test_errors,
    sizeof(test_errors) / sizeof(test_errors[0]),
};

suite("Turbo Error") {
  group("Turbo codes") {
    it("describes common and protocol errors") {
      check_equal(turbo_strerror(TURBO_OK), "success");
      check_equal(turbo_strerror(TURBO_EINVAL), "invalid argument");
      check_equal(turbo_strerror(TURBO_EPROTO), "protocol error");
      check_equal(turbo_strerror(TURBO_EAI_FAMILY), "address family not supported by DNS result");
    }

    it("returns structured metadata") {
      turbo_error_info_t info = turbo_error_info(TURBO_ENOMEM);

      check_equal(info.code, TURBO_ENOMEM);
      check_equal(info.domain, TURBO_ERROR_DOMAIN_TURBO);
      check_equal(info.name, "TURBO_ENOMEM");
      check_equal(info.message, "not enough memory");
    }

    it("maps turbo_error_domain enum to and from string") {
      turbo_error_domain_t domain = TURBO_ERROR_DOMAIN_UNKNOWN;
      const cmeta_enum_desc *meta = turbo_error_domain_t_meta();

      check_not_null(meta);
      check_equal(meta->name, "turbo_error_domain_t");
      check_equal(meta->count, (size_t)6u);
      check_equal(turbo_error_domain_t_to_symbol(TURBO_ERROR_DOMAIN_WIN32),
                  "TURBO_ERROR_DOMAIN_WIN32");
      check_equal(turbo_error_domain_t_to_string(TURBO_ERROR_DOMAIN_WIN32), "win32");
      check_true(turbo_error_domain_t_from_string("TURBO_ERROR_DOMAIN_WIN32", &domain));
      check_equal(domain, TURBO_ERROR_DOMAIN_WIN32);
      check_equal(turbo_error_domain_to_string(TURBO_ERROR_DOMAIN_NONE), "none");
      check_equal(turbo_error_domain_to_string(TURBO_ERROR_DOMAIN_CUSTOM), "custom");
      check_equal(turbo_error_domain_from_string("win32", &domain), 0);
      check_equal(domain, TURBO_ERROR_DOMAIN_WIN32);
      domain = TURBO_ERROR_DOMAIN_POSIX;
      check_equal(turbo_error_domain_from_string("TURBO_ERROR_DOMAIN_WIN32", &domain), -1);
      check_equal(domain, TURBO_ERROR_DOMAIN_POSIX);
      check_equal(turbo_error_domain_from_string("not_found", &domain), -1);
      check_equal(turbo_error_domain_to_string((turbo_error_domain_t)999), "unknown");
      check_equal(turbo_error_domain_count(), (size_t)6u);
      check_true(turbo_error_domain_is_valid(TURBO_ERROR_DOMAIN_TURBO));
      check_false(turbo_error_domain_is_valid((turbo_error_domain_t)999));
      check_true(turbo_error_domain_equals(TURBO_ERROR_DOMAIN_TURBO,
                                           TURBO_ERROR_DOMAIN_TURBO));
    }
  }

  group("Native codes") {
    it("describes negative errno values") {
      turbo_error_info_t info = turbo_error_info(-ENOENT);

      check_equal(info.domain, TURBO_ERROR_DOMAIN_POSIX);
      check_equal(info.name, "POSIX");
      check_not_null(info.message);
      check(strstr(info.message, "unknown") == NULL && strstr(info.message, "Unknown") == NULL);
    }

#ifdef _WIN32
    it("describes negative Win32 values") {
      turbo_error_info_t info = turbo_error_info(-(int)ERROR_PIPE_BUSY);

      check_equal(info.domain, TURBO_ERROR_DOMAIN_WIN32);
      check_equal(info.name, "WIN32");
      check_not_null(info.message);
      check(strstr(info.message, "unknown") == NULL && strstr(info.message, "Unknown") == NULL);
    }
#endif
  }

  group("Result wrapper") {
    it("wraps success and errors") {
      turbo_result_t ok = turbo_result_from_code(TURBO_OK);
      turbo_result_t err = turbo_result_from_code(TURBO_EPERM);

      check_true(turbo_result_is_ok(ok));
      check_false(turbo_result_is_err(ok));
      check_equal(ok.code, TURBO_OK);
      check_equal(ok.message, "success");

      check_false(turbo_result_is_ok(err));
      check_true(turbo_result_is_err(err));
      check_equal(err.code, TURBO_EPERM);
      check_equal(err.message, "operation not permitted");
    }
  }

  group("Custom domains") {
    it("registers and resolves custom module errors") {
      turbo_error_info_t info;

      (void)turbo_error_unregister_domain(TEST_ERROR_DOMAIN);
      check_equal(turbo_error_register_domain(&test_domain), TURBO_OK);

      info = turbo_error_info(TEST_ERROR_AUTH);
      check_equal(info.code, TEST_ERROR_AUTH);
      check_equal(info.domain, TURBO_ERROR_DOMAIN_CUSTOM);
      check_equal(info.custom_domain, TEST_ERROR_DOMAIN);
      check_equal(info.domain_name, "test");
      check_equal(info.name, "TEST_EAUTH");
      check_equal(info.message, "test authentication failed");
      check_equal(turbo_strerror(TEST_ERROR_PARSE), "test parse failed");

      check_equal(turbo_error_register_domain(&test_domain), TURBO_EALREADY);
      check_equal(turbo_error_unregister_domain(TEST_ERROR_DOMAIN), TURBO_OK);
      check_equal(turbo_error_info(TEST_ERROR_AUTH).domain, TURBO_ERROR_DOMAIN_UNKNOWN);
    }

    it("rejects invalid custom domain descriptors") {
      static const turbo_error_entry_t bad_errors[] = {
          {TURBO_ERROR_CUSTOM(TEST_ERROR_DOMAIN + 1, 1), "BAD", "bad domain"},
      };
      static const turbo_error_domain_desc_t bad_domain = {
          TEST_ERROR_DOMAIN,
          "bad",
          bad_errors,
          sizeof(bad_errors) / sizeof(bad_errors[0]),
      };

      check_equal(turbo_error_register_domain(NULL), TURBO_EINVAL);
      check_equal(turbo_error_register_domain(&bad_domain), TURBO_EINVAL);
      check_equal(turbo_error_unregister_domain(TEST_ERROR_DOMAIN), TURBO_ENOENT);
    }
  }
}
