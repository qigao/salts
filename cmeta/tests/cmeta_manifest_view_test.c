#include "cmeta_manifest_view_fixture.h"
#include "tinytest.h"
#include "cmeta_manifest_declaration_cases.h"

static const cmeta_manifest_limits view_limits = {
    CMETA_MANIFEST_DEFAULT_ITEMS, CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES
};
enum { VIEW_TYPE, VIEW_STRUCT, VIEW_FUNCTION, VIEW_INTERFACE, VIEW_TRACE, VIEW_CAPABILITY };

suite("CMeta validated manifest views") {
    it("borrows canonical descriptors through explicit kinds") {
        const cmeta_manifest *manifest = cmeta_view_fixture_manifest();
        const cmeta_type_desc *type = NULL;
        const cmeta_struct_desc *structure = NULL, *trace = NULL;
        const cmeta_function_abi_desc *function = NULL;
        const cmeta_interface_desc *interface_desc = NULL, *capability = NULL;
        check_equal(cmeta_manifest_get_type(manifest, VIEW_TYPE, &view_limits, &type), CMETA_OK);
        check_true(type == &cmeta_type_int);
        check_equal(cmeta_manifest_get_struct(manifest, VIEW_STRUCT, &view_limits, &structure), CMETA_OK);
        check_equal(structure->field_count, (size_t)1u);
        check_equal(cmeta_manifest_get_function(manifest, VIEW_FUNCTION, &view_limits, &function), CMETA_OK);
        check_true(function->function->params[0].type == &cmeta_type_int);
        check_equal(cmeta_manifest_get_interface(manifest, VIEW_INTERFACE, &view_limits, &interface_desc), CMETA_OK);
        check_equal(interface_desc->method_count, (size_t)1u);
        check_equal(cmeta_manifest_get_trace(manifest, VIEW_TRACE, &view_limits, &trace), CMETA_OK);
        check_true(trace == structure);
        check_equal(cmeta_manifest_get_capability(manifest, VIEW_CAPABILITY, &view_limits, &capability), CMETA_OK);
        check_true(capability == interface_desc);
    }
    it("rejects version, bounds, NULL and wrong kind without publishing output") {
        cmeta_manifest manifest = *cmeta_view_fixture_manifest();
        const cmeta_type_desc *out = &cmeta_type_long;
        check_equal(cmeta_manifest_get_type(&manifest, VIEW_STRUCT, &view_limits, &out), CMETA_TYPE_MISMATCH);
        check_equal(cmeta_manifest_get_type(&manifest, manifest.count, &view_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_manifest_get_type(NULL, VIEW_TYPE, &view_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_manifest_get_type(&manifest, VIEW_TYPE, NULL, &out), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_manifest_get_type(&manifest, VIEW_TYPE, &view_limits, NULL), CMETA_INVALID_ARGUMENT);
        ++manifest.format_version;
        check_equal(cmeta_manifest_get_type(&manifest, VIEW_TYPE, &view_limits, &out), CMETA_TYPE_MISMATCH);
        check_true(out == &cmeta_type_long);
    }
    it("rejects malformed canonical metadata and exhausted budgets") {
        cmeta_manifest manifest = *cmeta_view_fixture_manifest();
        cmeta_manifest_limits limits = view_limits;
        const cmeta_type_desc *out = &cmeta_type_long;
        limits.max_items = 1u;
        check_equal(cmeta_manifest_get_type(&manifest, VIEW_TYPE, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        cmeta_type_desc type = cmeta_type_int;
        cmeta_manifest_entry entry = {"type", CMETA_MANIFEST_TYPE, &type, 0u, 0u};
        manifest.entries = &entry;
        manifest.count = 1u;
        type.align = 0u;
        check_equal(cmeta_manifest_get_type(&manifest, 0u, &view_limits, &out), CMETA_INVALID_ARGUMENT);
        type = cmeta_type_int;
        cmeta_type_identity cycle = {0};
        cycle.form = CMETA_TYPE_CONST;
        cycle.base = &cycle;
        type.identity = &cycle;
        check_equal(cmeta_manifest_get_type(&manifest, 0u, &view_limits, &out), CMETA_CAPACITY_EXCEEDED);
        check_true(out == &cmeta_type_long);
    }
    it("bounds descriptor rows and identity nodes before semantic validation") {
        const cmeta_manifest *source = cmeta_view_fixture_manifest();
        cmeta_manifest_limits limits = view_limits;
        const cmeta_function_abi_desc *function = NULL;
        check_equal(cmeta_manifest_get_function(source, VIEW_FUNCTION, &limits, &function), CMETA_OK);
        cmeta_function_abi_desc bad_abi = *function;
        cmeta_function_desc bad_function = *function->function;
        bad_function.param_count = limits.max_items + 1u;
        bad_abi.function = &bad_function;
        cmeta_manifest_entry entry = {"function", CMETA_MANIFEST_FUNCTION, &bad_abi, 0u, 0u};
        cmeta_manifest manifest = {"bounded", &entry, 1u, CMETA_MANIFEST_FORMAT_VERSION};
        const cmeta_function_abi_desc *out = function;
        check_equal(cmeta_manifest_get_function(&manifest, 0u, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        bad_function = *function->function;
        bad_abi.return_carrier = CMETA_ABI_VOID;
        check_equal(cmeta_manifest_get_function(&manifest, 0u, &limits, &out), CMETA_INVALID_ARGUMENT);
        check_true(out == function);

        cmeta_type_identity base = CMETA_TYPE_ID_ATOM_INIT("bounded.base");
        cmeta_type_identity outer = {0};
        outer.form = CMETA_TYPE_CONST;
        outer.base = &base;
        cmeta_type_desc type = cmeta_type_int;
        type.identity = &outer;
        entry.kind = CMETA_MANIFEST_TYPE;
        entry.descriptor = &type;
        limits.max_identity_nodes = 1u;
        const cmeta_type_desc *type_out = &cmeta_type_long;
        check_equal(cmeta_manifest_get_type(&manifest, 0u, &limits, &type_out), CMETA_CAPACITY_EXCEEDED);
        limits = view_limits;
        limits.max_identity_depth = 1u;
        check_equal(cmeta_manifest_get_type(&manifest, 0u, &limits, &type_out), CMETA_CAPACITY_EXCEEDED);
        limits.max_identity_depth = CMETA_MANIFEST_DEPTH_LIMIT + 1u;
        check_equal(cmeta_manifest_get_type(&manifest, 0u, &limits, &type_out), CMETA_INVALID_ARGUMENT);
        check_true(type_out == &cmeta_type_long);
    }
    it("checks layout ranges before publishing trace payloads") {
        const cmeta_struct_desc *source = NULL;
        check_equal(cmeta_manifest_get_trace(cmeta_view_fixture_manifest(), VIEW_TRACE, &view_limits, &source), CMETA_OK);
        cmeta_struct_desc malformed = *source;
        cmeta_field_desc field = source->fields[0];
        field.offset = source->size;
        malformed.fields = &field;
        cmeta_manifest_entry entry = {"trace", CMETA_MANIFEST_TRACEPOINT, &malformed, 0u, 0u};
        cmeta_manifest manifest = {"bad", &entry, 1u, CMETA_MANIFEST_FORMAT_VERSION};
        const cmeta_struct_desc *out = source;
        check_equal(cmeta_manifest_get_trace(&manifest, 0u, &view_limits, &out), CMETA_INVALID_ARGUMENT);
        check_true(out == source);
    }
}
