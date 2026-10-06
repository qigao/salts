#include <cmeta/manifest_view.h>
static const cmeta_type_desc Invalid_interface_meta = {
    "Invalid", sizeof(int), CMETA_ALIGNOF(int), CMETA_T_INTEGER, NULL, NULL, NULL
};
cmeta_plugin(InvalidPlugin, cmeta_provides(Invalid));
