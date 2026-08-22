module
public import CMeta.NestedReplayGccGeneratedC

/-! Compatibility alias for older GCC-specific nested-replay conformance. -/
namespace CMeta.NestedReplayGeneratedC

public def compilerFamilyTag : Nat := CMeta.NestedReplayGccGeneratedC.compilerFamilyTag
public def compilerMajorVersion : Nat := CMeta.NestedReplayGccGeneratedC.compilerMajorVersion
public def languageStandard : Nat := CMeta.NestedReplayGccGeneratedC.languageStandard
public def distinctCount : Nat := CMeta.NestedReplayGccGeneratedC.distinctCount
public def depth2Count : Nat := CMeta.NestedReplayGccGeneratedC.depth2Count
public def depth3Count : Nat := CMeta.NestedReplayGccGeneratedC.depth3Count
public def depth4Count : Nat := CMeta.NestedReplayGccGeneratedC.depth4Count
public def certifiedSameProducerDepth : Nat := CMeta.NestedReplayGccGeneratedC.certifiedSameProducerDepth
public def directSameProducerAccepted : Bool := CMeta.NestedReplayGccGeneratedC.directSameProducerAccepted
public def deferredSameProducerAccepted : Bool := CMeta.NestedReplayGccGeneratedC.deferredSameProducerAccepted
public def distinctStrategyTrace : List Nat := CMeta.NestedReplayGccGeneratedC.distinctStrategyTrace
public def reentryStrategyTrace : List Nat := CMeta.NestedReplayGccGeneratedC.reentryStrategyTrace

end CMeta.NestedReplayGeneratedC
