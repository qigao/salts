import CMeta.PreprocessorBackendRegistryEquivalence

/-!
# Registry observational equivalence conformance

The registry is semantically a finite map even though its representation is a
`List`. These tests require an extensional equivalence that observes backend
payloads through exact lookup while ignoring proof-object identity, together
with the basic mutation algebra expected from that finite-map interpretation.
-/

namespace CMeta
namespace Producer

private def gcc14Backend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := 14
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 4 }

private def gcc14DeepBackend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := 14
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 8 }

private def gcc14DeeperBackend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := 14
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 12 }

private def gcc15Backend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := 15
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 8 }

private def clang18Backend : PreprocessorBackend :=
  { compilerFamily := .clang
    compilerMajorVersion := 18
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 4 }

private def gcc14 : CertifiedPreprocessorBackend :=
  ⟨gcc14Backend, by native_decide⟩

private def gcc14Deep : CertifiedPreprocessorBackend :=
  ⟨gcc14DeepBackend, by native_decide⟩

private def gcc14Deeper : CertifiedPreprocessorBackend :=
  ⟨gcc14DeeperBackend, by native_decide⟩

private def gcc15 : CertifiedPreprocessorBackend :=
  ⟨gcc15Backend, by native_decide⟩

private def clang18 : CertifiedPreprocessorBackend :=
  ⟨clang18Backend, by native_decide⟩

private def baseRegistry : PreprocessorBackendRegistry :=
  { entries := [gcc14, clang18]
    uniqueKeys := by native_decide }

/-- Removing a key that is not present is observationally the identity. -/
theorem CPreprocessorBackendRegistryEquivalenceConformance.remove_missing_identity :
    PreprocessorBackendRegistry.Equivalent
      (baseRegistry.remove gcc15.key) baseRegistry := by
  exact PreprocessorBackendRegistry.remove_missing_equivalent
    baseRegistry gcc15.key (by native_decide)

/-- Fresh insertion followed by removal of that same exact key restores the
    original finite-map observation. -/
theorem CPreprocessorBackendRegistryEquivalenceConformance.insert_remove_identity
    (inserted : PreprocessorBackendRegistry)
    (hinsert : baseRegistry.insert gcc15 = some inserted) :
    PreprocessorBackendRegistry.Equivalent
      (inserted.remove gcc15.key) baseRegistry := by
  exact PreprocessorBackendRegistry.insert_remove_equivalent
    baseRegistry gcc15 inserted hinsert

/-- Removal order is a representation detail for two distinct exact keys. -/
theorem CPreprocessorBackendRegistryEquivalenceConformance.remove_commutes :
    PreprocessorBackendRegistry.Equivalent
      ((baseRegistry.remove gcc14.key).remove clang18.key)
      ((baseRegistry.remove clang18.key).remove gcc14.key) := by
  exact PreprocessorBackendRegistry.remove_remove_equivalent
    baseRegistry gcc14.key clang18.key

/-- Consecutive replacement of one exact key is last-write-wins extensionally:
    the intermediate payload cannot be observed after the second replacement. -/
theorem CPreprocessorBackendRegistryEquivalenceConformance.replace_last_write_wins
    (first second direct : PreprocessorBackendRegistry)
    (hfirst : baseRegistry.replace gcc14Deep = some first)
    (hsecond : first.replace gcc14Deeper = some second)
    (hdirect : baseRegistry.replace gcc14Deeper = some direct) :
    PreprocessorBackendRegistry.Equivalent second direct := by
  exact PreprocessorBackendRegistry.replace_last_write_wins
    baseRegistry gcc14Deep gcc14Deeper first second direct
    (by native_decide) hfirst hsecond hdirect

end Producer
end CMeta
