#include "tinymock_selective_fixture.h"

int tinymock_selective_consume_mocked(int value) {
  return tinymock_selective_mocked(value);
}

int tinymock_selective_consume_real(int value) {
  return tinymock_selective_real(value);
}

void tinymock_selective_consume_void(void) {
  tinymock_selective_void();
}
