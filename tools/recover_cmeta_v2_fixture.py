#!/usr/bin/env python3
from pathlib import Path

path = Path("cmeta/tests/cmeta_data_test.c")
text = path.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one exact match, found {count}: {old[:80]!r}")
    text = text.replace(old, new, 1)


replace_once(
    """static void cmeta_data_test_buffer_restore_zero(void *object) {\n    if (object != NULL)\n        *(int *)object = 0;\n}\n""",
    """static cmeta_status cmeta_data_test_buffer_init_zero(void *object) {\n    if (object == NULL)\n        return CMETA_INVALID_ARGUMENT;\n    *(int *)object = 0;\n    return CMETA_OK;\n}\n\nstatic void cmeta_data_test_buffer_restore_zero(void *object) {\n    if (object != NULL)\n        *(int *)object = 0;\n}\n\nstatic void cmeta_data_test_buffer_move(void *destination, void *source) {\n    if (destination == NULL || source == NULL)\n        return;\n    *(int *)destination = *(int *)source;\n    *(int *)source = 0;\n}\n""",
)

replace_once(
    """    .is_zero = cmeta_data_test_buffer_is_zero,\n    .assign = cmeta_data_test_buffer_assign,\n    .restore_zero = cmeta_data_test_buffer_restore_zero,\n    .read = cmeta_data_test_buffer_read\n};\n""",
    """    .is_zero = cmeta_data_test_buffer_is_zero,\n    .assign = cmeta_data_test_buffer_assign,\n    .restore_zero = cmeta_data_test_buffer_restore_zero,\n    .read = cmeta_data_test_buffer_read,\n    .init_zero = cmeta_data_test_buffer_init_zero,\n    .move = cmeta_data_test_buffer_move\n};\n""",
)

replace_once(
    """  it(\"reports a missing buffer read trait without breaking legacy ops\") {\n    static const unsigned char sentinel[] = {'x'};\n    cmeta_data_buffer_ops ops = cmeta_data_test_buffer_ops;\n    cmeta_data_desc desc = cmeta_data_test_buffer_desc;\n    const unsigned char *data = sentinel;\n    size_t size = 9u;\n    const int object = 3;\n\n    ops.struct_size = offsetof(cmeta_data_buffer_ops, read);\n    desc.buffer_ops = &ops;\n    check_true(cmeta_data_buffer_ops_of(&desc) == &ops);\n    check_equal(cmeta_data_buffer_read(&desc, &object, 3u, &data, &size),\n                CMETA_TRAIT_MISSING);\n    check_true(data == sentinel);\n    check_equal(size, (size_t)9u);\n\n    ops = cmeta_data_test_buffer_ops;\n    ops.read = NULL;\n    check_true(cmeta_data_buffer_ops_of(&desc) == &ops);\n    check_equal(cmeta_data_buffer_read(&desc, &object, 3u, &data, &size),\n                CMETA_TRAIT_MISSING);\n  }\n""",
    """  it(\"reports a missing buffer read trait without weakening v2 lifecycle\") {\n    static const unsigned char sentinel[] = {'x'};\n    cmeta_data_buffer_ops ops = cmeta_data_test_buffer_ops;\n    cmeta_data_desc desc = cmeta_data_test_buffer_desc;\n    const unsigned char *data = sentinel;\n    size_t size = 9u;\n    const int object = 3;\n\n    ops.read = NULL;\n    desc.buffer_ops = &ops;\n    check_true(cmeta_data_buffer_ops_of(&desc) == &ops);\n    check_equal(cmeta_data_buffer_read(&desc, &object, 3u, &data, &size),\n                CMETA_TRAIT_MISSING);\n    check_true(data == sentinel);\n    check_equal(size, (size_t)9u);\n  }\n""",
)

path.write_text(text, encoding="utf-8")
