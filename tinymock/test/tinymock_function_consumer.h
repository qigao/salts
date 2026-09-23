#ifndef TINYMOCK_FUNCTION_CONSUMER_H
#define TINYMOCK_FUNCTION_CONSUMER_H

#include <stddef.h>

int tinymock_function_consumer_run(int input);
int tinymock_function_consumer_pointer(int *value);
int tinymock_function_consumer_write_size(int input, size_t *written);
int tinymock_function_consumer_adjust_int(int *value);
int tinymock_function_consumer_unknown_ptr(int *value);
int tinymock_function_consumer_nullable_out(size_t *written);

#endif /* TINYMOCK_FUNCTION_CONSUMER_H */
