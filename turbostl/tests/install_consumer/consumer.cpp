#include <turbostl.h>
#include <turbostl/typed.h>

int main() {
  vec_t values{};
  const int input = 9;
  int out = 0;

  if (vec_init_bytes(&values, sizeof(int), alignof(int), 2u) != STL_OK)
    return 1;
  if (vec_push(&values, &input) != STL_OK) return 2;
  if (vec_pop(&values, &out) != STL_OK || out != 9) return 3;
  vec_raw_destroy_storage(&values);
  return 0;
}
