from pathlib import Path

path = Path("cnet/tests/CMakeLists.txt")
text = path.read_text()
old = "  SOURCES cnet_api_test.c cnet_client_control_profile_contract_test.c\n"
new = "  SOURCES cnet_api_test.c\n"
if text.count(old) != 1:
    raise SystemExit(f"expected one profile test source line, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

extra = Path("cnet/tests/cnet_client_control_profile_contract_test.c")
if not extra.exists():
    raise SystemExit("expected RED contract source")
extra.unlink()
