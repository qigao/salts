#include <salts/plugin_decl.h>
Function0InvokeDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,invalid_version);
int invalid_version(void) { return 1; }
#define EXPORTS(X) X(function,invalid_version,"bad","bad",0,0)
SALTS_PLUGIN_DECLARE(bad,"bad",(1,0,0),EXPORTS,SALTS_PLUGIN_PASSIVE());
