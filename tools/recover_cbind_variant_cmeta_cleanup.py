#!/usr/bin/env python3
from pathlib import Path

path = Path("cbind/tests/cbind_variant_decode_test.c")
text = path.read_text(encoding="utf-8")


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected one exact match, found {count}: {old!r}")
    text = text.replace(old, new, 1)


replace_once(
    "static cbind_variant_select_mode cbind_variant_mode;\n",
    "static cbind_variant_select_mode cbind_variant_mode;\n"
    "static const cmeta_data_desc cbind_variant_text_data;\n",
)

replace_once(
    "        salts_tstr_cmeta_restore_zero(&value->payload.text);\n",
    "        (void)cmeta_data_buffer_restore_zero(&cbind_variant_text_data,\n"
    "                                             &value->payload.text);\n",
)

path.write_text(text, encoding="utf-8")
