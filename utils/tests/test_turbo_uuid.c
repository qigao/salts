#include "tinytest.h"
#include "turbo_error.h"
#include "turbo_uuid.h"

#include <string.h>

suite("Turbo UUID") {
  group("Secure random") {
    it("validates destination buffers") {
      check_equal(turbo_secure_random(NULL, 0U), TURBO_OK);
      check_equal(turbo_secure_random(NULL, 1U), TURBO_EINVAL);
    }

    it("fills independent buffers") {
      uint8_t first[32];
      uint8_t second[32];

      memset(first, 0, sizeof(first));
      memset(second, 0, sizeof(second));
      check_equal(turbo_secure_random(first, sizeof(first)), TURBO_OK);
      check_equal(turbo_secure_random(second, sizeof(second)), TURBO_OK);
      check_not_equal(first, second, sizeof(first));
    }
  }

  group("UUID version 4") {
    it("rejects a missing destination") {
      check_equal(turbo_uuid_v4_generate(NULL), TURBO_EINVAL);
    }

    it("generates a fixed-size RFC variant value") {
      turbo_uuid_t uuid;

      check_equal(sizeof(uuid), TURBO_UUID_SIZE);
      check_equal(turbo_uuid_v4_generate(&uuid), TURBO_OK);
      check_equal(uuid.bytes[6] & 0xf0U, 0x40U);
      check_equal(uuid.bytes[8] & 0xc0U, 0x80U);
    }

    it("generates distinct values") {
      turbo_uuid_t first;
      turbo_uuid_t second;

      check_equal(turbo_uuid_v4_generate(&first), TURBO_OK);
      check_equal(turbo_uuid_v4_generate(&second), TURBO_OK);
      check_false(turbo_uuid_equal(&first, &second));
    }
  }

  group("UUID version 7") {
    it("rejects a missing destination") {
      check_equal(turbo_uuid_v7_generate(NULL), TURBO_EINVAL);
    }

    it("sets the RFC version and variant bits") {
      turbo_uuid_t uuid;

      check_equal(turbo_uuid_v7_generate(&uuid), TURBO_OK);
      check_equal(uuid.bytes[6] & 0xf0U, 0x70U);
      check_equal(uuid.bytes[8] & 0xc0U, 0x80U);
    }

    it("generates distinct values within one process") {
      turbo_uuid_t first;
      turbo_uuid_t second;

      check_equal(turbo_uuid_v7_generate(&first), TURBO_OK);
      check_equal(turbo_uuid_v7_generate(&second), TURBO_OK);
      check_false(turbo_uuid_equal(&first, &second));
    }
  }

  group("Text conversion") {
    it("round-trips a canonical UUID") {
      const char *text = "00112233-4455-4677-8899-aabbccddeeff";
      turbo_uuid_t uuid;
      turbo_uuid_t reparsed;
      char formatted[TURBO_UUID_STRING_SIZE];

      check_equal(turbo_uuid_parse(text, &uuid), TURBO_OK);
      check_equal(turbo_uuid_format(&uuid, formatted, sizeof(formatted)), TURBO_OK);
      check_equal(formatted, text);
      check_equal(turbo_uuid_parse(formatted, &reparsed), TURBO_OK);
      check_true(turbo_uuid_equal(&uuid, &reparsed));
    }

    it("rejects malformed and undersized text buffers") {
      turbo_uuid_t uuid;
      char short_buffer[TURBO_UUID_STRING_LENGTH];

      check_equal(turbo_uuid_parse("00112233-4455-4677-8899-aabbccddeefg", &uuid), TURBO_EINVAL);
      check_equal(turbo_uuid_parse("001122334455-4677-8899-aabbccddeeff", &uuid), TURBO_EINVAL);
      check_equal(turbo_uuid_parse("00112233-4455-4677-8899-aabbccddeeff", &uuid), TURBO_OK);
      check_equal(turbo_uuid_format(&uuid, short_buffer, sizeof(short_buffer)), TURBO_ENOSPC);
    }
  }
}
