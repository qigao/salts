#include "api.h"

int tinymock_installed_consumer_run(int value) {
  return tinymock_installed_add(value, 2);
}

void tinymock_installed_consumer_shutdown(void) {
  tinymock_installed_shutdown();
}

int tinymock_installed_real(int value) {
  return value + 20;
}

int tinymock_installed_consumer_real(int value) {
  return tinymock_installed_real(value);
}
