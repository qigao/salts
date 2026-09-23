#ifndef CMETA_INTERFACE_REFLECTION_FIXTURE_H
#define CMETA_INTERFACE_REFLECTION_FIXTURE_H

#include <cmeta/interface.h>

#define CMETA_INTERFACE_REFLECTION_METHODS(X, I) \
    X(I,F1,int,add,value,&cmeta_type_int,CMETA_ABI_SCALAR, \
      (int, delta, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR)) \
    X(I,F0,int,value,value,&cmeta_type_int,CMETA_ABI_SCALAR) \
    X(I,F1,void,reset_to,stateful,&cmeta_type_void,CMETA_ABI_VOID, \
      (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR))

CMETA_INTERFACE(cmeta_reflection_counter,
                CMETA_INTERFACE_REFLECTION_METHODS);

#endif /* CMETA_INTERFACE_REFLECTION_FIXTURE_H */
