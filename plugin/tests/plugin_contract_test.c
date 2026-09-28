#include <salts/plugin.h>
#include <tinytest.h>

#include "plugin_test_interface.h"

#include <string.h>

typedef struct plugin_test_codec_state {
    int bias;
} plugin_test_codec_state;

static int plugin_test_transform(void *self, int value) {
    plugin_test_codec_state *state = (plugin_test_codec_state *)self;
    return value + state->bias;
}

CMETA_IMPLEMENTS(plugin_test_codec, plugin_test_codec_impl, 1u,
    .transform = plugin_test_transform);

FunctionDecl(value, int, plugin_test_increment,
    (int, value, CMETA_PARAM_IN));

int plugin_test_increment(int value) {
    return value + 1;
}

FunctionDecl(value, int, plugin_test_decrement,
    (int, value, CMETA_PARAM_IN));

int plugin_test_decrement(int value) {
    return value - 1;
}

FunctionDecl(value, long, plugin_test_widen,
    (int, value, CMETA_PARAM_IN));

long plugin_test_widen(int value) {
    return (long)value;
}

static bool SALTS_PLUGIN_CALL plugin_test_increment_adapter(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
    int value;
    int result;

    if (context != NULL || return_storage == NULL || params == NULL ||
        param_count != 1u || params[0] == NULL)
        return false;

    value = *(const int *)params[0];
    result = plugin_test_increment(value);
    *(int *)return_storage = result;
    return true;
}

static bool SALTS_PLUGIN_CALL plugin_test_decrement_adapter(
    void *context,
    void *return_storage,
    void *const *params,
    size_t param_count) {
    int value;
    int result;

    if (context != NULL || return_storage == NULL || params == NULL ||
        param_count != 1u || params[0] == NULL)
        return false;

    value = *(const int *)params[0];
    result = plugin_test_decrement(value);
    *(int *)return_storage = result;
    return true;
}

static salts_plugin_manifest make_manifest(
    salts_plugin_export exports[2],
    plugin_test_codec *codec) {
    exports[0] = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = 1u,
        .export_id = "codec",
        .contract_id = "test.codec",
        .value.interface = {
            .desc = plugin_test_interface_a(),
            .value = codec,
        },
    };
    exports[1] = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
        .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
        .contract_version = 1u,
        .capabilities = 2u,
        .export_id = "test.transform.increment",
        .contract_id = "test.transform",
        .value.function = {
            .desc = FunctionMeta(plugin_test_increment),
            .abi = FunctionAbi(plugin_test_increment),
            .context = NULL,
            .invoke = plugin_test_increment_adapter,
        },
    };

    return (salts_plugin_manifest){
        .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
        .abi_version = SALTS_PLUGIN_ABI_VERSION,
        .plugin_id = "test.plugin",
        .version = {1u, 2u, 3u},
        .exports = exports,
        .export_count = 2u,
    };
}

spec("Salts Plugin current ABI contract") {
describe("manifest admission") {
    it("accepts Interface and reflected Function exports") {
        plugin_test_codec_state state = {7};
        plugin_test_codec codec =
            plugin_test_codec_impl_as_plugin_test_codec(&state);
        salts_plugin_export exports[2];
        salts_plugin_manifest manifest = make_manifest(exports, &codec);
        const salts_plugin_export *found = NULL;
        void *params[1];
        int input = 5;
        int output = 0;

        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_OK);

        check_equal(salts_plugin_manifest_find_export(
                        &manifest, "codec", &found),
                    SALTS_PLUGIN_OK);
        check_true(found == &exports[0]);
        check_true(plugin_test_codec_valid(
            (const plugin_test_codec *)found->value.interface.value));
        check_equal(plugin_test_codec_transform(
                        (plugin_test_codec *)found->value.interface.value, 5),
                    12);
        check_equal(salts_plugin_export_require_interface(
                        found, "test.codec", 1u, 1u,
                        plugin_test_interface_b()),
                    SALTS_PLUGIN_OK);

        check_equal(salts_plugin_manifest_find_export(
                        &manifest, "test.transform.increment", &found),
                    SALTS_PLUGIN_OK);
        check_true(found == &exports[1]);
        check_not_null(found->value.function.desc);
        check_not_null(found->value.function.abi);
        check_not_null(found->value.function.invoke);
        check_true(cmeta_function_desc_valid(found->value.function.desc));
        check_true(cmeta_function_abi_desc_valid(found->value.function.abi));
        check_true(found->value.function.abi->function == found->value.function.desc);
        check_equal(salts_plugin_export_require_function(
                        found, "test.transform", 1u, 2u),
                    SALTS_PLUGIN_OK);

        params[0] = &input;
        check_true(found->value.function.invoke(
            found->value.function.context, &output, params, 1u));
        check_equal(output, 6);

        check_equal(salts_plugin_manifest_find_export(
                        &manifest, "missing", &found),
                    SALTS_PLUGIN_UNKNOWN_EXPORT);
        check_null(found);
    }

    it("rejects any non-current ABI or non-exact struct layout") {
        plugin_test_codec_state state = {0};
        plugin_test_codec codec =
            plugin_test_codec_impl_as_plugin_test_codec(&state);
        salts_plugin_export exports[2];
        salts_plugin_manifest manifest = make_manifest(exports, &codec);

        manifest.abi_version = SALTS_PLUGIN_ABI_VERSION + 1u;
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_UNSUPPORTED_ABI);

        manifest = make_manifest(exports, &codec);
        manifest.struct_size = SALTS_PLUGIN_MANIFEST_SIZE - 1u;
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_INVALID_MANIFEST);

        manifest = make_manifest(exports, &codec);
        manifest.struct_size = SALTS_PLUGIN_MANIFEST_SIZE + 1u;
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_INVALID_MANIFEST);

        manifest = make_manifest(exports, &codec);
        exports[0].struct_size = SALTS_PLUGIN_EXPORT_SIZE - 1u;
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_INVALID_MANIFEST);

        manifest = make_manifest(exports, &codec);
        exports[0].struct_size = SALTS_PLUGIN_EXPORT_SIZE + 1u;
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_INVALID_MANIFEST);
    }

    it("requires complete FunctionAbi and a matching exact adapter") {
        plugin_test_codec_state state = {0};
        plugin_test_codec codec =
            plugin_test_codec_impl_as_plugin_test_codec(&state);
        salts_plugin_export exports[2];
        salts_plugin_manifest manifest = make_manifest(exports, &codec);
        cmeta_function_abi_desc incomplete = *FunctionAbi(plugin_test_increment);

        exports[1].value.function.abi = FunctionAbi(plugin_test_widen);
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_INVALID_MANIFEST);

        manifest = make_manifest(exports, &codec);
        incomplete.return_carrier = CMETA_ABI_UNSPECIFIED;
        exports[1].value.function.abi = &incomplete;
        check_true(cmeta_function_abi_desc_valid(&incomplete));
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_INVALID_MANIFEST);

        manifest = make_manifest(exports, &codec);
        exports[1].value.function.invoke = NULL;
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_INVALID_MANIFEST);
    }

    it("rejects duplicate exports and bounded-capacity overflow") {
        plugin_test_codec_state state = {0};
        plugin_test_codec codec =
            plugin_test_codec_impl_as_plugin_test_codec(&state);
        salts_plugin_export exports[2];
        salts_plugin_manifest manifest = make_manifest(exports, &codec);

        exports[1].export_id = exports[0].export_id;
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_DUPLICATE_EXPORT);

        manifest = make_manifest(exports, &codec);
        manifest.export_count = SALTS_PLUGIN_MAX_EXPORTS + 1u;
        check_equal(salts_plugin_manifest_validate(&manifest),
                    SALTS_PLUGIN_CAPACITY_EXCEEDED);
    }
}

describe("semantic identity") {
    it("compares independently compiled Interface representations by content") {
        const cmeta_interface_desc *left = plugin_test_interface_a();
        const cmeta_interface_desc *right = plugin_test_interface_b();

        check_not_null(left);
        check_not_null(right);
        check_true(left != right);
        check_true(cmeta_interface_desc_valid(left));
        check_true(cmeta_interface_desc_valid(right));
        check_true(cmeta_interface_desc_equal(left, right));
    }

    it("uses contract identity independently from Function/adapter addresses") {
        salts_plugin_export left = {
            .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
            .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
            .contract_version = 1u,
            .capabilities = 2u,
            .export_id = "test.transform.increment",
            .contract_id = "test.transform",
            .value.function = {
                .desc = FunctionMeta(plugin_test_increment),
                .abi = FunctionAbi(plugin_test_increment),
                .context = NULL,
                .invoke = plugin_test_increment_adapter,
            },
        };
        salts_plugin_export right = {
            .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
            .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
            .contract_version = 1u,
            .capabilities = 2u,
            .export_id = "test.transform.decrement",
            .contract_id = "test.transform",
            .value.function = {
                .desc = FunctionMeta(plugin_test_decrement),
                .abi = FunctionAbi(plugin_test_decrement),
                .context = NULL,
                .invoke = plugin_test_decrement_adapter,
            },
        };

        check_true(left.value.function.desc != right.value.function.desc);
        check_true(left.value.function.abi != right.value.function.abi);
        check_true(left.value.function.invoke != right.value.function.invoke);

        check_equal(salts_plugin_export_require_function(
                        &left, "test.transform", 1u, 2u),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_export_require_function(
                        &right, "test.transform", 1u, 2u),
                    SALTS_PLUGIN_OK);

        check_equal(salts_plugin_export_require_function(
                        &left, "test.transform", 2u, 2u),
                    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
        check_equal(salts_plugin_export_require_function(
                        &left, "test.transform", 1u, 4u),
                    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
    }
}

describe("ABI layout contract") {
    it("uses exact current layouts rather than compatibility prefixes") {
        check_equal(SALTS_PLUGIN_EXPORT_SIZE,
                    (uint32_t)sizeof(salts_plugin_export));
        check_equal(SALTS_PLUGIN_MANIFEST_SIZE,
                    (uint32_t)sizeof(salts_plugin_manifest));
        check_equal((unsigned)SALTS_PLUGIN_ABI_VERSION, 2u);
    }
}

describe("status contract") {
    it("keeps required failure classes distinct") {
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_INVALID_MANIFEST), "invalid_manifest"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_UNSUPPORTED_ABI), "unsupported_abi"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_DUPLICATE_PLUGIN_ID), "duplicate_plugin_id"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_DUPLICATE_EXPORT), "duplicate_export"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_UNKNOWN_EXPORT), "unknown_export"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_INCOMPATIBLE_CONTRACT),
                    "incompatible_contract"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_CAPACITY_EXCEEDED),
                    "capacity_exceeded"), 0);
    }
}
}
