#include <cmeta/pp.h>
#define DECL(type,name,context) type name;
CMETA_PP_PAIR_MAP_N(2,DECL,~,int,first,int)
