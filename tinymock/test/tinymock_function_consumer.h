#ifndef TINYMOCK_FUNCTION_CONSUMER_H
#define TINYMOCK_FUNCTION_CONSUMER_H

#include <stddef.h>
#include "tinymock_function_fixture.h"

int tinymock_function_consumer_run(int input);
int tinymock_function_consumer_pointer(int *value);
int tinymock_function_consumer_write_size(int input, size_t *written);
int tinymock_function_consumer_adjust_int(int *value);
int tinymock_function_consumer_unknown_ptr(int *value);
int tinymock_function_consumer_nullable_out(size_t *written);
void tinymock_function_consumer_notify(int event, size_t *written);
void tinymock_function_consumer_shutdown(void);
tinymock_fixture_box tinymock_function_consumer_box(tinymock_fixture_box input);
int *tinymock_function_consumer_pointer_answer(void);

#endif /* TINYMOCK_FUNCTION_CONSUMER_H */
