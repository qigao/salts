import CMeta.Producer
import CMeta.FmtArgs
import CMeta.NestedReplay
import CMeta.NestedReplayLowering

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
assert_not_exists CMeta.Producer.lowerReplayIR_progress
