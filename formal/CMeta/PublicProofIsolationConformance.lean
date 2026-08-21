module
import CMeta.PublicProof

/-!
# Public proof hard-isolation conformance

This module is a downstream-client view: it imports only `CMeta.PublicProof`.
The positive checks define the supported proof facade; negative assertions make
representative internal proof plumbing unavailable through that facade.
M6 will extend this list after the remaining legacy conformance consumers have
been moduleized.
-/

#check CMeta.PublicProof.structured_graph_type_safe
#check CMeta.PublicProof.zip_lowering_type_safe
#check CMeta.PublicProof.fused_map_type_safe
#check CMeta.PublicProof.direct_plan_exact
#check CMeta.PublicProof.runtime_output_type_safe
#check CMeta.PublicProof.static_checker_matches_runtime

assert_not_exists CMeta.EndToEnd.direct_plan_exact
assert_not_exists CMeta.TypedGraph.check_stages
assert_not_exists CMeta.FusedMap.type_preserved
assert_not_exists CMeta.ExecProgram.runtime_execution_exact
#check_assertions
