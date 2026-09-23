#include "tinymock_function_consumer.h"
#include "tinymock_function_fixture.h"

int tinymock_function_consumer_run(int input) {
  return tinymock_fixture_add(input, tinymock_fixture_answer());
}
