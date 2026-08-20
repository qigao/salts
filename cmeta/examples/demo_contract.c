#include <cmeta/cmeta.h>
#include "ops.h"

#include <stdio.h>

int main(void) {
    cmeta_callable assoc = add_long.fn;
    cmeta_callable bound;
    if (!cmeta_callable_bind(assoc, &bound)) return 1;
    if (!cmeta_properties_include(bound.meta.properties, CMETA_PROP_ASSOCIATIVE)) return 2;

    /* ASSOCIATIVE is only meaningful for a binary endomorphism T(T,T)->T. */
    cmeta_callable bad_shape = clamp_nonnegative.fn;
    bad_shape.meta.properties |= CMETA_PROP_ASSOCIATIVE;
    if (cmeta_callable_contract_valid(bad_shape)) return 3;

    /* TOTAL and MAY_FAIL are contradictory admission claims. */
    cmeta_callable contradiction = assoc;
    contradiction.meta.effects |= CMETA_EFFECT_MAY_FAIL;
    if (cmeta_callable_contract_valid(contradiction)) return 4;

    printf("semantic contract: associative reducer admitted; invalid claims rejected\n");
    return 0;
}
