#include <salts/plugin_decl.h>
Function0InvokeDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,capability_probe);
int capability_probe(void) { return 1; }
#define EXPORTS(X) X(function,capability_probe,"bad","bad",1,-1)
SALTS_PLUGIN_DECLARE(bad,"bad",(1,0,0),EXPORTS,SALTS_PLUGIN_PASSIVE());
