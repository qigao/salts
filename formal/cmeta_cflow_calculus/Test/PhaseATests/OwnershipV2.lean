import CMetaCFlowCalculus.Proofs.Ownership

open CMetaCFlowCalculus.CMeta

namespace CMetaCFlowCalculus.Tests.OwnershipV2

def resourceTy : Ty := .named "Resource"
def leaseTy : Ty := .named "PluginLease"
def descriptorTy : Ty := .named "FunctionDesc"

def sharedResource : Value resourceTy where
  token := 20

def sharedContext : OwnershipContext := fun token =>
  if token = sharedResource.token then
    some { ty := resourceTy, ownership := .shared }
  else none

theorem sharedResourceLive :
    HasOwnership sharedContext sharedResource .shared := by
  rfl

example : ContextReadable sharedContext sharedResource :=
  ⟨.shared, sharedResourceLive, .shared⟩

example : ContextNeedsCleanup sharedContext sharedResource :=
  ⟨.shared, sharedResourceLive, .shared⟩

def releasedSharedContext : OwnershipContext :=
  discharge sharedContext sharedResource .shared
    sharedResourceLive NeedsCleanup.shared

example :
    releasedSharedContext sharedResource.token =
      some { ty := resourceTy, ownership := .released } :=
  discharge_updates_source sharedContext sharedResource .shared
    sharedResourceLive NeedsCleanup.shared

example : ¬ContextReadable releasedSharedContext sharedResource :=
  discharge_source_not_readable sharedContext sharedResource .shared
    sharedResourceLive NeedsCleanup.shared

example : ¬ContextNeedsCleanup releasedSharedContext sharedResource :=
  discharge_removes_cleanup sharedContext sharedResource .shared
    sharedResourceLive NeedsCleanup.shared

def pluginLease : Value leaseTy where
  token := 30

def functionView : Value descriptorTy where
  token := 31

def pluginContext : OwnershipContext := fun token =>
  if token = pluginLease.token then
    some { ty := leaseTy, ownership := .owned }
  else if token = functionView.token then
    some { ty := descriptorTy, ownership := .borrowed }
  else none

theorem pluginLeaseOwned :
    HasOwnership pluginContext pluginLease .owned := by
  rfl

theorem functionViewBorrowed :
    HasOwnership pluginContext functionView .borrowed := by
  rfl

def pluginBorrows : BorrowRelations := fun token =>
  if token = functionView.token then some pluginLease.token else none

theorem functionViewBoundToLease :
    BorrowedFrom pluginContext pluginBorrows functionView pluginLease := by
  exact ⟨functionViewBorrowed, rfl,
    ⟨.owned, pluginLeaseOwned, LifetimeAuthority.owned⟩⟩

def releasedPluginContext : OwnershipContext :=
  discharge pluginContext pluginLease .owned
    pluginLeaseOwned NeedsCleanup.owned

example :
    ¬BorrowedFrom
      releasedPluginContext pluginBorrows functionView pluginLease :=
  borrowed_from_invalid_after_owner_discharge
    pluginContext pluginBorrows functionView pluginLease .owned
    pluginLeaseOwned NeedsCleanup.owned

example : ¬ContextAuthority releasedPluginContext pluginLease :=
  discharge_source_not_authority pluginContext pluginLease .owned
    pluginLeaseOwned NeedsCleanup.owned

example : ¬ContextNeedsCleanup releasedPluginContext pluginLease :=
  discharge_removes_cleanup pluginContext pluginLease .owned
    pluginLeaseOwned NeedsCleanup.owned

def reflectedResult : Value resourceTy where
  token := 40

example :
    admitResult (fun _ => none) reflectedResult .unknown = none :=
  reflected_unknown_result_not_admitted (fun _ => none) reflectedResult

example :
    ∃ post,
      admitResult (fun _ => none) reflectedResult .owned = some post ∧
      HasOwnership post reflectedResult .owned :=
  reflected_result_admission_updates_source
    (fun _ => none) reflectedResult .owned .owned rfl

example :
    ∃ post,
      admitResult (fun _ => none) reflectedResult .shared = some post ∧
      HasOwnership post reflectedResult .shared :=
  reflected_result_admission_updates_source
    (fun _ => none) reflectedResult .shared .shared rfl

example :
    ∃ post,
      admitResult (fun _ => none) reflectedResult .borrowed = some post ∧
      HasOwnership post reflectedResult .borrowed :=
  reflected_result_admission_updates_source
    (fun _ => none) reflectedResult .borrowed .borrowed rfl

example :
    ∃ post,
      admitResult (fun _ => none) reflectedResult .value = some post ∧
      HasOwnership post reflectedResult .owned :=
  reflected_result_admission_updates_source
    (fun _ => none) reflectedResult .value .owned rfl

end CMetaCFlowCalculus.Tests.OwnershipV2
