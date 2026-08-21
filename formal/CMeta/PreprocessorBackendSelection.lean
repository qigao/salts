import Init.Tactics
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
      cases hcmp : policy.compare current next with
      | preferLeft =>
          have htail := ih current
          simp [selectAux, choose, hcmp] at htail ⊢
          exact htail.elim Or.inl (fun h => Or.inr (Or.inr h))
      | equivalent =>
          have htail := ih current
          simp [selectAux, choose, hcmp] at htail ⊢
          exact htail.elim Or.inl (fun h => Or.inr (Or.inr h))
      | preferRight =>
          have htail := ih next
          simp [selectAux, choose, hcmp] at htail ⊢
          exact htail.elim
            (fun h => Or.inr (Or.inl h))
            (fun h => Or.inr (Or.inr h))

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

/-- Semantic rank used to certify deterministic replay selection. Larger depth
    wins first; compiler major version is the secondary coordinate. -/
structure BackendSelectionRank where
  certifiedDepth : Nat
  compilerMajorVersion : Nat
  deriving Repr, DecidableEq

namespace BackendSelectionRank

/-- Lexicographic non-strict preference order on replay selection ranks. -/
abbrev le (left right : BackendSelectionRank) : Prop :=
  left.certifiedDepth < right.certifiedDepth ∨
  (left.certifiedDepth = right.certifiedDepth ∧
    left.compilerMajorVersion ≤ right.compilerMajorVersion)

private theorem le_refl (rank : BackendSelectionRank) : le rank rank := by
  exact Or.inr ⟨rfl, Nat.le_refl _⟩

private theorem le_total (left right : BackendSelectionRank) :
    le left right ∨ le right left := by
  rcases left with ⟨ld, lv⟩
  rcases right with ⟨rd, rv⟩
  simp [le]
  omega

private theorem le_trans {a b c : BackendSelectionRank}
    (hab : le a b) (hbc : le b c) : le a c := by
  rcases a with ⟨ad, av⟩
  rcases b with ⟨bd, bv⟩
  rcases c with ⟨cd, cv⟩
  simp [le] at hab hbc ⊢
  omega

private theorem le_antisymm {left right : BackendSelectionRank}
    (hlr : le left right) (hrl : le right left) : left = right := by
  rcases left with ⟨ld, lv⟩
  rcases right with ⟨rd, rv⟩
  simp [le] at hlr hrl
  have hd : ld = rd := by omega
  have hv : lv = rv := by omega
  cases hd
  cases hv
  rfl

/-- Deterministic maximum under the lexicographic replay rank. -/
def max (left right : BackendSelectionRank) : BackendSelectionRank :=
  if le left right then right else left

theorem max_eq_right {left right : BackendSelectionRank}
    (h : le left right) : max left right = right := by
  simp [max, h]

theorem max_eq_left {left right : BackendSelectionRank}
    (h : ¬ le left right) : max left right = left := by
  simp [max, h]

private theorem le_max_left (left right : BackendSelectionRank) :
    le left (max left right) := by
  by_cases h : le left right
  · rw [max_eq_right h]
    exact h
  · rw [max_eq_left h]
    exact le_refl left

private theorem le_max_right (left right : BackendSelectionRank) :
    le right (max left right) := by
  by_cases h : le left right
  · rw [max_eq_right h]
    exact le_refl right
  · have hright : le right left := (le_total left right).resolve_left h
    rw [max_eq_left h]
    exact hright

private theorem max_le {a b c : BackendSelectionRank}
    (ha : le a c) (hb : le b c) : le (max a b) c := by
  by_cases h : le a b
  · rw [max_eq_right h]
    exact hb
  · rw [max_eq_left h]
    exact ha

/-- Folding rank maxima is insensitive to swapping any two inputs after an
    arbitrary accumulated rank. -/
theorem max_right_comm (z x y : BackendSelectionRank) :
    max (max z x) y = max (max z y) x := by
  apply le_antisymm
  · apply max_le
    · apply max_le
      · exact le_trans (le_max_left z y) (le_max_left (max z y) x)
      · exact le_max_right (max z y) x
    · exact le_trans (le_max_right z y) (le_max_left (max z y) x)
  · apply max_le
    · apply max_le
      · exact le_trans (le_max_left z x) (le_max_left (max z x) y)
      · exact le_max_right (max z x) y
    · exact le_trans (le_max_right z x) (le_max_left (max z x) y)

/-- Bottom rank used to make the aggregate independent of which list element is
    encountered first. -/
def bottom : BackendSelectionRank := ⟨0, 0⟩

private theorem bottom_le (rank : BackendSelectionRank) : le bottom rank := by
  rcases rank with ⟨depth, version⟩
  simp [bottom, le]
  omega

private theorem max_bottom_left (rank : BackendSelectionRank) :
    max bottom rank = rank :=
  max_eq_right (bottom_le rank)

/-- Canonical rank aggregate for a candidate list. -/
def aggregate (ranks : List BackendSelectionRank) : BackendSelectionRank :=
  ranks.foldl max bottom

/-- The canonical rank aggregate depends only on the multiset of ranks, not list
    representation order. -/
theorem aggregate_eq_of_perm {left right : List BackendSelectionRank}
    (h : left.Perm right) : aggregate left = aggregate right := by
  exact h.foldl_eq'
    (fun x _ y _ z => max_right_comm z x y)
    bottom

end BackendSelectionRank

/-- A policy is well formed for registry selection when its pairwise choice is
    exactly maximum under a lawful replay rank and equal rank within one query
    determines one backend identity. These are the properties needed to erase
    registry list order from observable selection. -/
structure WellFormedSelectionPolicy where
  policy : BackendSelectionPolicy
  rank : CertifiedPreprocessorBackend → BackendSelectionRank
  choose_rank : ∀ left right,
    rank (policy.choose left right) =
      BackendSelectionRank.max (rank left) (rank right)
  key_eq_of_query_rank_eq : ∀ left right query,
    left.matchesQuery query →
    right.matchesQuery query →
    rank left = rank right →
    left.key = right.key

namespace BackendSelectionPolicy

/-- Rank corresponding exactly to the replay policy's depth-then-version
    preference. -/
def replayRank (backend : CertifiedPreprocessorBackend) : BackendSelectionRank :=
  ⟨backend.replayCapability.certifiedSameProducerDepth,
    backend.backend.compilerMajorVersion⟩

private theorem backendKey_eq_of_fields
    (left right : BackendKey)
    (hfamily : left.family = right.family)
    (hversion : left.majorVersion = right.majorVersion)
    (hmode : left.languageMode = right.languageMode) : left = right := by
  rcases left with ⟨lf, lv, lm⟩
  rcases right with ⟨rf, rv, rm⟩
  simp_all

private theorem replayChooseRank
    (left right : CertifiedPreprocessorBackend) :
    replayRank
        ((preferGreaterCertifiedDepth.thenBy preferNewerVersion).choose left right) =
      BackendSelectionRank.max (replayRank left) (replayRank right) := by
  by_cases hrl :
      right.replayCapability.certifiedSameProducerDepth <
        left.replayCapability.certifiedSameProducerDepth
  · have hchoose :
        (preferGreaterCertifiedDepth.thenBy preferNewerVersion).choose left right = left := by
      simp [choose, thenBy, preferGreaterCertifiedDepth, hrl]
    have hnle : ¬ BackendSelectionRank.le (replayRank left) (replayRank right) := by
      simp [BackendSelectionRank.le, replayRank]
      omega
    rw [hchoose, BackendSelectionRank.max_eq_left hnle]
  · by_cases hlr :
      left.replayCapability.certifiedSameProducerDepth <
        right.replayCapability.certifiedSameProducerDepth
    · have hchoose :
          (preferGreaterCertifiedDepth.thenBy preferNewerVersion).choose left right = right := by
        simp [choose, thenBy, preferGreaterCertifiedDepth, hrl, hlr]
      have hle : BackendSelectionRank.le (replayRank left) (replayRank right) := by
        exact Or.inl hlr
      rw [hchoose, BackendSelectionRank.max_eq_right hle]
    · have hdepth :
          left.replayCapability.certifiedSameProducerDepth =
            right.replayCapability.certifiedSameProducerDepth := by
        omega
      by_cases hrv :
          right.backend.compilerMajorVersion < left.backend.compilerMajorVersion
      · have hchoose :
            (preferGreaterCertifiedDepth.thenBy preferNewerVersion).choose left right = left := by
          simp [choose, thenBy, preferGreaterCertifiedDepth, preferNewerVersion,
            hrl, hlr, hrv]
        have hnle : ¬ BackendSelectionRank.le (replayRank left) (replayRank right) := by
          simp [BackendSelectionRank.le, replayRank, hdepth]
          omega
        rw [hchoose, BackendSelectionRank.max_eq_left hnle]
      · by_cases hlv :
          left.backend.compilerMajorVersion < right.backend.compilerMajorVersion
        · have hchoose :
              (preferGreaterCertifiedDepth.thenBy preferNewerVersion).choose left right = right := by
            simp [choose, thenBy, preferGreaterCertifiedDepth, preferNewerVersion,
              hrl, hlr, hrv, hlv]
          have hle : BackendSelectionRank.le (replayRank left) (replayRank right) := by
            exact Or.inr ⟨hdepth, Nat.le_of_lt hlv⟩
          rw [hchoose, BackendSelectionRank.max_eq_right hle]
        · have hver :
              left.backend.compilerMajorVersion =
                right.backend.compilerMajorVersion := by
            omega
          have hchoose :
              (preferGreaterCertifiedDepth.thenBy preferNewerVersion).choose left right = left := by
            simp [choose, thenBy, preferGreaterCertifiedDepth, preferNewerVersion,
              hrl, hlr, hrv, hlv]
          have hle : BackendSelectionRank.le (replayRank left) (replayRank right) := by
            exact Or.inr ⟨hdepth, Nat.le_of_eq hver⟩
          have hrank : replayRank left = replayRank right := by
            simp [replayRank, hdepth, hver]
          rw [hchoose, BackendSelectionRank.max_eq_right hle]
          exact hrank

private theorem replayKeyEqOfQueryRankEq
    (left right : CertifiedPreprocessorBackend) (query : BackendQuery)
    (hleft : left.matchesQuery query)
    (hright : right.matchesQuery query)
    (hrank : replayRank left = replayRank right) :
    left.key = right.key := by
  have hver :
      left.backend.compilerMajorVersion = right.backend.compilerMajorVersion :=
    congrArg BackendSelectionRank.compilerMajorVersion hrank
  apply backendKey_eq_of_fields
  · exact hleft.1.trans hright.1.symm
  · exact hver
  · exact hleft.2.trans hright.2.symm

/-- Certified replay selection: maximize formally witnessed replay depth, then
    compiler major version. -/
def replayWellFormed : WellFormedSelectionPolicy :=
  { policy := preferGreaterCertifiedDepth.thenBy preferNewerVersion
    rank := replayRank
    choose_rank := replayChooseRank
    key_eq_of_query_rank_eq := replayKeyEqOfQueryRankEq }

private theorem selectAux_rank
    (wellFormed : WellFormedSelectionPolicy)
    (current : CertifiedPreprocessorBackend)
    (rest : List CertifiedPreprocessorBackend) :
    wellFormed.rank (selectAux wellFormed.policy current rest) =
      (rest.map wellFormed.rank).foldl BackendSelectionRank.max
        (wellFormed.rank current) := by
  induction rest generalizing current with
  | nil =>
      rfl
  | cons next rest ih =>
      change wellFormed.rank
          (selectAux wellFormed.policy (wellFormed.policy.choose current next) rest) =
        (rest.map wellFormed.rank).foldl BackendSelectionRank.max
          (BackendSelectionRank.max (wellFormed.rank current) (wellFormed.rank next))
      rw [ih]
      rw [wellFormed.choose_rank]

private theorem select_rank_eq_aggregate_of_ne_nil
    (wellFormed : WellFormedSelectionPolicy)
    (candidates : List CertifiedPreprocessorBackend)
    (hne : candidates ≠ []) :
    (wellFormed.policy.select candidates).map wellFormed.rank =
      some (BackendSelectionRank.aggregate (candidates.map wellFormed.rank)) := by
  cases candidates with
  | nil =>
      exact (hne rfl).elim
  | cons first rest =>
      simp only [select, Option.map_some]
      rw [selectAux_rank]
      simp [BackendSelectionRank.aggregate, BackendSelectionRank.max_bottom_left]

/-- A well-formed policy selects the same backend identity from any two
    permutations whose candidates all belong to the same backend query. -/
theorem select_key_eq_of_perm_of_matches
    (wellFormed : WellFormedSelectionPolicy)
    (query : BackendQuery)
    {left right : List CertifiedPreprocessorBackend}
    (hperm : left.Perm right)
    (hleft : ∀ backend, backend ∈ left → backend.matchesQuery query)
    (hright : ∀ backend, backend ∈ right → backend.matchesQuery query) :
    (wellFormed.policy.select left).map CertifiedPreprocessorBackend.key =
      (wellFormed.policy.select right).map CertifiedPreprocessorBackend.key := by
  by_cases hnil : left = []
  · subst left
    have hrightNil : right = [] := by
      exact hperm.nil_eq.symm
    subst right
    rfl
  · have hrightNonempty : right ≠ [] := by
      intro hrightNil
      subst right
      exact hnil hperm.eq_nil
    have hleftRank :=
      select_rank_eq_aggregate_of_ne_nil wellFormed left hnil
    have hrightRank :=
      select_rank_eq_aggregate_of_ne_nil wellFormed right hrightNonempty
    have haggregate :
        BackendSelectionRank.aggregate (left.map wellFormed.rank) =
          BackendSelectionRank.aggregate (right.map wellFormed.rank) :=
      BackendSelectionRank.aggregate_eq_of_perm (hperm.map wellFormed.rank)
    have hranks :
        (wellFormed.policy.select left).map wellFormed.rank =
          (wellFormed.policy.select right).map wellFormed.rank := by
      rw [hleftRank, hrightRank, haggregate]
    cases hlsel : wellFormed.policy.select left with
    | none =>
        rw [hlsel] at hleftRank
        simp at hleftRank
    | some leftBackend =>
        cases hrsel : wellFormed.policy.select right with
        | none =>
            rw [hrsel] at hrightRank
            simp at hrightRank
        | some rightBackend =>
            have hleftMem := select_mem wellFormed.policy left leftBackend hlsel
            have hrightMem := select_mem wellFormed.policy right rightBackend hrsel
            have hrankEq :
                wellFormed.rank leftBackend = wellFormed.rank rightBackend := by
              rw [hlsel, hrsel] at hranks
              simp only [Option.map_some] at hranks
              exact Option.some.inj hranks
            have hkey := wellFormed.key_eq_of_query_rank_eq
              leftBackend rightBackend query
              (hleft leftBackend hleftMem)
              (hright rightBackend hrightMem)
              hrankEq
            change some leftBackend.key = some rightBackend.key
            exact congrArg Option.some hkey

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

/-- Registry representation order is unobservable for a well-formed policy: if
    two registries contain the same certified entries up to permutation, one
    query and replay IR select the same backend identity. -/
theorem selectSupporting_key_eq_of_entries_perm
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (hentries : left.entries.Perm right.entries) :
    (left.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key =
      (right.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key := by
  have hcandidates :
      (left.supportingCandidates query ir).Perm
        (right.supportingCandidates query ir) := by
    unfold supportingCandidates
    exact hentries.filter _
  apply BackendSelectionPolicy.select_key_eq_of_perm_of_matches
    wellFormed query hcandidates
  · intro backend hmem
    exact (left.mem_supportingCandidates_iff query ir backend).1 hmem |>.2.1
  · intro backend hmem
    exact (right.mem_supportingCandidates_iff query ir backend).1 hmem |>.2.1

end PreprocessorBackendRegistry

end Producer
end CMeta
