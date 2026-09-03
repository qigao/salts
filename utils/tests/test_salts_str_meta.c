#include "salts_str.h"
#include "salts_vstr.h"
#include "tinytest.h"

#include <stddef.h>

typedef struct vstr_legacy_layout {
  const char *data;
  size_t len;
} vstr_legacy_layout;

const cmeta_struct_desc *vstr_meta_from_peer(void);

_Static_assert(sizeof(vstr) == sizeof(vstr_legacy_layout),
               "vstr metadata must preserve size");
_Static_assert(CMETA_ALIGNOF(vstr) == CMETA_ALIGNOF(vstr_legacy_layout),
               "vstr metadata must preserve alignment");
_Static_assert(offsetof(vstr, data) == offsetof(vstr_legacy_layout, data),
               "vstr metadata must preserve data offset");
_Static_assert(offsetof(vstr, len) == offsetof(vstr_legacy_layout, len),
               "vstr metadata must preserve len offset");

spec("SaltsStr CMeta") {
  it("exposes stable vstr field metadata across translation units") {
    const cmeta_struct_desc *local = vstr_meta();
    const cmeta_struct_desc *peer = vstr_meta_from_peer();

    check_not_null(local);
    check_not_null(peer);
    check_equal(local->name, "vstr");
    check_equal(peer->name, "vstr");
    check_equal(local->size, sizeof(vstr));
    check_equal(peer->size, sizeof(vstr));
    check_equal(local->align, CMETA_ALIGNOF(vstr));
    check_equal(peer->align, CMETA_ALIGNOF(vstr));
    check_equal(local->field_count, (size_t)2);
    check_equal(peer->field_count, (size_t)2);
    check_equal(local->fields[0].name, "data");
    check_equal(local->fields[0].offset, offsetof(vstr, data));
    check_equal(local->fields[1].name, "len");
    check_equal(local->fields[1].offset, offsetof(vstr, len));
    check_equal(peer->fields[0].offset, local->fields[0].offset);
    check_equal(peer->fields[1].offset, local->fields[1].offset);
  }

  it("rejects an invalid view instead of copying it as an empty string") {
    const vstr invalid = {NULL, 3};
    tstr copy = tstr_from_v(invalid);

    check_null(copy);
    tstr_free(copy);
  }

  it("preserves owned strings when append or copy receives an invalid view") {
    const vstr invalid = {NULL, 3};
    tstr owned = tstr_dup("keep");
    tstr updated = tstr_cat_v(owned, invalid);

    check_equal((const void *)updated, (const void *)owned);
    check_equal(updated, "keep");

    updated = tstr_cpy_v(owned, invalid);
    check_equal((const void *)updated, (const void *)owned);
    check_equal(updated, "keep");
    check_null(tstr_cat_v(NULL, invalid));
    check_null(tstr_cpy_v(NULL, invalid));

    tstr_free(owned);
  }

  it("compares an invalid view as unequal without dereferencing it") {
    const vstr invalid = {NULL, 3};
    tstr owned = tstr_dup("abc");

    check(tstr_cmp_v(owned, invalid) != 0);

    tstr_free(owned);
  }

  it("reports whether a view has a usable data and length pair") {
    const vstr empty = {NULL, 0};
    const vstr valid = {"abc", 3};
    const vstr invalid = {NULL, 3};

    check_equal(vstr_is_valid(empty), 1);
    check_equal(vstr_is_valid(valid), 1);
    check_equal(vstr_is_valid(invalid), 0);
  }

  it("constructs and searches through the vstr public API") {
    const vstr value = vstr_from_cstr("abc");

    check_equal(vstr_len(value), (size_t)3);
    check_equal(vstr_find(value, vstr_from_cstr("z")), VSTR_NPOS);
  }
}
