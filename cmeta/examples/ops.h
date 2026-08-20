#ifndef OPS_H
#define OPS_H

#include <cflow/meta.h>

typed_decl(filter, even);
typed_decl(map, square);
typed_decl(map, half);
typed_decl(flatMap, expand_long);
typed_decl(flatMap, fail_long);
typed_decl(reduce, add_long);
typed_decl(zip, merge_long_double);
typed_decl(map, as_double);
typed_decl(map, to_int);
typed_decl(map, times_ten);
typed_decl(map, plus_hundred);
typed_decl(transform, times_two_transform);
typed_decl(map, io_tagged);
typed_decl(map, may_fail_tagged);
typed_decl(map, clamp_nonnegative);
typed_decl(map, clamp_unproven);
typed_decl(map, unproven_square);

#endif
