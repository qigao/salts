#include "tinytest.h"

#include <chttp/chttp.h>

#include <string.h>

static const unsigned char valid_key[] = "0123456789abcdef0123456789abcdef";
static const unsigned char short_key[] = "0123456789abcdef0123456789abcde";

spec("chttp jwt") {
  group("HS256 key policy") {
    it("rejects HS256 secrets shorter than 32 bytes") {
      const chttp_jwt_claims claims = {.subject = "alice", .expires_at = INT64_C(3000000000)};
      chttp_jwt_bearer_validator validator = {0};
      const chttp_jwt_bearer_validator_options options = {
          .size = sizeof(options), .key = short_key, .key_size = sizeof(short_key) - 1u};
      char *token = NULL;

      check_equal(chttp_jwt_hs256_token_create(&claims, short_key, sizeof(short_key) - 1u, &token),
                  SALTS_EINVAL);
      check_null(token);
      check_equal(chttp_jwt_bearer_validator_init(&validator, &options), SALTS_EINVAL);
      check_null(validator.impl);
    }

    it("accepts a 32 byte HS256 secret") {
      const chttp_jwt_claims claims = {.subject = "alice", .expires_at = INT64_C(3000000000)};
      char *token = NULL;

      check_equal(chttp_jwt_hs256_token_create(&claims, valid_key, sizeof(valid_key) - 1u, &token),
                  SALTS_OK);
      check_not_null(token);
      chttp_jwt_token_destroy(token);
    }
  }

  group("HS256 client token") {
    it("creates a token and formats an Authorization Bearer header") {
      const chttp_jwt_claims claims = {
          .issuer = "issuer.example",
          .subject = "alice",
          .audience = "api.example",
          .issued_at = 1700000000,
          .not_before = 1700000000,
          .expires_at = 1700003600,
      };
      char *token = NULL;
      char authorization[512];
      chttp_header header = {0};

      check_equal(chttp_jwt_hs256_token_create(&claims, valid_key, sizeof(valid_key) - 1u, &token),
                  SALTS_OK);
      check_not_null(token);
      check_equal(chttp_jwt_bearer_header(token, authorization, sizeof(authorization), &header),
                  SALTS_OK);
      check_equal(header.name, "Authorization");
      check_equal(strncmp(header.value, "Bearer ", strlen("Bearer ")), 0);
      check_equal(header.value + strlen("Bearer "), token);

      chttp_jwt_token_destroy(token);
    }
  }
}
