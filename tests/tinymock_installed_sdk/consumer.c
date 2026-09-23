#include "reflected_api.h"
#include <tinymock_function.h>

TINYMOCk_USE(tinymock_installed_add);

int main(void) {
  const tinymock_recorded_call_t *call;

  mock_tinymock_installed_add_reset();
  if (!cmeta_function_desc_valid(
          TINYMOCk_FUNCTION_META(tinymock_installed_add)))
    return 1;

  tinymock_mock_set_default_return(
      TINYMOCk_FUNCTION(tinymock_installed_add), TINYMOCk_RETURN(11));

  if (tinymock_installed_add(5, 6) != 11)
    return 2;
  if (tinymock_mock_call_count(
          TINYMOCk_FUNCTION(tinymock_installed_add)) != 1u)
    return 3;

  call = tinymock_mock_call_at(
      TINYMOCk_FUNCTION(tinymock_installed_add), 0u);
  if (call == NULL || call->argc != 2u)
    return 4;
  if (TINYMOCk_VALUE_AS(int, call->args[0]) != 5 ||
      TINYMOCk_VALUE_AS(int, call->args[1]) != 6)
    return 5;

  return 0;
}
