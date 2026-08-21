import CMeta.NestedReplayBackendPlan

/-!
# Certified preprocessor backend selection policy

Registry lookup and candidate discovery remain policy-free. This layer ranks an
already-discovered list of certified backends and proves that ranking cannot
manufacture entries outside that list. Registry-level selection therefore
inherits the candidate query/support guarantees established below the policy
boundary.
-/

namespace CMeta
namespace Producer

/-- Three-way preference result used for composable backend ranking. -/
inductive BackendPreference where
  | preferLeft
  | equivalent
  | preferRight
  deriving Repr, DecidableEq

/-- A selection policy compares two already-certified backend candidates. -/
structure BackendSelectionPolicy where
  compare : CertifiedPreprocessorBackend → CertifiedPreprocessorBackend →
    BackendPreference

namespace BackendSelectionPolicy

/-- Lexicographic policy composition: consult the second policy only when the
    first regards both candidates as equivalent. -/
def thenBy
    (primary secondary : BackendSelectionPolicy) : BackendSelectionPolicy :=
  { compare := fun left right =>
      match primary.compare left right with
      | .equivalent => secondary.compare left right
      | result => result }

/-- Prefer the candidate with the larger formally certified replay envelope. -/
def preferGreaterCertifiedDepth : BackendSelectionPolicy :=
  { compare := fun left right =>
      let l := left.replayCapability.certifiedSameProducerDepth
      let r := right.replayCapability.certifiedSameProducerDepth
      if r < l then .preferLeft
      else if l < r then .preferRight
      else .equivalent }

/-- Prefer the newer compiler major version when an earlier criterion ties. -/
def preferNewerVersion : BackendSelectionPolicy :=
  { compare := fun left right =>
      let l := left.backend.compilerMajorVersion
      let r := right.backend.compilerMajorVersion
      if r < l then .preferLeft
      else if l < r then .preferRight
      else .equivalent }

/-- Reduce one comparison to a surviving candidate. Equivalence deliberately
    keeps the current left candidate; deterministic tie-breaking beyond the
    declared policy belongs in an explicit later criterion. -/
def choose
    (policy : BackendSelectionPolicy)
    (left right : CertifiedPreprocessorBackend) : CertifiedPreprocessorBackend :=
  match policy.compare left right with
  | .preferRight => right
  | .preferLeft => left
  | .equivalent => left

private def selectAux
    (policy : BackendSelectionPolicy)
    (current : CertifiedPreprocessorBackend) :
    List CertifiedPreprocessorBackend → CertifiedPreprocessorBackend
  | [] => current
  | next :: rest => selectAux policy (policy.choose current next) rest

/-- Select one candidate according to the declared policy, or `none` for an empty
    candidate set. -/
def select
    (policy : BackendSelectionPolicy)
    (candidates : List CertifiedPreprocessorBackend) :
    Option CertifiedPreprocessorBackend :=
  match candidates with
  | [] => none
  | first :: rest => some (selectAux policy first rest)

private theorem selectAux_mem
    (policy : BackendSelectionPolicy)
    (current : CertifiedPreprocessorBackend)
    (rest : List CertifiedPreprocessorBackend) :
    selectAux policy current rest ∈ current :: rest := by
  induction rest generalizing current with
  | nil =>
      simp [selectAux]
  | cons next rest ih =>
      cases hcmp : policy.compare current next <;>
        simp [selectAux, choose, hcmp, ih]

/-- A policy can only return an element of the candidate list supplied to it. -/
theorem select_mem
    (policy : BackendSelectionPolicy)
    (candidates : List CertifiedPreprocessorBackend)
    (backend : CertifiedPreprocessorBackend)
    (h : policy.select candidates = some backend) :
    backend ∈ candidates := by
  cases candidates with
  | nil =>
      simp [select] at h
  | cons first rest =>
      simp [select] at h
      subst backend
      exact selectAux_mem policy first rest

end BackendSelectionPolicy

namespace PreprocessorBackendRegistry

/-- Apply an explicit policy only after policy-free registry candidate discovery. -/
def selectSupporting
    (registry : PreprocessorBackendRegistry)
    (policy : BackendSelectionPolicy)
    (query : BackendQuery)
    (ir : ReplayIR) : Option CertifiedPreprocessorBackend :=
  policy.select (registry.supportingCandidates query ir)

/-- A successful registry-level selection remains a member of the policy-free
    supporting candidate set. -/
theorem selectSupporting_mem_candidates
    (registry : PreprocessorBackendRegistry)
    (policy : BackendSelectionPolicy)
    (query : BackendQuery)
    (ir : ReplayIR)
    (backend : CertifiedPreprocessorBackend)
    (h : registry.selectSupporting policy query ir = some backend) :
    backend ∈ registry.supportingCandidates query ir := by
  exact BackendSelectionPolicy.select_mem policy
    (registry.supportingCandidates query ir) backend h

/-- Selection cannot lose the replay-support guarantee established during
    candidate discovery. -/
theorem selectSupporting_supports
    (registry : PreprocessorBackendRegistry)
    (policy : BackendSelectionPolicy)
    (query : BackendQuery)
    (ir : ReplayIR)
    (backend : CertifiedPreprocessorBackend)
    (h : registry.selectSupporting policy query ir = some backend) :
    backend.supportsReplay ir := by
  have hmem := registry.selectSupporting_mem_candidates policy query ir backend h
  exact (registry.mem_supportingCandidates_iff query ir backend).1 hmem |>.2.2

/-- Any selected supporting backend lowers to the same canonical replay plan;
    selection policy affects which certificate is chosen, never plan semantics. -/
theorem selectSupporting_lowering_canonical
    (registry : PreprocessorBackendRegistry)
    (policy : BackendSelectionPolicy)
    (query : BackendQuery)
    (ir : ReplayIR)
    (backend : CertifiedPreprocessorBackend)
    (h : registry.selectSupporting policy query ir = some backend) :
    lowerReplayBackendPlan backend.replayCapability ir =
      some (ReplayBackendPlan.fromIR ir) := by
  exact lowerReplayBackendPlan_eq_canonical_of_supports
    backend.replayCapability ir
    (registry.selectSupporting_supports policy query ir backend h)

end PreprocessorBackendRegistry

end Producer
end CMeta
