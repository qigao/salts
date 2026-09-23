#include <cmeta/interface.h>
#include "tinytest.h"

#define CMETA_DIRECT_INTERFACE_METHODS(X,I) \
    X(I,F1,int,apply,value, \
      &cmeta_type_int,CMETA_ABI_SCALAR, \
      (int,value,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR))

CMETA_INTERFACE(cmeta_direct_interface, CMETA_DIRECT_INTERFACE_METHODS);

suite("CMeta direct interface header") {
  it("sees complete function reflection before interface generation") {
    const cmeta_interface_desc *meta = cmeta_direct_interface_interface();

    check_true(cmeta_interface_desc_valid(meta));
    check_true(cmeta_interface_method_reflection_valid(&meta->methods[0]));
    check_equal(meta->methods[0].function->name,
                "cmeta_direct_interface.apply");
  }
}
