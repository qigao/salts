#include "tlog.h"
#include "tinytest.h"
#include <cstddef>

spec("TLog vstr ABI C++ contract") {
  it("exposes view-native log entry metadata") {
    const cmeta_struct_desc *meta = salts_log_entry_t_meta();

    check_not_null(meta);
    check(meta->field_count == static_cast<size_t>(7));
    check(meta->fields[3].offset == offsetof(salts_log_entry_t, component));
    check(meta->fields[4].offset == offsetof(salts_log_entry_t, file));
    check(meta->fields[6].offset == offsetof(salts_log_entry_t, message));
    check(cmeta_struct_find_field(meta, "message_len") == nullptr);
    check(meta->fields[3].size == sizeof(vstr));
    check(meta->fields[4].size == sizeof(vstr));
    check(meta->fields[6].size == sizeof(vstr));
  }
}
