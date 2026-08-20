#include <cmeta/cmeta.h>
#include "tinytest.h"

spec("CMeta Core Tests") {
  it("should expose builtin type metadata") {
    const cmeta_type_desc *type = cmeta_type_find("int");

    check_not_null(type);
    check_equal(cmeta_type_equal(type, &cmeta_type_int), 1);
    check_size_ge(cmeta_type_registry_count(), 5);
    check_null(cmeta_type_find("missing-type"));
    check_null(cmeta_type_registry_at(cmeta_type_registry_count()));
  }
}
