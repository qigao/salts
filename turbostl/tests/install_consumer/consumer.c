#include <turbostl.h>
#include <turbostl/typed.h>

int main(void) {
  Vec(int, values);
  const int input = 7;
  int out = 0;

  if (vec_init(&values, 2u) != STL_OK) return 1;
  if (vec_push(&values, &input) != STL_OK) return 2;
  if (vec_pop(&values, &out) != STL_OK || out != 7) return 3;
  vec_destroy(&values);
  return 0;
}
