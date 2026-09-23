#include <cmeta/interface.h>

#include <string.h>

bool cmeta_interface_desc_equal(const cmeta_interface_desc *left,
                                const cmeta_interface_desc *right) {
    size_t i;

    if (left == right)
        return left != NULL && cmeta_interface_desc_valid(left);
    if (!cmeta_interface_desc_valid(left) ||
        !cmeta_interface_desc_valid(right) ||
        strcmp(left->name, right->name) != 0 ||
        left->method_count != right->method_count)
        return false;

    for (i = 0u; i < left->method_count; ++i) {
        const cmeta_interface_method_desc *a = &left->methods[i];
        const cmeta_interface_method_desc *b = &right->methods[i];

        if (strcmp(a->name, b->name) != 0 ||
            a->dispatch_arity != b->dispatch_arity ||
            a->flags != b->flags)
            return false;

        if ((a->function == NULL) != (b->function == NULL) ||
            (a->abi == NULL) != (b->abi == NULL))
            return false;

        if (a->function != NULL &&
            (!cmeta_function_desc_equal(a->function, b->function) ||
             !cmeta_function_abi_desc_equal(a->abi, b->abi)))
            return false;
    }

    return true;
}
