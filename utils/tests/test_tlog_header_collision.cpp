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

struct salts_log_entry_collision_layout {
  salts_log_level_t level;
  uint64_t timestamp_ms;
  uint32_t thread_id;
  vstr component;
  vstr file;
  int line;
  vstr message;
};

static_assert(sizeof(salts_log_entry_t) == sizeof(salts_log_entry_collision_layout),
              "salts_log_entry_t size changed under host macros");
static_assert(alignof(salts_log_entry_t) == alignof(salts_log_entry_collision_layout),
              "salts_log_entry_t alignment changed under host macros");
static_assert(offsetof(salts_log_entry_t, message) ==
                  offsetof(salts_log_entry_collision_layout, message),
              "salts_log_entry_t layout changed under host macros");

spec("TLog C++ public-header collisions") {
  it("preserves host macros and exposes log entry metadata") {
    const cmeta_struct_desc *meta = salts_log_entry_t_meta();

    check_not_null(meta);
    check(meta->name != nullptr);
    check(meta->field_count == static_cast<size_t>(7));
    check(meta->fields[0].offset == offsetof(salts_log_entry_t, level));
    check(meta->fields[6].offset == offsetof(salts_log_entry_t, message));
    check(meta->fields[3].size == sizeof(vstr));
    check(meta->fields[4].size == sizeof(vstr));
    check(meta->fields[6].size == sizeof(vstr));
    check(cmeta_struct_find_field(meta, "message_len") == nullptr);
    check(host_struct_semantics == 201);
    check(host_field_find_semantics == 205);
  }
}
