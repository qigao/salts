import CMeta.NestedReplayGccGeneratedC

/-! Compatibility alias for older GCC-specific nested-replay conformance. -/
namespace CMeta.NestedReplayGeneratedC

def compilerFamilyTag : Nat := CMeta.NestedReplayGccGeneratedC.compilerFamilyTag
def compilerMajorVersion : Nat := CMeta.NestedReplayGccGeneratedC.compilerMajorVersion
def languageStandard : Nat := CMeta.NestedReplayGccGeneratedC.languageStandard
def distinctCount : Nat := CMeta.NestedReplayGccGeneratedC.distinctCount
def depth2Count : Nat := CMeta.NestedReplayGccGeneratedC.depth2Count
def depth3Count : Nat := CMeta.NestedReplayGccGeneratedC.depth3Count
def depth4Count : Nat := CMeta.NestedReplayGccGeneratedC.depth4Count
def certifiedSameProducerDepth : Nat := CMeta.NestedReplayGccGeneratedC.certifiedSameProducerDepth
def directSameProducerAccepted : Bool := CMeta.NestedReplayGccGeneratedC.directSameProducerAccepted
def deferredSameProducerAccepted : Bool := CMeta.NestedReplayGccGeneratedC.deferredSameProducerAccepted
def distinctStrategyTrace : List Nat := CMeta.NestedReplayGccGeneratedC.distinctStrategyTrace
def reentryStrategyTrace : List Nat := CMeta.NestedReplayGccGeneratedC.reentryStrategyTrace

end CMeta.NestedReplayGeneratedC
