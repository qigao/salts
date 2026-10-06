#include <salts/plugin_linker.h>
#include "plugin_test_interface.h"
#include "plugin_linker_fixture.h"

FunctionInvokeDeclAsAbi(value,int,&cmeta_type_int,CMETA_ABI_SCALAR,linker_double,
    (int,value,CMETA_PARAM_IN,&cmeta_type_int,CMETA_ABI_SCALAR));
int linker_double(int value) { return value + value; }
static int linker_transform(void *self, int value) {
    return value + *(const int *)self;
}
static int linker_bias = PLUGIN_LINKER_BIAS;
static const plugin_test_codec_vtable linker_vtable = {"linker_codec", 1u, linker_transform};
static plugin_test_codec linker_codec = {&linker_bias, &linker_vtable};
#define LINKER_B_EXPORTS(X) \
    X(function,linker_double,"double","math",1u,1u) \
    X(interface,(plugin_test_codec,&linker_codec),"codec","codec",1u,1u)
SALTS_PLUGIN_EXPORT_FRAGMENT(linker_part_b,LINKER_B_EXPORTS);
