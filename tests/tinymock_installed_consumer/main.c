#define TINYTEST_NO_MAIN
#include <assert.h>

#include <cmeta/function.h>
#include <tinymock_function.h>

#include "api.h"

int tinymock_installed_consumer_run(int value);

TINYMOCk_FUNCTION_DECLARE(tinymock_installed_add);

int main(void) {
  const cmeta_function_desc *meta;

  TINYMOCk_FUNCTION_RESET(tinymock_installed_add);
  tinymock_mock_set_default_return(
      TINYMOCk_FUNCTION(tinymock_installed_add),
      TINYMOCk_RETURN(11));

  assert(tinymock_installed_consumer_run(7) == 11);
  assert(tinymock_mock_call_count(
             TINYMOCk_FUNCTION(tinymock_installed_add)) == 1u);

  meta = TINYMOCk_FUNCTION_META(tinymock_installed_add);
  assert(meta != NULL);
  assert(cmeta_function_desc_valid(meta));
  assert(meta->param_count == 2u);
  return 0;
}
