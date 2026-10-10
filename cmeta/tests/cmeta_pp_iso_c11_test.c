/* Compile with -std=c11 -Wall -Wextra -Werror -pedantic on Clang/GCC.
 * No C23 omitted-variadic-argument extension is needed for one-row relations.
 */
#include <cmeta/compute.h>

enum { first_single = CMETA_PP_FIRST(7) };
enum { first_many = CMETA_PP_FIRST(3, 5, 7) };

TypeFunction(PPOnly, (int, double, double));
TypeFunction(PPMulti, (int, double, double), (short, double, float));

typedef TypeEval(PPOnly, int, double) pp_only_result;
typedef TypeEval(PPMulti, short, double) pp_multi_result;

_Static_assert(first_single == 7, "single item FIRST");
_Static_assert(first_many == 3, "many items FIRST");
_Static_assert(sizeof(pp_only_result) == sizeof(double), "one-row TypeFunction");
_Static_assert(sizeof(pp_multi_result) == sizeof(float), "multi-row TypeFunction");

int main(void)
{
    return 0;
}
