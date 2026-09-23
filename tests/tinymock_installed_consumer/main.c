#define TINYTEST_NO_MAIN
#include <assert.h>
#include <string.h>

#include <cmeta/function.h>
#include <cflow/adapters.h>
#include <cflow/function_projection.h>
#include <tinymock_function.h>
#include <tinymock_cmeta.h>

#include "api.h"

TINYMOCk_INTERFACE(tinymock_installed_interface,
                   TINYMOCK_INSTALLED_INTERFACE_METHODS);

CFLOW_REFLECTED_ADAPTER(tinymock_installed_projected);

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
tinymock_installed_mode
tinymock_installed_consumer_mode(tinymock_installed_mode input);

TINYMOCk_FUNCTION_DECLARE(tinymock_installed_add);
TINYMOCk_FUNCTION_DECLARE(tinymock_installed_shutdown);
TINYMOCk_FUNCTION_DECLARE(tinymock_installed_box_copy);
TINYMOCk_FUNCTION_DECLARE(tinymock_installed_apply_callback);
TINYMOCk_FUNCTION_DECLARE(tinymock_installed_callback_answer);
TINYMOCk_FUNCTION_DECLARE(tinymock_installed_mode_echo);

int main(void) {
  const cmeta_function_desc *meta;

  {
    cflow_function_projection projection = {0};
    cflow_graph graph = {0};
    cflow_result result = {0};
    const int input[] = {1, 2};
    const int expected[] = {31, 32};

    assert(cflow_function_projection_admit(
        FunctionMeta(tinymock_installed_projected),
        FunctionAbi(tinymock_installed_projected),
        CFLOW_REFLECTED_CALLABLE(tinymock_installed_projected),
        CFLOW_OP_MAP,
        &projection) == CFLOW_FUNCTION_PROJECTION_OK);
    cflow_graph_init(&graph, &cmeta_type_int);
    assert(cflow_graph_add_function_projection(&graph, &projection));
    assert(cflow_eval_array(&graph, input, 2u, &result));
    assert(result.count == 2u);
    assert(cmeta_type_equal(result.type, &cmeta_type_int));
    assert(memcmp(result.data, expected, sizeof(expected)) == 0);
    cflow_result_destroy(&result);
    cflow_graph_destroy(&graph);
  }

  {
    const cmeta_interface_desc *iface_meta =
        tinymock_installed_interface_interface();
    const cmeta_interface_method_desc *method;
    tinymock_tinymock_installed_interface mock;
    tinymock_installed_interface iface;
    tinymock_installed_box input = {6};
    tinymock_installed_box expected = {6};
    tinymock_installed_box scripted = {42};
    tinymock_installed_box result;

    assert(cmeta_interface_desc_valid(iface_meta));
    assert(iface_meta->method_count == 2u);
    method = &iface_meta->methods[0];
    assert(cmeta_interface_method_reflection_valid(method));
    assert(method->function == tinymock_installed_interface_apply_function());
    assert(method->abi == tinymock_installed_interface_apply_function_abi());
    assert(method->function->param_count == 1u);
    assert(method->abi->return_carrier == CMETA_ABI_SCALAR);

    method = &iface_meta->methods[1];
    assert(cmeta_interface_method_reflection_valid(method));
    assert(method->function ==
           tinymock_installed_interface_map_box_function());
    assert(method->abi->return_carrier == CMETA_ABI_AGGREGATE);

    tinymock_tinymock_installed_interface_init(&mock);
    iface = tinymock_tinymock_installed_interface_as_interface(&mock);

    tinymock_mock_set_default_return(
        TINYMOCk_INTERFACE_METHOD(&mock, apply),
        TINYMOCk_RETURN(31));
    assert(tinymock_installed_interface_apply(&iface, 3) == 31);

    assert(TINYMOCk_INTERFACE_SET_RETURN(&mock, map_box, scripted));
    result = tinymock_installed_interface_map_box(&iface, input);
    assert(result.value == 42);
    assert(TINYMOCk_INTERFACE_ARG_EQUAL_TYPED(
        &mock, map_box, 0u, "input", expected));

    tinymock_tinymock_installed_interface_destroy(&mock);
  }

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

  {
    tinymock_installed_mode input = TINYMOCK_INSTALLED_MODE_READY;
    tinymock_installed_mode expected = TINYMOCK_INSTALLED_MODE_READY;
    tinymock_installed_mode scripted = TINYMOCK_INSTALLED_MODE_DONE;
    tinymock_installed_mode result;

    TINYMOCk_FUNCTION_RESET(tinymock_installed_mode_echo);
    assert(TINYMOCk_FUNCTION_SET_RETURN(
        tinymock_installed_mode_echo, scripted));

    result = tinymock_installed_consumer_mode(input);
    assert(result == TINYMOCK_INSTALLED_MODE_DONE);
    assert(TINYMOCk_FUNCTION_ARG_EQUAL_TYPED(
        tinymock_installed_mode_echo, 0u, "input", expected));
    assert(TINYMOCk_FUNCTION_ABI(
        tinymock_installed_mode_echo)->return_carrier ==
        CMETA_ABI_ENUM);
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
  TINYMOCk_FUNCTION_DESTROY(tinymock_installed_mode_echo);
  return 0;
}
