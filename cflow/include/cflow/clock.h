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
    X(I,R0,cflow_instant,now,_) \
    X(I,R1,bool,advance,cflow_duration,delta) \
    X(I,V0,void,destroy,_)
CMETA_INTERFACE(cflow_clock, CMETA_CLOCK_METHODS);

bool cflow_clock_system_init(cflow_clock *clock);
bool cflow_clock_virtual_init(cflow_clock *clock, cflow_instant start);

#ifdef __cplusplus
}
#endif
#endif /* CFLOW_CLOCK_H */
