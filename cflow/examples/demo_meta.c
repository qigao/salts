#include <cmeta/meta.h>
#include "demo_meta_schema.h"
#include <cflow/graph.h>
#include <cflow/reactive.h>
#include <cflow/publishers.h>
#include "ops.h"

#include <stdio.h>
#include <string.h>

_Static_assert(CMETA_CONTRACT_EFFECTS(value) == CMETA_EFFECT_PURE,
               "value contract must be pure");
_Static_assert((CMETA_CONTRACT_PROPERTIES(value) & CMETA_PROP_TOTAL) != 0,
               "value contract must be total");
_Static_assert((CMETA_CONTRACT_PROPERTIES(idempotent) & CMETA_PROP_IDEMPOTENT) != 0,
               "idempotent contract must carry the law");
_Static_assert((CMETA_CONTRACT_EFFECTS(io) & CMETA_EFFECT_IO) != 0,
               "io contract must carry IO effect");

#define DEMO_REPEAT_TERM(i, ctx) + ((i) + 1)
enum { DEMO_REPEAT_SUM = 0 CMETA_PP_REPEAT(4, DEMO_REPEAT_TERM, ~) };
_Static_assert(DEMO_REPEAT_SUM == 10, "CMETA_PP_REPEAT must expand indexed repetitions");

#define DEMO_INDEXED_VALUE(i, item, ctx) [i] = (item),
static const int demo_indexed_values[] = {
    CMETA_PP_FOR_EACH_I(DEMO_INDEXED_VALUE, ~, 11, 22, 33, 44)
};

static int check_custom_enum(void) {
    demo_color c = DEMO_RED;
    demo_http_status h = DEMO_HTTP_OK;
    const cmeta_enum_desc *m;
    if (!demo_color_from_string("green", &c) || c != DEMO_GREEN) return 0;
    if (!demo_color_from_string("DEMO_BLUE", &c) || c != DEMO_BLUE) return 0;
    if (strcmp(demo_color_to_string(c), "blue") != 0) return 0;
    if (strcmp(demo_color_to_symbol(c), "DEMO_BLUE") != 0) return 0;
    m = demo_color_meta();
    if (!m || strcmp(m->name, "demo_color") != 0 || m->count != 3u) return 0;
    if (!demo_http_status_from_string("404", &h)) {
        /* Numeric parsing is intentionally not implicit. */
        if (!demo_http_status_from_string("not_found", &h)) return 0;
    }
    if (h != DEMO_HTTP_NOT_FOUND || strcmp(demo_http_status_to_string(h), "not_found") != 0)
        return 0;
    if ((int)DEMO_HTTP_NOT_FOUND != 404) return 0;
    return 1;
}

static int check_struct_meta(void) {
    demo_point p = { 3, 4 };
    const cmeta_struct_desc *m = StructMeta(demo_point);
    const cmeta_field_desc *x = FieldFind(demo_point, "x");
    const cmeta_field_desc *y = FieldMeta(demo_point, 1u);
    if (p.x != 3 || p.y != 4 || !m || m->field_count != 2u) return 0;
    if (!x || !y || strcmp(x->type_name, "int") != 0) return 0;
    if (x->offset != offsetof(demo_point, x) || y->offset != offsetof(demo_point, y)) return 0;
    if (x->size != sizeof(int) || y->align != _Alignof(int)) return 0;
    return 1;
}

static int check_pp_kernel(void) {
    return DEMO_REPEAT_SUM == 10 &&
           sizeof(demo_indexed_values) / sizeof(demo_indexed_values[0]) == 4u &&
           demo_indexed_values[0] == 11 && demo_indexed_values[3] == 44;
}

static int check_builtin_enum_meta(void) {
    cflow_relation_coordination rel = CFLOW_REL_COORD_ALL;
    cflow_step_kind step = CFLOW_STEP_VALUE;
    cflow_read_status read = CFLOW_READ_VALUE;
    if (!cflow_relation_coordination_from_string("latest", &rel) ||
        rel != CFLOW_REL_COORD_LATEST) return 0;
    if (strcmp(cflow_relation_coordination_to_symbol(rel), "CFLOW_REL_COORD_LATEST") != 0)
        return 0;
    if (strcmp(cflow_relation_coordination_to_string(rel), "latest") != 0) return 0;
    if (!cflow_step_kind_from_string("wait", &step) || step != CFLOW_STEP_WAIT) return 0;
    if (!cflow_read_status_from_string("would_block", &read) ||
        read != CFLOW_READ_WOULD_BLOCK) return 0;
    if (cmeta_type_kind_meta()->count != 6u) return 0;
    if (strcmp(cmeta_gen_status_to_string(CMETA_GEN_VALUE_AND_DONE), "value_and_done") != 0)
        return 0;
    if (cmeta_type_registry_count() < 5u || cmeta_type_find("int") != &cmeta_type_int) return 0;
    {
        cmeta_callable bound;
        if (!cmeta_callable_bind(square.fn, &bound)) return 0;
        if (!cmeta_sig_to_string(bound.meta.sig) || !cmeta_sig_to_symbol(bound.meta.sig)) return 0;
    }
    {
        const cmeta_contract_desc *d = cmeta_contract_find("idempotent");
        if (!d || !(d->properties & CMETA_PROP_IDEMPOTENT)) return 0;
    }
    return 1;
}

int main(void) {
    if (!check_custom_enum()) return 1;
    if (!check_builtin_enum_meta()) return 2;
    if (!check_struct_meta()) return 3;
    if (!check_pp_kernel()) return 4;
    printf("C Meta Enum: single declaration -> enum + strings + parse + descriptor PASS\n");
    printf("C Meta Struct: single declaration -> fields + offsets + descriptor PASS\n");
    printf("C Meta contracts: value/idempotent/io aliases -> IR effects/properties PASS\n");
    printf("C Meta PP kernel: repeat + indexed foreach PASS\n");
    return 0;
}
