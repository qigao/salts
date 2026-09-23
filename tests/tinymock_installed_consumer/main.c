#define TINYTEST_NO_MAIN
#include <assert.h>

#include <cmeta/function.h>
#include <tinymock_function.h>

#include "api.h"

static int tinymock_installed_callback_a(int value) {
  return value + 4;
}

static int tinymock_installed_callback_b(int value) {
  return value + 10;
}

int tinymock_installed_consumer_run(int value);
void tinymock_installed_consumer_shutdown(void);
int tinymock_installed_consumer_real(int value);
tinymock_installed_box
tinymock_installed_consumer_box(tinymock_installed_box input);
int tinymock_installed_consumer_apply_callback(
    tinymock_installed_callback callback, int value);
tinymock_installed_callback
tinymock_installed_consumer_callback_answer(void);

TINYMOCk_FUNCTION_DECLARE(tinymock_installed_add);
TINYMOCk_FUNCTION_DECLARE(tinymock_installed_shutdown);
TINYMOCk_FUNCTION_DECLARE(tinymock_installed_box_copy);
TINYMOCk_FUNCTION_DECLARE(tinymock_installed_apply_callback);
TINYMOCk_FUNCTION_DECLARE(tinymock_installed_callback_answer);

int main(void) {
  const cmeta_function_desc *meta;

  TINYMOCk_FUNCTION_RESET(tinymock_installed_add);
  tinymock_mock_set_default_return(
      TINYMOCk_FUNCTION(tinymock_installed_add),
      TINYMOCk_RETURN(11));

  assert(tinymock_installed_consumer_run(7) == 11);
  assert(tinymock_installed_consumer_real(7) == 27);
  assert(tinymock_mock_call_count(
             TINYMOCk_FUNCTION(tinymock_installed_add)) == 1u);

  meta = TINYMOCk_FUNCTION_META(tinymock_installed_add);
  assert(meta != NULL);
  assert(cmeta_function_desc_valid(meta));
  assert(meta->param_count == 2u);

  {
    tinymock_installed_box input = {5};
    tinymock_installed_box scripted = {29};
    tinymock_installed_box result;

    TINYMOCk_FUNCTION_RESET(tinymock_installed_box_copy);
    assert(TINYMOCk_FUNCTION_SET_RETURN(
        tinymock_installed_box_copy, scripted));
    result = tinymock_installed_consumer_box(input);
    assert(result.value == 29);
    assert(TINYMOCk_FUNCTION_ARG_EQUAL_TYPED(
        tinymock_installed_box_copy, 0u, "input", input));
  }

  {
    tinymock_installed_callback expected =
        tinymock_installed_callback_a;
    tinymock_installed_callback scripted =
        tinymock_installed_callback_b;
    tinymock_installed_callback returned;

    TINYMOCk_FUNCTION_RESET(tinymock_installed_apply_callback);
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_installed_apply_callback),
        TINYMOCk_RETURN(71));

    assert(tinymock_installed_consumer_apply_callback(
        tinymock_installed_callback_a, 3) == 71);
    assert(TINYMOCk_FUNCTION_ARG_EQUAL_TYPED(
        tinymock_installed_apply_callback, 0u, "callback", expected));

    TINYMOCk_FUNCTION_RESET(tinymock_installed_callback_answer);
    assert(TINYMOCk_FUNCTION_SET_RETURN(
        tinymock_installed_callback_answer, scripted));
    returned = tinymock_installed_consumer_callback_answer();
    assert(returned == tinymock_installed_callback_b);
    assert(returned(2) == 12);
  }

  TINYMOCk_FUNCTION_RESET(tinymock_installed_shutdown);
  tinymock_installed_consumer_shutdown();
  tinymock_mock_verify_times(
      TINYMOCk_FUNCTION(tinymock_installed_shutdown), 1u);
  assert(cmeta_type_equal(
      TINYMOCk_FUNCTION_META(tinymock_installed_shutdown)->return_type,
      &cmeta_type_void));

  TINYMOCk_FUNCTION_DESTROY(tinymock_installed_add);
  TINYMOCk_FUNCTION_DESTROY(tinymock_installed_shutdown);
  TINYMOCk_FUNCTION_DESTROY(tinymock_installed_box_copy);
  TINYMOCk_FUNCTION_DESTROY(tinymock_installed_apply_callback);
  TINYMOCk_FUNCTION_DESTROY(tinymock_installed_callback_answer);
  return 0;
}
