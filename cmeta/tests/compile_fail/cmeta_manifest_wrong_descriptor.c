#include <cmeta/manifest_view.h>
cmeta_registry(wrong, cmeta_manifest_struct_entry("wrong", &cmeta_type_int));
int main(void) { return wrong.count == 0u; }
