#ifndef CFLOW_CLOCK_H
#define CFLOW_CLOCK_H

#include <cflow/time.h>
#include <cmeta/interface.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CMETA_CLOCK_CAP_MANUAL = 1u << 0
};

#define CMETA_CLOCK_METHODS(X,I) \
    X(I,F0,cflow_instant,now,stateful, \
      &cmeta_type_cflow_instant,CMETA_ABI_AGGREGATE) \
    X(I,F1,bool,advance,stateful, \
      &cmeta_type_bool,CMETA_ABI_SCALAR, \
      (cflow_duration,delta,CMETA_PARAM_IN, \
       &cmeta_type_cflow_duration,CMETA_ABI_AGGREGATE)) \
    X(I,F0,void,destroy,stateful, \
      &cmeta_type_void,CMETA_ABI_VOID)
CMETA_INTERFACE(cflow_clock, CMETA_CLOCK_METHODS);

bool cflow_clock_system_init(cflow_clock *clock);
bool cflow_clock_virtual_init(cflow_clock *clock, cflow_instant start);

#ifdef __cplusplus
}
#endif
#endif /* CFLOW_CLOCK_H */
