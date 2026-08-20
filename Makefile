CC ?= cc
AR ?= ar
CFLAGS ?= -O2
STD = -std=c11
WARN = -pedantic-errors -Wall -Wextra -Werror
CPPFLAGS = -Icmeta/include -Icflow/include -Iexamples

BUILD = build
CMETA_BUILD = $(BUILD)/cmeta
CFLOW_BUILD = $(BUILD)/cflow
EXAMPLE_BUILD = $(BUILD)/examples

CMETA_SRC = cmeta/src/cmeta.c
CONTAINER_SRC = $(wildcard container/src/*.c)
RAW_CONTAINER_HEADERS = $(filter-out container/include/container/meta.h container/include/container/typed.h,$(wildcard container/include/container/*.h))

CFLOW_SRC = \
  cflow/src/adapters.c cflow/src/coord.c cflow/src/effect.c cflow/src/graph.c \
  cflow/src/interfaces.c cflow/src/lower.c cflow/src/opt.c \
  cflow/src/plan_compile.c cflow/src/plan_exec.c cflow/src/property.c \
  cflow/src/reactive.c cflow/src/relation_exec.c cflow/src/runtime.c \
  cflow/src/scheduler.c cflow/src/scheduler_worker.c cflow/src/sources.c \
  cflow/src/stream.c cflow/src/subrun.c cflow/src/verify.c

CMETA_OBJ = $(patsubst cmeta/src/%.c,$(CMETA_BUILD)/%.o,$(CMETA_SRC))
CFLOW_OBJ = $(patsubst cflow/src/%.c,$(CFLOW_BUILD)/%.o,$(CFLOW_SRC))

LIBCMETA = $(BUILD)/libcmeta.a
LIBCFLOW = $(BUILD)/libcflow.a

DEMOS = demo_collection demo_sources demo_worker demo_backpressure demo_subruns \
        demo_coord demo_graph_ir demo_relations demo_normalize demo_opt demo_contract \
        demo_verify demo_plan demo_lambda demo_meta demo_interface
DEMO_BINS = $(addprefix $(EXAMPLE_BUILD)/,$(DEMOS))

.PHONY: all libs cmeta cflow examples test schema generic header-only container-stream boundary matrix sanitize property fuzz fuzz-smoke stress verify clean

all: libs examples
libs: $(LIBCMETA) $(LIBCFLOW)
cmeta: $(LIBCMETA)
cflow: $(LIBCFLOW)
examples: $(DEMO_BINS) $(EXAMPLE_BUILD)/demo_cmeta_standalone

$(CMETA_BUILD)/%.o: cmeta/src/%.c
	@mkdir -p $(CMETA_BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) -Icmeta/include -c $< -o $@

$(CFLOW_BUILD)/%.o: cflow/src/%.c
	@mkdir -p $(CFLOW_BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) -Icmeta/include -Icflow/include -c $< -o $@

$(LIBCMETA): $(CMETA_OBJ)
	@mkdir -p $(BUILD)
	$(AR) rcs $@ $^

$(LIBCFLOW): $(CFLOW_OBJ)
	@mkdir -p $(BUILD)
	$(AR) rcs $@ $^

$(EXAMPLE_BUILD)/demo_cmeta_standalone: examples/demo_cmeta_standalone.c $(LIBCMETA)
	@mkdir -p $(EXAMPLE_BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) -Icmeta/include $< $(LIBCMETA) -o $@

$(EXAMPLE_BUILD)/%: examples/%.c examples/ops.c $(LIBCFLOW) $(LIBCMETA)
	@mkdir -p $(EXAMPLE_BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) $(CPPFLAGS) $< examples/ops.c $(LIBCFLOW) $(LIBCMETA) -o $@

GENERIC_CPPFLAGS = -Itests/containers/support -Itests/containers -Icontainer/include -Icmeta/include

$(BUILD)/schema-replay: $(CMETA_SRC) tests/schema_replay.c
	@mkdir -p $(BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) -Icmeta/include $^ -o $@

schema: $(BUILD)/schema-replay
	$(BUILD)/schema-replay
	@echo "CMeta Schema/Replay kernel verification PASS"

$(BUILD)/generic-smoke: $(CMETA_SRC) $(CONTAINER_SRC) tests/containers/container_types.c tests/containers/container_meta_smoke.c
	@mkdir -p $(BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) $(GENERIC_CPPFLAGS) $^ -o $@

$(BUILD)/btree-property: $(CMETA_SRC) $(CONTAINER_SRC) tests/containers/container_types.c tests/containers/btree_property.c
	@mkdir -p $(BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) $(GENERIC_CPPFLAGS) $^ -o $@

$(BUILD)/typed-coexist: $(CMETA_SRC) $(CONTAINER_SRC) tests/containers/typed_coexist.c
	@mkdir -p $(BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) $(GENERIC_CPPFLAGS) -Icflow/include $^ -o $@

$(BUILD)/range-smoke: $(CMETA_SRC) $(CONTAINER_SRC) tests/containers/container_types.c tests/containers/range_smoke.c
	@mkdir -p $(BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) $(GENERIC_CPPFLAGS) $^ -o $@

$(BUILD)/header-only-multitu: $(CMETA_SRC) $(CONTAINER_SRC) tests/containers/container_types.c tests/containers/header_only_multi_tu_helper.c tests/containers/header_only_multi_tu.c
	@mkdir -p $(BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) $(GENERIC_CPPFLAGS) $^ -o $@

header-only: $(BUILD)/header-only-multitu
	$(BUILD)/header-only-multitu
	@echo "header-only typed container multi-TU verification PASS"

$(BUILD)/stream-container: $(CMETA_SRC) $(CFLOW_SRC) $(CONTAINER_SRC) tests/containers/container_types.c tests/containers/stream_container.c
	@mkdir -p $(BUILD)
	$(CC) $(STD) $(WARN) $(CFLAGS) $(GENERIC_CPPFLAGS) -Icflow/include $^ -o $@

generic: $(BUILD)/generic-smoke $(BUILD)/btree-property $(BUILD)/typed-coexist $(BUILD)/range-smoke $(BUILD)/header-only-multitu
	$(BUILD)/generic-smoke
	$(BUILD)/btree-property
	$(BUILD)/typed-coexist
	$(BUILD)/range-smoke
	$(BUILD)/header-only-multitu
	@echo "CMeta generic/value/container verification PASS"

container-stream: $(BUILD)/stream-container
	$(BUILD)/stream-container
	@echo "CMeta Range -> CFlow Stream verification PASS"

test: examples
	$(EXAMPLE_BUILD)/demo_cmeta_standalone
	@set -e; for d in $(DEMOS); do $(EXAMPLE_BUILD)/$$d >/dev/null; done
	@echo "CMeta + CFlow split regression PASS"

property: $(EXAMPLE_BUILD)/demo_verify
	$(EXAMPLE_BUILD)/demo_verify 5000

boundary: libs $(EXAMPLE_BUILD)/demo_cmeta_standalone
	@if grep -RniE '#include[[:space:]]*[<\"]cflow/' cmeta; then \
	  echo "ERROR: CMeta depends on CFlow"; exit 1; \
	else echo "dependency boundary: CMeta -> no CFlow dependency"; fi
	@if grep -RniE '#include[[:space:]]*[<"]cflow/' container; then \
	  echo "ERROR: Container depends on CFlow"; exit 1; \
	else echo "dependency boundary: Container -> no CFlow dependency"; fi
	@if grep -nE '#include[[:space:]]*[<"]cmeta/|\bCMETA_' $(RAW_CONTAINER_HEADERS); then \
	  echo "ERROR: raw Container headers depend on CMeta"; exit 1; \
	else echo "dependency boundary: Container raw -> no CMeta dependency"; fi
	@if grep -RniE '\bcflow_(graph|run|stream|scheduler|source|sink|waitable|plan)' cmeta/include cmeta/src; then \
	  echo "ERROR: CFlow runtime symbols leaked into CMeta"; exit 1; \
	else echo "namespace boundary: CMeta contains no CFlow runtime API"; fi
	@if ! grep -q '<cmeta/cmeta.h>' cflow/include/cflow/meta.h; then \
	  echo "ERROR: CFlow Meta bridge does not depend on CMeta"; exit 1; \
	else echo "dependency boundary: CFlow -> CMeta"; fi
	@if grep -RniE '#include[[:space:]]*\"cflow_|#include[[:space:]]*\"cmeta_' cflow/include cflow/src; then \
	  echo "ERROR: legacy flat include names remain"; exit 1; \
	else echo "include boundary: namespaced <cmeta/...>/<cflow/...> headers"; fi
	@if grep -RniE '\bmeta_enum(v|_impl|v_impl)?\b|\bmeta_struct\b' cmeta/include cmeta/src cflow/include cflow/src examples --exclude='*.md'; then \
	  echo "ERROR: legacy meta_enum/meta_struct API remains"; exit 1; \
	else echo "schema boundary: Enum/Struct are canonical single-declaration APIs"; fi
	@if grep -RniE '\bItem[[:space:]]*\(|\bField[[:space:]]*\(' cmeta/include cmeta/src cflow/include cflow/src examples --exclude='*.md'; then \
	  echo "ERROR: legacy Item/Field wrapper macros remain"; exit 1; \
	else echo "schema-row boundary: Enum/Struct consume tuples directly"; fi
	@if ! grep -q '^#define Schema' cmeta/include/cmeta/pp.h || ! grep -q '^#define Replay' cmeta/include/cmeta/pp.h; then \
	  echo "ERROR: unified Schema/Replay kernel missing"; exit 1; \
	else echo "schema kernel: Schema + Replay are canonical"; fi
	@if grep -RniE 'CMETA_(OPERATORS|CONTAINERS)_APPLY' cmeta/include; then \
	  echo "ERROR: specialized operator/container replay machinery remains"; exit 1; \
	else echo "schema kernel: no specialized Operators/Containers apply engines"; fi
	@if ! grep -q 'Schema(CMETA_ENUM_ITEM_DECL' cmeta/include/cmeta/enum.h || \
	   ! grep -q 'Schema(CMETA_STRUCT_FIELD_DECL' cmeta/include/cmeta/struct.h; then \
	  echo "ERROR: Enum/Struct are not built on Schema kernel"; exit 1; \
	else echo "schema kernel: Enum + Struct consume shared row engine"; fi
	@if find cflow -name 'ops.def' -o -name '*.def' | grep -q .; then \
	  echo "ERROR: legacy replay .def schema remains"; exit 1; \
	elif grep -RniE 'ops\.def|#include[[:space:]]*[<"][^>"]*\.def' cflow/include cflow/src --exclude='*.md'; then \
	  echo "ERROR: legacy .def replay reference remains"; exit 1; \
	else echo "operator schema boundary: CFlowOperators + CMeta Operators, no .def replay"; fi
	@if ! grep -q 'Operators(M,' cflow/include/cflow/operators.h; then \
	  echo "ERROR: CFlow operator universe is not declared through CMeta Operators"; exit 1; \
	else echo "operator schema source: single CFlowOperators declaration"; fi
	@if grep -Rni 'CFlowOperators(CFLOW_OP_ROW)' cflow/include cflow/src; then \
	  echo "ERROR: CFlow named schema bypasses Replay"; exit 1; \
	else echo "operator schema consumption: Replay(CFlowOperators, mapper)"; fi
	@if ! grep -q '^Containers(' tests/containers/container_types.h; then \
	  echo "ERROR: container test universe is not declared through direct Containers(...)"; exit 1; \
	else echo "container API: direct one-shot Containers(...) instantiation"; fi
	@if grep -RniE '^#define[[:space:]]+implement\(|\b(DeclareContainers|ImplementContainers)\b|CMETA_IMPLEMENT_[A-Za-z]|CONTAINER_[A-Z0-9_]+_IMPLEMENT' cmeta/include container/include tests/containers --exclude='*.md'; then \
	  echo "ERROR: public/two-phase container implementation API remains"; exit 1; \
	else echo "container API: no implement/DeclareContainers/ImplementContainers phase"; fi
	@if test -d turbo; then \
	  echo "ERROR: legacy turbo/ container module remains"; exit 1; \
	else echo "container module: top-level container/ layout"; fi
	@if grep -RniE '#include[[:space:]]*[<"]turbo_(vec|deque|list|stack|queue|heap|set|hash_set|hash_map|map|multimap|btree|bplus_tree|container_meta|typed)\.h[>"]|\b(turbo_(vec|deque|list|stack|queue|heap|set|hash_set|hash_map|map|multimap|btree|bplus_tree|hash)_(t|[a-zA-Z0-9_]+))\b' container tests examples cmeta cflow 2>/dev/null --exclude='*.md'; then \
	  echo "ERROR: legacy turbo container API remains"; exit 1; \
	else echo "container module: no legacy turbo container API"; fi
	@$(EXAMPLE_BUILD)/demo_cmeta_standalone >/dev/null
	@echo "standalone CMeta link boundary PASS"

matrix:
	@set -e; \
	for cc in gcc clang; do \
	  command -v $$cc >/dev/null || continue; \
	  for opt in -O0 -O2; do \
	    echo "== $$cc $$opt CMeta standalone =="; \
	    rm -rf .matrix; mkdir -p .matrix/cmeta .matrix/cflow; \
	    for src in $(CMETA_SRC); do o=.matrix/cmeta/$$(basename $${src%.c}).o; $$cc $(STD) $(WARN) $$opt -Icmeta/include -c $$src -o $$o; done; \
	    $(AR) rcs .matrix/libcmeta.a .matrix/cmeta/*.o; \
	    $$cc $(STD) $(WARN) $$opt -Icmeta/include examples/demo_cmeta_standalone.c .matrix/libcmeta.a -o .matrix/meta; .matrix/meta >/dev/null; \
	    echo "== $$cc $$opt Schema/Replay =="; \
	    $$cc $(STD) $(WARN) $$opt -Icmeta/include $(CMETA_SRC) tests/schema_replay.c -o .matrix/schema; .matrix/schema >/dev/null; \
	    echo "== $$cc $$opt one-shot Containers =="; \
	    $$cc $(STD) $(WARN) $$opt $(GENERIC_CPPFLAGS) $(CMETA_SRC) $(CONTAINER_SRC) tests/containers/container_types.c tests/containers/container_meta_smoke.c -o .matrix/container; .matrix/container >/dev/null; \
	    echo "== $$cc $$opt header-only multi-TU =="; \
	    $$cc $(STD) $(WARN) $$opt $(GENERIC_CPPFLAGS) $(CMETA_SRC) $(CONTAINER_SRC) tests/containers/container_types.c tests/containers/header_only_multi_tu_helper.c tests/containers/header_only_multi_tu.c -o .matrix/multitu; .matrix/multitu >/dev/null; \
	    echo "== $$cc $$opt direct typed container + callable coexistence =="; \
	    $$cc $(STD) $(WARN) $$opt $(GENERIC_CPPFLAGS) -Icflow/include $(CMETA_SRC) $(CONTAINER_SRC) tests/containers/typed_coexist.c -o .matrix/direct; .matrix/direct >/dev/null; \
	    echo "== $$cc $$opt CFlow core =="; \
	    for src in $(CFLOW_SRC); do o=.matrix/cflow/$$(basename $${src%.c}).o; $$cc $(STD) $(WARN) $$opt -Icmeta/include -Icflow/include -c $$src -o $$o; done; \
	    $(AR) rcs .matrix/libcflow.a .matrix/cflow/*.o; \
	    for d in demo_collection demo_graph_ir demo_relations demo_plan demo_lambda demo_interface; do \
	      $$cc $(STD) $(WARN) $$opt $(CPPFLAGS) examples/$$d.c examples/ops.c .matrix/libcflow.a .matrix/libcmeta.a -o .matrix/demo; .matrix/demo >/dev/null; \
	    done; \
	  done; \
	done; rm -rf .matrix

sanitize:
	@rm -rf .san; mkdir -p .san
	$(CC) $(STD) -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(CPPFLAGS) $(CMETA_SRC) $(CFLOW_SRC) examples/ops.c examples/demo_lambda.c -o .san/demo
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 .san/demo >/dev/null
	$(CC) $(STD) -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(GENERIC_CPPFLAGS) -Icflow/include $(CMETA_SRC) $(CFLOW_SRC) $(CONTAINER_SRC) tests/containers/container_types.c tests/containers/stream_container.c -o .san/container-stream
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 .san/container-stream >/dev/null
	@rm -rf .san
	@echo "ASan/UBSan core + header-only container Stream PASS"

stress: examples
	@set -e; for i in $$(seq 1 100); do $(EXAMPLE_BUILD)/demo_worker >/dev/null; $(EXAMPLE_BUILD)/demo_subruns >/dev/null; $(EXAMPLE_BUILD)/demo_relations >/dev/null; done
	@$(EXAMPLE_BUILD)/demo_verify 10000 >/dev/null
	@echo "split runtime/compiler stress PASS"

fuzz:
	@if ! command -v clang >/dev/null 2>&1; then echo "clang unavailable"; exit 2; fi
	clang $(STD) -Wall -Wextra -Werror -O1 -g -fsanitize=fuzzer,address,undefined \
	  $(CPPFLAGS) $(CMETA_SRC) $(CFLOW_SRC) examples/ops.c tests/fuzz_graph.c -o $(BUILD)/fuzz_graph

fuzz-smoke: fuzz
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 $(BUILD)/fuzz_graph -runs=2000 >/dev/null
	@echo "libFuzzer split smoke PASS"

verify: test schema generic container-stream boundary property
	@echo "CMeta/CFlow split + Range/Stream verification PASS"

clean:
	rm -rf $(BUILD) .matrix .san
