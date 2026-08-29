#include "cmeta_expr.h"

#include <cmeta/data.h>
#include "tinytest.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

Enum(scxml_expr_stage,
    (SCXML_EXPR_IDLE, 1, "idle"),
    (SCXML_EXPR_READY, 2, "ready")
);

Struct(scxml_expr_order,
    (bool, ready),
    (int, count),
    (size_t, total),
    (double, ratio),
    (scxml_expr_stage, stage)
);

Struct(scxml_expr_root,
    (bool, enabled),
    (scxml_expr_order, order)
);

static const cmeta_type_identity order_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.expr.order");
static const cmeta_type_desc order_type = {
    .name = "scxml_expr_order",
    .size = sizeof(scxml_expr_order),
    .align = _Alignof(scxml_expr_order),
    .kind = CMETA_T_OBJECT,
    .identity = &order_identity
};

static const cmeta_type_identity root_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.expr.root");
static const cmeta_type_desc root_type = {
    .name = "scxml_expr_root",
    .size = sizeof(scxml_expr_root),
    .align = _Alignof(scxml_expr_root),
    .kind = CMETA_T_OBJECT,
    .identity = &root_identity
};

static const cmeta_type_identity stage_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.expr.stage");
static const cmeta_type_desc stage_type = {
    .name = "scxml_expr_stage",
    .size = sizeof(scxml_expr_stage),
    .align = _Alignof(scxml_expr_stage),
    .kind = CMETA_T_INTEGER,
    .identity = &stage_identity
};

static bool stage_is_zero(const void *object) {
    scxml_expr_stage value;
    memcpy(&value, object, sizeof(value));
    return value == 0;
}

static cmeta_status stage_read(const void *object, int64_t *out) {
    scxml_expr_stage value;
    if (object == NULL || out == NULL) return CMETA_INVALID_ARGUMENT;
    memcpy(&value, object, sizeof(value));
    *out = (int64_t)value;
    return CMETA_OK;
}

static cmeta_status stage_assign(void *object, int64_t value) {
    scxml_expr_stage native = (scxml_expr_stage)value;
    if (object == NULL) return CMETA_INVALID_ARGUMENT;
    memcpy(object, &native, sizeof(native));
    return CMETA_OK;
}

static void stage_restore_zero(void *object) {
    scxml_expr_stage value = (scxml_expr_stage)0;
    if (object != NULL) memcpy(object, &value, sizeof(value));
}

static const cmeta_data_enum_shape stage_shape = {
    .meta = EnumMeta(scxml_expr_stage)
};

static const cmeta_data_enum_ops stage_ops = {
    .struct_size = sizeof(cmeta_data_enum_ops),
    .abi_version = CMETA_DATA_ENUM_OPS_ABI_VERSION,
    .storage_type = &stage_type,
    .is_zero = stage_is_zero,
    .read = stage_read,
    .assign = stage_assign,
    .restore_zero = stage_restore_zero
};

static const cmeta_data_desc stage_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.expr.stage.data",
    .display_name = "Stage",
    .kind = CMETA_DATA_ENUM,
    .storage_type = &stage_type,
    .shape = &stage_shape,
    .enum_ops = &stage_ops
};

static const cmeta_data_field_desc order_fields[] = {
    {"test.scxml.expr.order.ready", "ready",
     offsetof(scxml_expr_order, ready), &cmeta_data_bool},
    {"test.scxml.expr.order.count", "count",
     offsetof(scxml_expr_order, count), &cmeta_data_int},
    {"test.scxml.expr.order.total", "total",
     offsetof(scxml_expr_order, total), &cmeta_data_size},
    {"test.scxml.expr.order.ratio", "ratio",
     offsetof(scxml_expr_order, ratio), &cmeta_data_double},
    {"test.scxml.expr.order.stage", "stage",
     offsetof(scxml_expr_order, stage), &stage_data}
};

static const cmeta_data_struct_shape order_shape = {
    .layout = StructMeta(scxml_expr_order),
    .fields = order_fields,
    .field_count = sizeof(order_fields) / sizeof(order_fields[0])
};

static const cmeta_data_desc order_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.expr.order.data",
    .display_name = "Order",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &order_type,
    .shape = &order_shape
};

static const cmeta_data_field_desc root_fields[] = {
    {"test.scxml.expr.root.enabled", "enabled",
     offsetof(scxml_expr_root, enabled), &cmeta_data_bool},
    {"test.scxml.expr.root.order", "order",
     offsetof(scxml_expr_root, order), &order_data}
};

static const cmeta_data_struct_shape root_shape = {
    .layout = StructMeta(scxml_expr_root),
    .fields = root_fields,
    .field_count = sizeof(root_fields) / sizeof(root_fields[0])
};

static const cmeta_data_desc root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.expr.root.data",
    .display_name = "Root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &root_type,
    .shape = &root_shape
};

typedef struct state_fixture {
    bool fail_active;
} state_fixture;

static bool resolve_state(void *user, const char *name, size_t size,
                          cflow_machine_state_id *out) {
    (void)user;
    if (name == NULL || out == NULL) return false;
    if (size == 6u && memcmp(name, "active", size) == 0) {
        *out = 7u;
        return true;
    }
    if (size == 5u && memcmp(name, "other", size) == 0) {
        *out = 9u;
        return true;
    }
    return false;
}

static bool state_is_active(void *user, cflow_machine_state_id state,
                            bool *out_active) {
    state_fixture *fixture = (state_fixture *)user;
    if (fixture == NULL || out_active == NULL || fixture->fail_active)
        return false;
    *out_active = state == 7u;
    return true;
}

static cflow_scxml_cmeta_expr_status compile_expression(
    cflow_scxml_cmeta_expr_program *program, const char *source,
    const cflow_scxml_cmeta_expr_limits *limits,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    return cflow_scxml_cmeta_expr_compile(
        program, source, strlen(source), &root_data,
        resolve_state, NULL, limits, diagnostic);
}

static bool evaluate_expression(const char *source,
                                const scxml_expr_root *root) {
    cflow_scxml_cmeta_expr_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = false;
    check_equal(compile_expression(&program, source, NULL, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_equal(cflow_scxml_cmeta_expr_evaluate(
                    &program, root, state_is_active, &states,
                    &result, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    cflow_scxml_cmeta_expr_program_destroy(&program);
    return result;
}

spec("CFlow SCXML private CMeta expressions") {
  static scxml_expr_root root;

  before_each() {
    memset(&root, 0, sizeof(root));
    root.enabled = true;
    root.order.ready = true;
    root.order.count = -3;
    root.order.total = SIZE_MAX;
    root.order.ratio = 1.5;
    root.order.stage = SCXML_EXPR_READY;
  }

  it("exposes finite nonzero default limits") {
    const cflow_scxml_cmeta_expr_limits limits =
        cflow_scxml_cmeta_expr_default_limits();
    check_true(limits.max_source_bytes > 0u);
    check_true(limits.max_instructions > 0u);
    check_true(limits.max_operands > 0u);
    check_true(limits.max_expression_depth > 0u);
    check_true(limits.max_path_depth > 0u);
    check_true(limits.max_literal_bytes > 0u);
  }

  it("rejects invalid compile arguments without publishing a program") {
    static const char expression[] = "true";
    cflow_scxml_cmeta_expr_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cflow_scxml_cmeta_expr_limits limits =
        cflow_scxml_cmeta_expr_default_limits();

    check_equal(cflow_scxml_cmeta_expr_compile(
                    NULL, expression, sizeof(expression) - 1u, &root_data,
                    resolve_state, NULL, NULL, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT);
    check_equal(cflow_scxml_cmeta_expr_compile(
                    &program, NULL, 0u, &root_data, resolve_state, NULL,
                    NULL, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT);
    limits.max_operands = 0u;
    check_equal(cflow_scxml_cmeta_expr_compile(
                    &program, expression, sizeof(expression) - 1u,
                    &root_data, resolve_state, NULL, &limits, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT);
    check_null(program.impl);
  }

  it("evaluates literals precedence and active-state predicates") {
    check_true(evaluate_expression("true && !false", &root));
    check_true(evaluate_expression("false || true && true", &root));
    check_true(evaluate_expression("(false || true) && In(\"active\")",
                                   &root));
    check_false(evaluate_expression("In(\"other\") || false", &root));
  }

  it("reads nested reflected scalar and enum locations") {
    check_true(evaluate_expression("enabled && order.ready", &root));
    check_true(evaluate_expression("order.count == -3", &root));
    check_true(evaluate_expression("order.total > order.count", &root));
    check_true(evaluate_expression("order.ratio >= 1.5", &root));
    check_true(evaluate_expression("order.stage == 2", &root));
    check_true(evaluate_expression("-1 < 0u", &root));
    check_true(evaluate_expression("0u > -1", &root));
    check_true(evaluate_expression(
        "18446744073709551615u == 18446744073709551615u", &root));
    check_true(evaluate_expression("enabled == true", &root));
  }

  it("defines unordered floating point comparisons") {
    root.order.ratio = NAN;
    check_false(evaluate_expression("order.ratio == order.ratio", &root));
    check_true(evaluate_expression("order.ratio != order.ratio", &root));
    check_false(evaluate_expression("order.ratio < 2.0", &root));
  }

  it("compares floating point and 64-bit integers without narrowing") {
    check_true(evaluate_expression(
        "9223372036854775807 < 9223372036854775808.0", &root));
    check_true(evaluate_expression(
        "9223372036854775808.0 > 9223372036854775807", &root));
    check_true(evaluate_expression(
        "18446744073709551615u < 18446744073709551616.0", &root));
    check_true(evaluate_expression(
        "18446744073709551615u != 18446744073709551616.0", &root));
    check_true(evaluate_expression(
        "-9223372036854775808 == -9223372036854775808.0", &root));
    check_true(evaluate_expression("0u == -0.0", &root));
  }

  it("retains immutable compiled operands independently of source") {
    char source[] = "order.count == -3";
    cflow_scxml_cmeta_expr_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = false;
    check_equal(compile_expression(&program, source, NULL, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    memset(source, 'x', sizeof(source) - 1u);
    check_equal(cflow_scxml_cmeta_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_true(result);
    cflow_scxml_cmeta_expr_program_destroy(&program);
  }

  it("rejects unknown paths nonboolean conditions and malformed syntax") {
    static const char *const invalid[] = {
        "order.missing == 1", "order.count", "true &&", "In(active)",
        "In(\"missing\")"};
    static const cflow_scxml_cmeta_expr_status expected[] = {
        CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
        CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
        CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
        CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
        CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION};
    size_t index;
    for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        cflow_scxml_cmeta_expr_program program = {0};
        cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
        check_equal(compile_expression(&program, invalid[index], NULL,
                                       &diagnostic), expected[index]);
        check_null(program.impl);
        check_equal(diagnostic.status, expected[index]);
        check_true(diagnostic.message[0] != '\0');
    }
  }

  it("rejects a selected nested scalar descriptor with inconsistent storage") {
    static const char expression[] = "order.count == 1";
    const cmeta_data_integer_shape wide_shape = {64u};
    cmeta_data_desc bad_leaf = cmeta_data_int;
    cmeta_data_field_desc bad_order_field = order_fields[1];
    cmeta_data_struct_shape bad_order_shape = order_shape;
    cmeta_data_desc bad_order = order_data;
    cmeta_data_field_desc bad_root_field = root_fields[1];
    cmeta_data_struct_shape bad_root_shape = root_shape;
    cmeta_data_desc bad_root = root_data;
    cflow_scxml_cmeta_expr_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};

    bad_leaf.shape = &wide_shape;
    bad_order_field.value = &bad_leaf;
    bad_order_shape.fields = &bad_order_field;
    bad_order_shape.field_count = 1u;
    bad_order.shape = &bad_order_shape;
    bad_root_field.value = &bad_order;
    bad_root_shape.fields = &bad_root_field;
    bad_root_shape.field_count = 1u;
    bad_root.shape = &bad_root_shape;

    check_equal(cflow_scxml_cmeta_expr_compile(
                    &program, expression, sizeof(expression) - 1u, &bad_root,
                    resolve_state, NULL, NULL, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH);
    check_null(program.impl);
  }

  it("fails fast on configured source instruction and depth limits") {
    cflow_scxml_cmeta_expr_limits limits =
        cflow_scxml_cmeta_expr_default_limits();
    cflow_scxml_cmeta_expr_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};

    limits.max_source_bytes = 4u;
    check_equal(compile_expression(&program, "true && false", &limits,
                                   &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED);
    check_null(program.impl);

    limits = cflow_scxml_cmeta_expr_default_limits();
    limits.max_instructions = 1u;
    check_equal(compile_expression(&program, "true && false", &limits,
                                   &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED);

    limits = cflow_scxml_cmeta_expr_default_limits();
    limits.max_expression_depth = 2u;
    check_equal(compile_expression(&program, "!!!true", &limits,
                                   &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED);

    limits = cflow_scxml_cmeta_expr_default_limits();
    limits.max_operands = 1u;
    check_equal(compile_expression(&program, "order.count == -3", &limits,
                                   &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED);

    limits = cflow_scxml_cmeta_expr_default_limits();
    limits.max_path_depth = 1u;
    check_equal(compile_expression(&program, "order.count == -3", &limits,
                                   &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED);

    limits = cflow_scxml_cmeta_expr_default_limits();
    limits.max_literal_bytes = 1u;
    check_equal(compile_expression(&program, "In(\"active\")", &limits,
                                   &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED);
  }

  it("preserves output when an enum provider rejects the stored value") {
    cflow_scxml_cmeta_expr_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    state_fixture states = {false};
    bool result = true;
    root.order.stage = (scxml_expr_stage)99;
    check_equal(compile_expression(&program, "order.stage == 2", NULL,
                                   &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_equal(cflow_scxml_cmeta_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR);
    check_true(result);
    cflow_scxml_cmeta_expr_program_destroy(&program);
  }

  it("preserves output when runtime inputs or callbacks fail") {
    cflow_scxml_cmeta_expr_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    state_fixture states = {true};
    bool result = true;
    check_equal(compile_expression(&program, "In(\"active\")", NULL,
                                   &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_equal(cflow_scxml_cmeta_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR);
    check_true(result);
    check_equal(cflow_scxml_cmeta_expr_evaluate(
                    &program, NULL, state_is_active, &states,
                    &result, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT);
    check_true(result);
    cflow_scxml_cmeta_expr_program_destroy(&program);
  }

  it("short circuits inactive Boolean branches") {
    cflow_scxml_cmeta_expr_program program = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    state_fixture states = {true};
    bool result = true;
    check_equal(compile_expression(
                    &program, "false && In(\"active\")", NULL,
                    &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_equal(cflow_scxml_cmeta_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_false(result);
    cflow_scxml_cmeta_expr_program_destroy(&program);

    check_equal(compile_expression(
                    &program, "true || In(\"active\")", NULL,
                    &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_equal(cflow_scxml_cmeta_expr_evaluate(
                    &program, &root, state_is_active, &states,
                    &result, &diagnostic),
                CFLOW_SCXML_CMETA_EXPR_OK);
    check_true(result);
    cflow_scxml_cmeta_expr_program_destroy(&program);
  }
}
