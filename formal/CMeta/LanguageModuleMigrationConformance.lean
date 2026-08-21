import CMeta.Producer
import CMeta.FmtArgs
import CMeta.NestedReplay
import CMeta.NestedReplayLowering
import CMeta.PreprocessorBackend

#check CMeta.Producer.replay
#check CMeta.Producer.append
#check CMeta.Producer.count
#check CMeta.Producer.storage
#check CMeta.Producer.storageCount
#check CMeta.Producer.canRead

assert_not_exists CMeta.Producer.replay_append
assert_not_exists CMeta.Producer.count_eq_length
assert_not_exists CMeta.Producer.storage_count_eq_count
assert_not_exists CMeta.Producer.canRead_iff

#check CMeta.FmtArgs.Slot
#check CMeta.FmtArgs.legacyStorage
#check CMeta.FmtArgs.normalizedStorage
#check CMeta.FmtArgs.observe
#check CMeta.FmtArgs.legacyDispatch
#check CMeta.FmtArgs.argCountFromStorage
#check CMeta.FmtArgs.canReadRealArg

assert_not_exists CMeta.FmtArgs.legacy_normalized_observational_equivalence
assert_not_exists CMeta.FmtArgs.legacy_dispatch_normalizes
assert_not_exists CMeta.FmtArgs.normalized_guard_implies_physical_bound

#check CMeta.Producer.nestedReplay
#check CMeta.Producer.ReplayBackendCapability
#check CMeta.Producer.ReplayBackendCapability.supportsSameProducerDepth
#check CMeta.Producer.ReplayIR
#check CMeta.Producer.ReplayIR.sameProducerDepth
#check CMeta.Producer.lowerSameProducerDepth
#check CMeta.Producer.LoweredReplayIR
#check CMeta.Producer.lowerReplayIR

assert_not_exists CMeta.Producer.nestedReplay_length
assert_not_exists CMeta.Producer.nestedReplay_count
assert_not_exists CMeta.Producer.lowerSameProducerDepth_iff
assert_not_exists CMeta.Producer.lowerReplayIR_isSome_iff

-- TEMP-MODULE-BRIDGE(M7g): legacy NestedReplayConformance proof consumers
#check CMeta.Producer.nestedReplay_same_length
#check CMeta.Producer.lowerSameProducerDepth_progress
#check CMeta.Producer.lowerReplayIR_progress

#check CMeta.Producer.CompilerFamily
#check CMeta.Producer.CompilerFamily.tag
#check CMeta.Producer.LanguageMode
#check CMeta.Producer.LanguageMode.standardValue
#check CMeta.Producer.BackendKey
#check CMeta.Producer.BackendQuery
#check CMeta.Producer.PreprocessorBackend
#check CMeta.Producer.PreprocessorBackend.key
#check CMeta.Producer.PreprocessorBackend.replayCapability
#check CMeta.Producer.PreprocessorBackend.requiresDeferred
#check CMeta.Producer.PreprocessorBackend.IsReplayCertified
#check CMeta.Producer.CertifiedPreprocessorBackend
#check CMeta.Producer.CertifiedPreprocessorBackend.key
#check CMeta.Producer.CertifiedPreprocessorBackend.replayCapability
#check CMeta.Producer.CertifiedPreprocessorBackend.matchesQuery
#check CMeta.Producer.CertifiedPreprocessorBackend.supportsReplay
#check CMeta.Producer.PreprocessorBackendRegistry
#check CMeta.Producer.PreprocessorBackendRegistry.lookup
#check CMeta.Producer.PreprocessorBackendRegistry.supportingCandidates
#check CMeta.Producer.PreprocessorBackendRegistry.insert
#check CMeta.Producer.PreprocessorBackendRegistry.remove
#check CMeta.Producer.PreprocessorBackendRegistry.replace

assert_not_exists CMeta.Producer.CertifiedPreprocessorBackend.compilerVersionPositive
assert_not_exists CMeta.Producer.CertifiedPreprocessorBackend.deferredSameProducerAccepted
assert_not_exists CMeta.Producer.CertifiedPreprocessorBackend.certifiedDepthPositive
-- TEMP-MODULE-BRIDGE(M7g): legacy Selection and backend conformance candidate proofs
#check CMeta.Producer.PreprocessorBackendRegistry.mem_supportingCandidates_iff
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.insert_eq_none_iff
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.lookup_eq_none_of_key_not_mem
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.lookup_insert_self_exact
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.lookup_remove_self
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.lookup_replace_self_exact
