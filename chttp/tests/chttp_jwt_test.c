#include "tinytest.h"

#include <chttp/chttp.h>

#include <string.h>

spec("chttp jwt") {
  group("HS256 client token") {
    it("creates a token and formats an Authorization Bearer header") {
      static const unsigned char key[] = "client-server-test-key";
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

      check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key) - 1u, &token),
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
