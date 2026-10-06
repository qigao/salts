#include <salts/plugin_linker.h>

FunctionInvokeDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,linker_increment,
    (int,value,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
int linker_increment(int value) { return value + 1; }
#define LINKER_A_EXPORTS(X) X(function,linker_increment,"increment","math",1u,1u)
SALTS_PLUGIN_EXPORT_FRAGMENT(linker_part_a,LINKER_A_EXPORTS);
