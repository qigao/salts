#define TINYTEST_NO_MAIN 1
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>

typedef struct tinymock_unsupported_packet {
  int value;
} tinymock_unsupported_packet;

static const cmeta_type_desc tinymock_unsupported_packet_type = {
  .name = "tinymock_unsupported_packet",
  .size = sizeof(tinymock_unsupported_packet),
  .align = _Alignof(tinymock_unsupported_packet),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = NULL,
  .identity = NULL
};

FunctionDeclAs(
    value,
    int,
    &cmeta_type_int,
    tinymock_unsupported_by_value_parameter,
    (tinymock_unsupported_packet, packet, CMETA_PARAM_IN,
     &tinymock_unsupported_packet_type));
