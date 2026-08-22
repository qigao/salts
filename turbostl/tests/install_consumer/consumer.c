#include <turbostl.h>
#include <turbostl/typed.h>

typed(Vec, InstalledInts, int);

int main(void) {
  InstalledInts values = {0};
  int out = 0;

  if (InstalledInts_init(&values, 2u) != TURBO_STL_OK) return 1;
  if (InstalledInts_push(&values, 7) != TURBO_STL_OK) return 2;
  if (InstalledInts_pop(&values, &out) != TURBO_STL_OK || out != 7) return 3;
  InstalledInts_destroy(&values);
  return 0;
}
