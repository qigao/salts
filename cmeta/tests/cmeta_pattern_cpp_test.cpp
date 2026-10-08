#include "tinytest.hpp"
#include "cmeta_pattern_cases.h"

#include <type_traits>

/* C++ lexical ObjectRef ownership cannot be accidentally copied. */
static_assert(!std::is_copy_constructible<cmeta::object_scope>::value,
              "ACE ObjectRef scope must not be copy-constructible");
static_assert(!std::is_copy_assignable<cmeta::object_scope>::value,
              "ACE ObjectRef scope must not be copy-assignable");
static_assert(!std::is_move_assignable<cmeta::object_scope>::value,
              "ACE ObjectRef scope must not silently rebind its owner");
