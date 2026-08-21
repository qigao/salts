module
public import CMeta.PreprocessorBackend
import CMeta.PreprocessorBackendRegistryMutation
import all CMeta.PreprocessorBackend

/-!
# Registry observational equivalence

`PreprocessorBackendRegistry` is represented by a list, but clients observe it as
an exact-key finite map. Registry equivalence therefore compares lookup payloads
for every `BackendKey`. The certification proof carried by
`CertifiedPreprocessorBackend` is intentionally projected away: certificates
remain required to construct entries, while proof-object identity is not runtime
registry semantics.
-/

namespace CMeta
namespace Producer
namespace PreprocessorBackendRegistry

/-- Observable registry payload at one exact backend key. -/
public def observe
    (registry : PreprocessorBackendRegistry) (key : BackendKey) :
    Option PreprocessorBackend :=
  (registry.lookup key).map CertifiedPreprocessorBackend.backend

/-- Extensional finite-map equality for registries. List order and certificate
    proof terms are representation details; concrete backend payloads are not. -/
public abbrev Equivalent
    (left right : PreprocessorBackendRegistry) : Prop :=
  ∀ key, left.observe key = right.observe key

/-- Observational equivalence is reflexive. -/
-- TEMP-MODULE-BRIDGE(M7f): legacy LanguageSpec.Rule.eq_refl
public theorem equivalent_refl (registry : PreprocessorBackendRegistry) :
    Equivalent registry registry := by
  intro key
  rfl

/-- Observational equivalence is symmetric. -/
-- TEMP-MODULE-BRIDGE(M7f): legacy LanguageSpec.Rule.eq_symm
public theorem equivalent_symm
    {left right : PreprocessorBackendRegistry}
    (h : Equivalent left right) : Equivalent right left := by
  intro key
  exact (h key).symm

/-- Observational equivalence is transitive. -/
-- TEMP-MODULE-BRIDGE(M7f): legacy LanguageSpec.Rule.eq_trans
public theorem equivalent_trans
    {left middle right : PreprocessorBackendRegistry}
    (hleft : Equivalent left middle)
    (hright : Equivalent middle right) : Equivalent left right := by
  intro key
  exact (hleft key).trans (hright key)

/-- Successful insertion exposes the inserted backend payload at its own key. -/
theorem observe_insert_self
    (registry inserted : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (hinsert : registry.insert backend = some inserted) :
    inserted.observe backend.key = some backend.backend := by
  change (inserted.lookup backend.key).map CertifiedPreprocessorBackend.backend =
    some backend.backend
  rw [lookup_insert_self_exact registry inserted backend hinsert]
  rfl

/-- Successful insertion leaves every unrelated observable payload unchanged. -/
theorem observe_insert_ne
    (registry inserted : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (key : BackendKey)
    (hinsert : registry.insert backend = some inserted)
    (hne : backend.key ≠ key) :
    inserted.observe key = registry.observe key := by
  change (inserted.lookup key).map CertifiedPreprocessorBackend.backend =
    (registry.lookup key).map CertifiedPreprocessorBackend.backend
  rw [lookup_insert_ne_exact registry backend inserted key hinsert hne]

/-- Removal makes its target observably absent. -/
theorem observe_remove_self
    (registry : PreprocessorBackendRegistry) (key : BackendKey) :
    (registry.remove key).observe key = none := by
  change ((registry.remove key).lookup key).map
      CertifiedPreprocessorBackend.backend = none
  rw [lookup_remove_self registry key]
  rfl

/-- Removal leaves every unrelated observable payload unchanged. -/
theorem observe_remove_ne
    (registry : PreprocessorBackendRegistry)
    (target key : BackendKey)
    (hne : target ≠ key) :
    (registry.remove target).observe key = registry.observe key := by
  change ((registry.remove target).lookup key).map
      CertifiedPreprocessorBackend.backend =
    (registry.lookup key).map CertifiedPreprocessorBackend.backend
  rw [lookup_remove_ne_exact registry target key hne]

/-- Pointwise observable semantics of removal. -/
theorem observe_remove
    (registry : PreprocessorBackendRegistry)
    (target key : BackendKey) :
    (registry.remove target).observe key =
      if target = key then none else registry.observe key := by
  by_cases htarget : target = key
  · subst key
    rw [observe_remove_self]
    simp
  · rw [observe_remove_ne registry target key htarget]
    simp [htarget]

/-- Successful replacement exposes the replacement payload at its own key. -/
theorem observe_replace_self
    (registry replaced : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (hreplace : registry.replace backend = some replaced) :
    replaced.observe backend.key = some backend.backend := by
  change (replaced.lookup backend.key).map CertifiedPreprocessorBackend.backend =
    some backend.backend
  rw [lookup_replace_self_exact registry backend replaced hreplace]
  rfl

/-- Successful replacement leaves every unrelated payload unchanged. -/
theorem observe_replace_ne
    (registry replaced : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (key : BackendKey)
    (hreplace : registry.replace backend = some replaced)
    (hne : backend.key ≠ key) :
    replaced.observe key = registry.observe key := by
  change (replaced.lookup key).map CertifiedPreprocessorBackend.backend =
    (registry.lookup key).map CertifiedPreprocessorBackend.backend
  rw [lookup_replace_ne_exact registry backend replaced key hreplace hne]

/-- Removing an absent exact key is extensionally the identity. -/
-- TEMP-MODULE-BRIDGE(M7g): legacy PreprocessorBackendRegistryEquivalenceConformance
public theorem remove_missing_equivalent
    (registry : PreprocessorBackendRegistry)
    (target : BackendKey)
    (hmissing : registry.lookup target = none) :
    Equivalent (registry.remove target) registry := by
  intro key
  by_cases htarget : target = key
  · subst key
    rw [observe_remove_self]
    unfold observe
    rw [hmissing]
    rfl
  · exact observe_remove_ne registry target key htarget

/-- Fresh insertion followed by removal of the inserted exact key restores the
    original registry extensionally. -/
-- TEMP-MODULE-BRIDGE(M7g): legacy PreprocessorBackendRegistryEquivalenceConformance
public theorem insert_remove_equivalent
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (inserted : PreprocessorBackendRegistry)
    (hinsert : registry.insert backend = some inserted) :
    Equivalent (inserted.remove backend.key) registry := by
  have hfresh :
      backend.key ∉ registry.entries.map CertifiedPreprocessorBackend.key := by
    intro hmem
    have hnone : registry.insert backend = none :=
      (insert_eq_none_iff registry backend).2 hmem
    rw [hinsert] at hnone
    cases hnone
  have hmissing : registry.lookup backend.key = none :=
    lookup_eq_none_of_key_not_mem registry backend.key hfresh
  intro key
  by_cases hkey : backend.key = key
  · subst key
    rw [observe_remove_self]
    unfold observe
    rw [hmissing]
    rfl
  · calc
      (inserted.remove backend.key).observe key = inserted.observe key :=
        observe_remove_ne inserted backend.key key hkey
      _ = registry.observe key :=
        observe_insert_ne registry inserted backend key hinsert hkey

/-- Two removals commute extensionally. No distinctness premise is needed: the
    equal-key case is idempotent and the distinct-key case changes disjoint map
    points. -/
-- TEMP-MODULE-BRIDGE(M7g): legacy PreprocessorBackendRegistryEquivalenceConformance
public theorem remove_remove_equivalent
    (registry : PreprocessorBackendRegistry)
    (first second : BackendKey) :
    Equivalent
      ((registry.remove first).remove second)
      ((registry.remove second).remove first) := by
  intro key
  calc
    ((registry.remove first).remove second).observe key =
        if second = key then none else (registry.remove first).observe key :=
      observe_remove (registry.remove first) second key
    _ = if second = key then none else
        (if first = key then none else registry.observe key) := by
      rw [observe_remove registry first key]
    _ = if first = key then none else
        (if second = key then none else registry.observe key) := by
      by_cases hfirst : first = key <;>
        by_cases hsecond : second = key <;>
        simp [hfirst, hsecond]
    _ = ((registry.remove second).remove first).observe key := by
      rw [← observe_remove registry second key]
      rw [← observe_remove (registry.remove second) first key]

/-- Replacing one exact key twice is extensionally last-write-wins. The first
    replacement may change the payload arbitrarily, but after the second
    successful replacement no observation can distinguish it from replacing the
    original registry directly with the final payload. -/
-- TEMP-MODULE-BRIDGE(M7g): legacy PreprocessorBackendRegistryEquivalenceConformance
public theorem replace_last_write_wins
    (registry : PreprocessorBackendRegistry)
    (firstBackend secondBackend : CertifiedPreprocessorBackend)
    (first second direct : PreprocessorBackendRegistry)
    (hkey : firstBackend.key = secondBackend.key)
    (hfirst : registry.replace firstBackend = some first)
    (hsecond : first.replace secondBackend = some second)
    (hdirect : registry.replace secondBackend = some direct) :
    Equivalent second direct := by
  intro key
  by_cases htarget : secondBackend.key = key
  · subst key
    calc
      second.observe secondBackend.key = some secondBackend.backend :=
        observe_replace_self first second secondBackend hsecond
      _ = direct.observe secondBackend.key :=
        (observe_replace_self registry direct secondBackend hdirect).symm
  · have hfirstNe : firstBackend.key ≠ key := by
      intro hsame
      exact htarget (hkey.symm.trans hsame)
    calc
      second.observe key = first.observe key :=
        observe_replace_ne first second secondBackend key hsecond htarget
      _ = registry.observe key :=
        observe_replace_ne registry first firstBackend key hfirst hfirstNe
      _ = direct.observe key :=
        (observe_replace_ne registry direct secondBackend key hdirect htarget).symm

end PreprocessorBackendRegistry
end Producer
end CMeta
