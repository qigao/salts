#ifndef CMETA_INTERFACE_FUNCTION_FIXTURE_H
#define CMETA_INTERFACE_FUNCTION_FIXTURE_H

#include <cmeta/interface.h>

#define CMETA_REFLECTED_COUNTER_METHODS(X,I) \
    X(I,F1,int,add,value, \
      &cmeta_type_int,CMETA_ABI_SCALAR, \
      (int,delta,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR)) \
    X(I,F0,int,value,value, \
      &cmeta_type_int,CMETA_ABI_SCALAR) \
    X(I,FV0,void,reset,stateful, \
      &cmeta_type_void,CMETA_ABI_VOID)

CMETA_INTERFACE(cmeta_reflected_counter, CMETA_REFLECTED_COUNTER_METHODS);

#define CMETA_REFLECTED_OWNER_METHODS(X,I) \
    X(I,FD0,void,destroy,stateful, \
      &cmeta_type_void,CMETA_ABI_VOID)

CMETA_INTERFACE(cmeta_reflected_owner, CMETA_REFLECTED_OWNER_METHODS);

#endif /* CMETA_INTERFACE_FUNCTION_FIXTURE_H */
