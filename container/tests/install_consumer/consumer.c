#include <turbo/container/typed.h>

Containers((Vec, InstalledInts, int));

int main(void) {
  InstalledInts values = {0};
  int out = 0;

  if (InstalledInts_init(&values, 2u) != CONTAINER_OK) return 1;
  if (InstalledInts_push(&values, 7) != CONTAINER_OK) return 2;
  if (InstalledInts_pop(&values, &out) != CONTAINER_OK || out != 7) return 3;
  InstalledInts_destroy(&values);
  return 0;
}
