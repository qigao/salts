#include <turbostl.h>
#include <turbostl/typed.h>

int main() {
  vec_t values{};
  const int input = 9;
  int out = 0;

  if (vec_init_bytes(&values, sizeof(int), alignof(int), 2u) != TURBO_STL_OK)
    return 1;
  if (vec_push(&values, &input) != TURBO_STL_OK) return 2;
  if (vec_pop(&values, &out) != TURBO_STL_OK || out != 9) return 3;
  vec_destroy(&values);
  return 0;
}
