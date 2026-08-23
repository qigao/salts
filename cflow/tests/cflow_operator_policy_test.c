#include <cflow/cflow.h>
#include "tinytest.h"

suite("CFlow generated operator policy") {
    it("preserves built-in operator cardinalities") {
        check_equal((size_t)CFLOW_BUILTIN_FILTER_SIGNATURE_COUNT, (size_t)1u);
        check_equal((size_t)CFLOW_BUILTIN_MAP_SIGNATURE_COUNT, (size_t)7u);
        check_equal((size_t)CFLOW_BUILTIN_TRANSFORM_SIGNATURE_COUNT, (size_t)1u);
        check_equal((size_t)CFLOW_BUILTIN_FLAT_MAP_SIGNATURE_COUNT, (size_t)1u);
        check_equal((size_t)CFLOW_BUILTIN_REDUCE_SIGNATURE_COUNT, (size_t)1u);
        check_equal((size_t)CFLOW_BUILTIN_ZIP_SIGNATURE_COUNT, (size_t)1u);
    }

    it("admits built-ins only for their intended operators") {
        check_true(cflow_op_signature_allowed(CFLOW_OP_FILTER,
                                              CMETA_SIG_U_I_B));
        check_true(cflow_op_signature_allowed(CFLOW_OP_MAP,
                                              CMETA_SIG_U_I_I));
        check_true(cflow_op_signature_allowed(CFLOW_OP_TRANSFORM,
                                              CMETA_SIG_U_I_L));
        check_true(cflow_op_signature_allowed(CFLOW_OP_FLAT_MAP,
                                              CMETA_SIG_G_I_L));
        check_true(cflow_op_signature_allowed(CFLOW_OP_REDUCE,
                                              CMETA_SIG_B_L_L_L));
        check_true(cflow_op_signature_allowed(CFLOW_OP_ZIP,
                                              CMETA_SIG_B_L_D_D));
        check_false(cflow_op_signature_allowed(CFLOW_OP_MAP,
                                               CMETA_SIG_U_I_B));
    }
}
