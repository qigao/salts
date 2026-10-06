#include "cmeta_fingerprint_fixture.h"
#include "tinytest.h"

suite("Canonical CMeta contract fingerprints") {
    it("matches fixed v1 vectors across independent translation units") {
        uint64_t actual[FP_COUNT];
        check_equal(sizeof(uint32_t), (size_t)4u);
        check_equal(CMETA_ALIGNOF(uint32_t), (size_t)4u);
        check_true(cmeta_fingerprint_peer()->type != fingerprint_fixture.type);
        check_equal(cmeta_fingerprint_peer_values(actual), CMETA_OK);
        check_equal(actual, fingerprint_golden, sizeof(actual));
        uint64_t local;
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &fingerprint_limits, &local), CMETA_OK);
        check_equal(local, fingerprint_golden[FP_TYPE]);
        check_equal(cmeta_contract_fingerprint_struct(&fingerprint_record_meta, &fingerprint_limits, &local), CMETA_OK);
        check_equal(local, fingerprint_golden[FP_STRUCT]);
        check_equal(cmeta_contract_fingerprint_enum(FingerprintFlags_meta(), &fingerprint_limits, &local), CMETA_OK);
        check_equal(local, fingerprint_golden[FP_ENUM]);
        check_equal(cmeta_contract_fingerprint_function((&fingerprint_transform__function_abi_meta), &fingerprint_limits, &local), CMETA_OK);
        check_equal(local, fingerprint_golden[FP_FUNCTION]);
        check_equal(cmeta_contract_fingerprint_interface(&FingerprintService_interface_meta, &fingerprint_limits, &local), CMETA_OK);
        check_equal(local, fingerprint_golden[FP_INTERFACE]);
    }
    it("uses stable identity and layout instead of type display names") {
        cmeta_type_desc word = fingerprint_word;
        uint64_t out;
        word.name = "RenamedWord";
        check_equal(cmeta_contract_fingerprint_type(&word, &fingerprint_limits, &out), CMETA_OK);
        check_equal(out, fingerprint_golden[FP_TYPE]);
        word.size *= 2u;
        check_equal(cmeta_contract_fingerprint_type(&word, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_TYPE]);
        word = fingerprint_word;
        word.align *= 2u;
        check_equal(cmeta_contract_fingerprint_type(&word, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_TYPE]);
        word = fingerprint_word;
        cmeta_type_identity changed_id = CMETA_TYPE_ID_ATOM_INIT("test.other32");
        word.identity = &changed_id;
        check_equal(cmeta_contract_fingerprint_type(&word, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_TYPE]);
        word = fingerprint_word;
        word.traits = &cmeta_traits_int;
        check_equal(cmeta_contract_fingerprint_type(&word, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_TYPE]);
    }
    it("hashes canonical field rows and rejects guessed or inconsistent layouts") {
        cmeta_struct_desc record = fingerprint_record_meta;
        cmeta_field_desc field = fingerprint_fields[0];
        record.fields = &field;
        record.name = "DisplayRecord";
        field.type_name = "presentation only";
        uint64_t out;
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &out), CMETA_OK);
        check_equal(out, fingerprint_golden[FP_STRUCT]);
        field.name = "other";
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_STRUCT]);
        field = fingerprint_fields[0];
        record.size *= 2u;
        field.offset = field.size;
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_STRUCT]);
        out = fingerprint_golden[FP_STRUCT];
        field.offset = CMETA_FIELD_DYNAMIC_OFFSET;
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &out), CMETA_TYPE_MISMATCH);
        field = fingerprint_fields[0];
        field.type = NULL;
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &out), CMETA_TYPE_MISMATCH);
        field = fingerprint_fields[0];
        ++field.size;
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        field.offset = SIZE_MAX - 1u;
        field.size = SIZE_MAX;
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(out, fingerprint_golden[FP_STRUCT]);
    }
    it("preserves unsigned high bits and excludes enum display text") {
        cmeta_enum_domain domain = *FingerprintFlags_meta();
        cmeta_enum_bits_item items[2] = {domain.items[0], domain.items[1]};
        domain.items = items;
        items[1].text = "renamed high display";
        uint64_t out;
        check_true(cmeta_enum_domain_valid(&domain));
        check_equal(cmeta_contract_fingerprint_enum(&domain, &fingerprint_limits, &out), CMETA_OK);
        check_equal(out, fingerprint_golden[FP_ENUM]);
        items[1].bits = UINT64_C(0x4000000000000000);
        domain.declared_mask = items[0].bits | items[1].bits;
        check_equal(cmeta_contract_fingerprint_enum(&domain, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_ENUM]);
        domain.declared_mask = 0u;
        out = fingerprint_golden[FP_ENUM];
        check_false(cmeta_enum_domain_valid(&domain));
        check_equal(cmeta_contract_fingerprint_enum(&domain, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(out, fingerprint_golden[FP_ENUM]);
        domain = *FingerprintFlags_meta();
        domain.signedness = CMETA_ENUM_SIGNED;
        check_equal(cmeta_contract_fingerprint_enum(&domain, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_ENUM]);
        ++domain.abi_version;
        check_equal(cmeta_contract_fingerprint_enum(&domain, &fingerprint_limits, &out), CMETA_TYPE_MISMATCH);
    }
    it("tracks function contracts while ignoring parameter and function names") {
        cmeta_function_abi_desc abi = *(&fingerprint_transform__function_abi_meta);
        cmeta_function_desc function = *abi.function;
        cmeta_param_desc param = function.params[0];
        function.params = &param;
        function.name = "other_transform";
        param.name = "renamed_parameter";
        abi.function = &function;
        uint64_t out;
        check_true(cmeta_function_abi_contract_compatible((&fingerprint_transform__function_abi_meta), &abi));
        check_equal(cmeta_contract_fingerprint_function(&abi, &fingerprint_limits, &out), CMETA_OK);
        check_equal(out, fingerprint_golden[FP_FUNCTION]);
        param.flags = CMETA_PARAM_UNKNOWN;
        check_equal(cmeta_contract_fingerprint_function(&abi, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_FUNCTION]);
        param = (&fingerprint_transform__function_meta)->params[0];
        function.effects = CMETA_EFFECT_MAY_FAIL;
        function.properties = CMETA_PROP_NONE;
        check_equal(cmeta_contract_fingerprint_function(&abi, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_FUNCTION]);
        function = *(&fingerprint_transform__function_meta);
        function.result_flags = CMETA_RESULT_VALUE;
        check_equal(cmeta_contract_fingerprint_function(&abi, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_FUNCTION]);
        abi.return_carrier = CMETA_ABI_OPAQUE;
        check_equal(cmeta_contract_fingerprint_function(&abi, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_FUNCTION]);
        abi.return_carrier = CMETA_ABI_UNSPECIFIED;
        out = fingerprint_golden[FP_FUNCTION];
        check_equal(cmeta_contract_fingerprint_function(&abi, &fingerprint_limits, &out), CMETA_TYPE_MISMATCH);
        abi.return_carrier = CMETA_ABI_VOID;
        check_equal(cmeta_contract_fingerprint_function(&abi, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(out, fingerprint_golden[FP_FUNCTION]);
    }
    it("tracks interface method names and ownership but rejects legacy ABI-only rows") {
        cmeta_interface_desc service = FingerprintService_interface_meta;
        cmeta_interface_method_desc method = service.methods[0];
        service.methods = &method;
        service.name = "PresentationService";
        uint64_t out;
        check_equal(cmeta_contract_fingerprint_interface(&service, &fingerprint_limits, &out), CMETA_OK);
        check_equal(out, fingerprint_golden[FP_INTERFACE]);
        method.name = "other_read";
        check_equal(cmeta_contract_fingerprint_interface(&service, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_INTERFACE]);
        method = FingerprintService_interface_meta.methods[0];
        method.flags = CMETA_INTERFACE_METHOD_OWNS_SELF;
        check_equal(cmeta_contract_fingerprint_interface(&service, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_INTERFACE]);
        method.function = NULL;
        method.abi = NULL;
        out = fingerprint_golden[FP_INTERFACE];
        check_equal(cmeta_contract_fingerprint_interface(&service, &fingerprint_limits, &out), CMETA_TYPE_MISMATCH);
        check_equal(out, fingerprint_golden[FP_INTERFACE]);
    }
    it("bounds identity cycles, aggregate rows, nodes and string scans") {
        cmeta_type_desc word = fingerprint_word;
        cmeta_type_identity cycle = {CMETA_TYPE_CONST, NULL, NULL, NULL, NULL, 0u};
        cycle.base = &cycle;
        word.identity = &cycle;
        uint64_t out = fingerprint_golden[FP_TYPE];
        check_equal(cmeta_contract_fingerprint_type(&word, &fingerprint_limits, &out), CMETA_CAPACITY_EXCEEDED);
        word = fingerprint_word;
        word.identity = NULL;
        check_equal(cmeta_contract_fingerprint_type(&word, &fingerprint_limits, &out), CMETA_TYPE_MISMATCH);
        cmeta_fingerprint_limits limits = fingerprint_limits;
        limits.max_nodes = 1u;
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        limits = fingerprint_limits;
        limits.max_depth = 1u;
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        limits.max_depth = CMETA_FINGERPRINT_DEPTH_LIMIT + 1u;
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &limits, &out), CMETA_INVALID_ARGUMENT);
        limits = fingerprint_limits;
        limits.max_string_bytes = 1u;
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        const char unterminated[4] = {'N', 'a', 'm', 'e'};
        limits.max_string_bytes = sizeof(unterminated);
        word.name = unterminated;
        check_equal(cmeta_contract_fingerprint_type(&word, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        limits = fingerprint_limits;
        limits.max_rows = 1u;
        check_equal(cmeta_contract_fingerprint_enum(FingerprintFlags_meta(), &limits, &out), CMETA_CAPACITY_EXCEEDED);
        check_equal(out, fingerprint_golden[FP_TYPE]);
    }
    it("rejects null inputs without changing output") {
        uint64_t out = fingerprint_golden[FP_TYPE];
        check_equal(cmeta_contract_fingerprint_type(NULL, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_contract_fingerprint_struct(NULL, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_contract_fingerprint_enum(NULL, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_contract_fingerprint_function(NULL, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_contract_fingerprint_interface(NULL, &fingerprint_limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, NULL, &out), CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &fingerprint_limits, NULL), CMETA_INVALID_ARGUMENT);
        check_equal(out, fingerprint_golden[FP_TYPE]);
    }
    it("admits the exact depth, node and string budgets and rejects short budgets") {
        cmeta_fingerprint_limits limits = {2u, 2u, 1u,
            sizeof("Word") + sizeof("test.word32")};
        uint64_t out;
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &limits, &out), CMETA_OK);
        check_equal(out, fingerprint_golden[FP_TYPE]);
        --limits.max_string_bytes;
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        check_equal(out, fingerprint_golden[FP_TYPE]);
        limits = fingerprint_limits;
        limits.max_rows = 0u;
        check_equal(cmeta_contract_fingerprint_type(&fingerprint_word, &limits, &out), CMETA_INVALID_ARGUMENT);
        limits = fingerprint_limits;
        limits.max_rows = SIZE_MAX;
        cmeta_enum_domain oversized = *FingerprintFlags_meta();
        oversized.count = SIZE_MAX;
        check_equal(cmeta_contract_fingerprint_enum(&oversized, &limits, &out), CMETA_INVALID_ARGUMENT);
        check_equal(out, fingerprint_golden[FP_TYPE]);
    }
    it("retains canonical signed-width bits and enum declaration order") {
        cmeta_enum_bits_item values[] = {{UINT64_C(255), "NegativeOne", "minus one"},
            {UINT64_C(0), "Zero", "zero"}};
        cmeta_enum_domain domain = {sizeof(cmeta_enum_domain), CMETA_ENUM_DOMAIN_ABI_VERSION,
            CMETA_ENUM_SIGNED, 8u, CMETA_ENUM_ORDINARY, values, 2u, 0u};
        uint64_t initial, changed;
        check_equal(cmeta_contract_fingerprint_enum(&domain, &fingerprint_limits, &initial), CMETA_OK);
        cmeta_enum_bits_item swapped[] = {values[1], values[0]};
        domain.items = swapped;
        check_equal(cmeta_contract_fingerprint_enum(&domain, &fingerprint_limits, &changed), CMETA_OK);
        check_not_equal(changed, initial);
        domain.items = values;
        domain.bits = 16u;
        values[0].bits = UINT64_C(65535);
        check_equal(cmeta_contract_fingerprint_enum(&domain, &fingerprint_limits, &changed), CMETA_OK);
        check_not_equal(changed, initial);
        domain.bits = 8u;
        changed = initial;
        check_equal(cmeta_contract_fingerprint_enum(&domain, &fingerprint_limits, &changed), CMETA_INVALID_ARGUMENT);
        check_equal(changed, initial);
    }
    it("tracks function properties, parameter ABI carriers and interface method order") {
        cmeta_function_abi_desc abi = fingerprint_transform__function_abi_meta;
        cmeta_function_desc function = *abi.function;
        abi.function = &function;
        function.properties = CMETA_PROP_NONE;
        uint64_t out;
        check_equal(cmeta_contract_fingerprint_function(&abi, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_FUNCTION]);
        function = fingerprint_transform__function_meta;
        const cmeta_abi_carrier carriers[] = {CMETA_ABI_ENUM};
        abi.param_carriers = carriers;
        check_equal(cmeta_contract_fingerprint_function(&abi, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, fingerprint_golden[FP_FUNCTION]);
        cmeta_interface_method_desc methods[] = {FingerprintService_interface_meta.methods[0],
            FingerprintService_interface_meta.methods[0]};
        methods[1].name = "other";
        cmeta_interface_desc service = FingerprintService_interface_meta;
        service.methods = methods;
        service.method_count = 2u;
        uint64_t initial;
        check_equal(cmeta_contract_fingerprint_interface(&service, &fingerprint_limits, &initial), CMETA_OK);
        cmeta_interface_method_desc first = methods[0];
        methods[0] = methods[1];
        methods[1] = first;
        check_equal(cmeta_contract_fingerprint_interface(&service, &fingerprint_limits, &out), CMETA_OK);
        check_not_equal(out, initial);
    }
    it("discovers a typed canonical enum domain and validates it before publication") {
        const cmeta_manifest_limits limits = {CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES};
        const cmeta_enum_domain *out = NULL;
        check_equal(cmeta_manifest_get_enum(&fingerprint_manifest, 0u, &limits, &out), CMETA_OK);
        check_true(out == FingerprintFlags_meta());
        cmeta_enum_domain malformed = *out;
        cmeta_manifest_entry entry = fingerprint_manifest.entries[0];
        cmeta_manifest manifest = fingerprint_manifest;
        entry.descriptor = &malformed;
        manifest.entries = &entry;
        malformed.count = limits.max_items + 1u;
        check_equal(cmeta_manifest_get_enum(&manifest, 0u, &limits, &out), CMETA_CAPACITY_EXCEEDED);
        malformed = *FingerprintFlags_meta();
        malformed.declared_mask = 0u;
        check_equal(cmeta_manifest_get_enum(&manifest, 0u, &limits, &out), CMETA_INVALID_ARGUMENT);
        entry.kind = CMETA_MANIFEST_TYPE;
        check_equal(cmeta_manifest_get_enum(&manifest, 0u, &limits, &out), CMETA_TYPE_MISMATCH);
        check_true(out == FingerprintFlags_meta());
    }
    it("hashes declared generic identity and pointer pointee contracts without display lookup") {
        const cmeta_type_desc *arguments[] = {&fingerprint_word, &fingerprint_word};
        const cmeta_type_identity *ids[] = {&fingerprint_word_id, &fingerprint_word_id};
        cmeta_generic_desc constructor = cmeta_pair_generic_desc;
        cmeta_type_identity application = CMETA_TYPE_ID_APPLY_INIT(&constructor, ids);
        cmeta_type_desc storage = fingerprint_word;
        storage.identity = &application;
        cmeta_declared_type metadata = {&storage, &constructor, arguments, 2u, NULL};
        cmeta_struct_desc record = fingerprint_record_meta;
        cmeta_field_desc field = fingerprint_fields[0];
        record.fields = &field;
        field.type = &storage;
        field.declared_type = &metadata;
        uint64_t initial, changed;
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &initial), CMETA_OK);
        constructor.display_name = "PresentationPair";
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &changed), CMETA_OK);
        check_equal(changed, initial);
        constructor.stable_id = "test.OtherPair";
        check_equal(cmeta_contract_fingerprint_struct(&record, &fingerprint_limits, &changed), CMETA_OK);
        check_not_equal(changed, initial);
        cmeta_fingerprint_limits limits = fingerprint_limits;
        limits.max_rows = 2u;
        check_equal(cmeta_contract_fingerprint_struct(&record, &limits, &changed), CMETA_CAPACITY_EXCEEDED);

        cmeta_type_identity pointer_id = CMETA_TYPE_ID_POINTER_INIT(&fingerprint_word_id);
        cmeta_type_desc pointer = {"Pointer", sizeof(uint32_t *), CMETA_ALIGNOF(uint32_t *),
            CMETA_T_POINTER, &fingerprint_word, NULL, &pointer_id};
        check_equal(cmeta_contract_fingerprint_type(&pointer, &fingerprint_limits, &initial), CMETA_OK);
        cmeta_type_desc pointee = fingerprint_word;
        pointer.pointee = &pointee;
        pointee.align *= 2u;
        check_equal(cmeta_contract_fingerprint_type(&pointer, &fingerprint_limits, &changed), CMETA_OK);
        check_not_equal(changed, initial);
        pointer.pointee = &pointer;
        check_equal(cmeta_contract_fingerprint_type(&pointer, &fingerprint_limits, &changed), CMETA_CAPACITY_EXCEEDED);
    }
}
