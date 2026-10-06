#include <salts/plugin_decl.h>
typedef int array_parameter[2];
FunctionInvokeDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,array_input,
    (array_parameter,input,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
int array_input(array_parameter input) { return input[0]; }
