#include "api.h"

int tinymock_installed_consumer_run(int value) {
  return tinymock_installed_add(value, 2);
}
