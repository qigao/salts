#include <salts/plugin_decl.h>
FunctionInvokeDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,missing_parameter_abi,
    (int,input,CMETA_PARAM_IN,&cmeta_type_int));
int missing_parameter_abi(int input) { return input; }
