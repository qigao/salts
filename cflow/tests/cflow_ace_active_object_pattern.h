#ifndef CFLOW_ACE_ACTIVE_OBJECT_PATTERN_H
#define CFLOW_ACE_ACTIVE_OBJECT_PATTERN_H

#include <cflow/actor.h>
#include <cmeta/interface.h>

/*
 * ACE Active Object conformance only. These borrowed CMeta Interfaces model
 * typed synchronous Strategy dispatch; CFlow Actor retains the sole mailbox,
 * producer admission, scheduling, and lifecycle authority.
 */
#define ACE_ACTOR_PORT_METHODS(X, I) \
    X(I,FR1,int,try_send,stateful, \
      &cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (int,payload,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR))

#define ACE_ACTOR_STRATEGY_METHODS(X, I) \
    X(I,FR1,int,transform,stateful, \
      &cmeta_type_int,CMETA_ABI_SCALAR,CMETA_RESULT_VALUE, \
      (int,payload,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR))

CMETA_INTERFACE(ace_actor_port, ACE_ACTOR_PORT_METHODS);
CMETA_INTERFACE(ace_actor_strategy, ACE_ACTOR_STRATEGY_METHODS);

#endif /* CFLOW_ACE_ACTIVE_OBJECT_PATTERN_H */
