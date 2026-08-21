#define Struct(...) 201
#define StructMeta(...) 202
#define FieldCount(...) 203
#define FieldMeta(...) 204
#define FieldFind(...) 205

#include "tlog.h"
#include "tinytest.h"

#include <cstddef>

enum {
  host_struct_semantics = Struct(host_record, (int, value)),
  host_struct_meta_semantics = StructMeta(host_record),
  host_field_count_semantics = FieldCount(host_record),
  host_field_meta_semantics = FieldMeta(host_record, 0),
  host_field_find_semantics = FieldFind(host_record, value)
};

static_assert(host_struct_semantics == 201, "tlog.h replaced the host Struct macro");
static_assert(host_struct_meta_semantics == 202, "tlog.h replaced the host StructMeta macro");
static_assert(host_field_count_semantics == 203, "tlog.h replaced the host FieldCount macro");
static_assert(host_field_meta_semantics == 204, "tlog.h replaced the host FieldMeta macro");
static_assert(host_field_find_semantics == 205, "tlog.h replaced the host FieldFind macro");

struct turbo_log_entry_collision_layout {
  turbo_log_level_t level;
  uint64_t timestamp_ms;
  uint32_t thread_id;
  const char *component;
  const char *file;
  int line;
  const char *message;
  size_t message_len;
};

static_assert(sizeof(turbo_log_entry_t) == sizeof(turbo_log_entry_collision_layout),
              "turbo_log_entry_t size changed under host macros");
static_assert(alignof(turbo_log_entry_t) == alignof(turbo_log_entry_collision_layout),
              "turbo_log_entry_t alignment changed under host macros");
static_assert(offsetof(turbo_log_entry_t, message_len) ==
                  offsetof(turbo_log_entry_collision_layout, message_len),
              "turbo_log_entry_t layout changed under host macros");

spec("TLog C++ public-header collisions") {
  it("preserves host macros and exposes log entry metadata") {
    const cmeta_struct_desc *meta = turbo_log_entry_t_meta();

    check_not_null(meta);
    check(meta->name != nullptr);
    check(meta->field_count == static_cast<size_t>(8));
    check(meta->fields[0].offset == offsetof(turbo_log_entry_t, level));
    check(meta->fields[7].offset == offsetof(turbo_log_entry_t, message_len));
    check(host_struct_semantics == 201);
    check(host_field_find_semantics == 205);
  }
}
