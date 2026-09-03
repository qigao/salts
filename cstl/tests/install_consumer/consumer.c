#include <cstl.h>
#include <cstl/typed.h>

typed(Vec, InstalledInts, int);

int main(void) {
  InstalledInts values = {0};
  vec_t raw_values = VecOf(int);
  int out = 0;

  if (InstalledInts_init(&values, 2u) != STL_OK) return 1;
  if (InstalledInts_push(&values, 7) != STL_OK) return 2;
  if (InstalledInts_pop(&values, &out) != STL_OK || out != 7) return 3;
  if (vec_init(&raw_values, 1u) != STL_OK) {
    InstalledInts_destroy(&values);
    return 4;
  }
  vec_destroy(&raw_values);
  InstalledInts_destroy(&values);
  return 0;
}
