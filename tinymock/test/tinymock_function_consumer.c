#include "tinymock_function_consumer.h"
#include "tinymock_function_fixture.h"

int tinymock_function_consumer_run(int input) {
  return tinymock_fixture_add(input, tinymock_fixture_answer());
}

int tinymock_function_consumer_pointer(int *value) {
  return tinymock_fixture_pointer(value);
}

int tinymock_function_consumer_write_size(int input, size_t *written) {
  return tinymock_fixture_write_size(input, written);
}

int tinymock_function_consumer_adjust_int(int *value) {
  return tinymock_fixture_adjust_int(value);
}

int tinymock_function_consumer_unknown_ptr(int *value) {
  return tinymock_fixture_unknown_ptr(value);
}

int tinymock_function_consumer_nullable_out(size_t *written) {
  return tinymock_fixture_nullable_out(written);
}

void tinymock_function_consumer_notify(int event, size_t *written) {
  tinymock_fixture_notify(event, written);
}

void tinymock_function_consumer_shutdown(void) {
  tinymock_fixture_shutdown();
}

tinymock_fixture_box
tinymock_function_consumer_box(tinymock_fixture_box input) {
  return tinymock_fixture_box_copy(input);
}

int *tinymock_function_consumer_pointer_answer(void) {
  return tinymock_fixture_pointer_answer();
}

int tinymock_function_consumer_apply_callback(
    tinymock_fixture_callback callback, int value) {
  return tinymock_fixture_apply_callback(callback, value);
}

tinymock_fixture_callback tinymock_function_consumer_callback_answer(void) {
  return tinymock_fixture_callback_answer();
}

tinymock_fixture_mode
tinymock_function_consumer_mode(tinymock_fixture_mode input) {
  return tinymock_fixture_mode_echo(input);
}
