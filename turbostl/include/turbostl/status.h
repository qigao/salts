#ifndef TURBOSTL_STATUS_H
#define TURBOSTL_STATUS_H

typedef enum stl_status {
  STL_OK = 0,
  STL_INVALID_ARGUMENT,
  STL_OUT_OF_MEMORY,
  STL_CAPACITY_EXCEEDED,
  STL_EMPTY,
  STL_NOT_FOUND,
  STL_TYPE_MISMATCH,
  STL_TRAIT_MISSING
} stl_status;

/* Temporary repository-migration bridge. Remove after all callers use STL_*.
 * The final installed API must not retain these legacy aliases. */
typedef stl_status turbo_stl_status;
#define TURBO_STL_OK STL_OK
#define TURBO_STL_INVALID_ARGUMENT STL_INVALID_ARGUMENT
#define TURBO_STL_OUT_OF_MEMORY STL_OUT_OF_MEMORY
#define TURBO_STL_CAPACITY_EXCEEDED STL_CAPACITY_EXCEEDED
#define TURBO_STL_EMPTY STL_EMPTY
#define TURBO_STL_NOT_FOUND STL_NOT_FOUND
#define TURBO_STL_TYPE_MISMATCH STL_TYPE_MISMATCH
#define TURBO_STL_TRAIT_MISSING STL_TRAIT_MISSING

#endif /* TURBOSTL_STATUS_H */
