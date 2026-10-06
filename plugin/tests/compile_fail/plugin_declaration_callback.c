#include <salts/plugin_decl.h>
Function0InvokeDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,callback_probe);
int callback_probe(void) { return 1; }
static int state;
static void bad_start(void *self) { (void)self; }
static salts_plugin_status SALTS_PLUGIN_CALL stop(void *self) { (void)self; return SALTS_PLUGIN_OK; }
static bool SALTS_PLUGIN_CALL quiet(const void *self) { (void)self; return true; }
static void SALTS_PLUGIN_CALL destroy(void *self) { (void)self; }
#define EXPORTS(X) X(function,callback_probe,"bad","bad",1,0)
SALTS_PLUGIN_DECLARE(bad,"bad",(1,0,0),EXPORTS,
    SALTS_PLUGIN_LIFECYCLE(&state,bad_start,stop,quiet,destroy));
