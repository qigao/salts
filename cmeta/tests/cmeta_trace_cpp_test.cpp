#include "cmeta_trace_fixture.h"
#include "tinytest.hpp"

suite("CMeta C++ opaque trace and fault controls") {
    it("consumes C-owned faults and dispatches through the owning C facade") {
        check_equal(salts_fast_disable(&cmeta_shared_fault), SALTS_OK);
        check_false(salts_fast_key_consume(&cmeta_shared_fault));
        check_equal(salts_fast_enable(&cmeta_shared_fault), SALTS_OK);
        check_true(salts_fast_key_consume(&cmeta_shared_fault));
        check_false(salts_fast_key_consume(&cmeta_shared_fault));
        check_equal(cmeta_trace_fixture_enable(), CMETA_OK);
        cmeta_trace_fixture_emit(UINT64_MAX, 0);
        check_equal(cmeta_trace_fixture_last_id(), UINT64_MAX);
        const cmeta_struct_desc *meta = cmeta_trace_fixture_meta();
        check_equal(meta->field_count, static_cast<size_t>(2u));
        check_equal(meta->fields[0].size, sizeof(uint64_t));
        check_equal(cmeta_trace_fixture_disable(), CMETA_OK);
        cmeta_trace_fixture_emit(UINT64_C(0), 0);
        check_equal(cmeta_trace_fixture_last_id(), UINT64_MAX);
    }
}
