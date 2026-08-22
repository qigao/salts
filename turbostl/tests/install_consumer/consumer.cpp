#include <turbostl.h>
#include <turbostl/typed.h>

int main() {
  turbo_vec_t values{};
  const int input = 9;
  int out = 0;

  if (turbo_vec_init_bytes(&values, sizeof(int), alignof(int), 2u) != TURBO_STL_OK)
    return 1;
  if (turbo_vec_push(&values, &input) != TURBO_STL_OK) return 2;
  if (turbo_vec_pop(&values, &out) != TURBO_STL_OK || out != 9) return 3;
  turbo_vec_destroy(&values);
  return 0;
}
