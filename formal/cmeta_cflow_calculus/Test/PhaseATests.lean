import CMetaCFlowCalculus.CMeta.Environment
import CMetaCFlowCalculus.Proofs.Ownership
import CMetaCFlowCalculus.CFlow.Syntax
import PhaseATests.PhaseB
import PhaseATests.PhaseC
import PhaseATests.PhaseD
import PhaseATests.PhaseE
import PhaseATests.PhaseF
import PhaseATests.PhaseG
import PhaseATests.SignatureManifest
import PhaseATests.OperatorPolicyManifest
import PhaseATests.FiniteCompute
import PhaseATests.Mailbox
import PhaseATests.MachineSchema
import PhaseATests.Machine
import PhaseATests.MachineHierarchy
import PhaseATests.MachineRuntime
import PhaseATests.ManagedMachineState
import PhaseATests.Readiness
import PhaseATests.TimerEvent
import PhaseATests.Temporal
import PhaseATests.Actor

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests

def userTy : Ty := .named "User"
def nameTy : Ty := .named "Name"
def nameListTy : Ty := .named "NameList"
def intTy : Ty := .named "Int"

def stableProperties : PropertySet :=
  PropertySet.ofList [.deterministic, .total, .stable]

def coreEnv : Env where
  hasCapability := fun ty capability =>
    ty = userTy ∧ (capability = .copy ∨ capability = .move)
  declaresUnary := fun callable input output effect _ =>
    effect = .pure ∧
      ((callable = "normalize" ∧ input = userTy ∧ output = userTy) ∨
       (callable = "active" ∧ input = userTy ∧ output = .bool) ∨
       (callable = "name" ∧ input = userTy ∧ output = nameTy) ∨
       (callable = "aliases" ∧ input = userTy ∧ output = .range nameTy))
  declaresBinary := fun callable left right output effect _ =>
    callable = "sum" ∧ left = intTy ∧ right = intTy ∧ output = intTy ∧
      effect = .pure
  declaresSource := fun source output =>
    (source = "users" ∧ output = userTy) ∨
      (source = "ints" ∧ output = intTy)
  declaresCollector := fun collector input result =>
    collector = "names" ∧ input = nameTy ∧ result = nameListTy

def normalize : UnaryCallable coreEnv userTy userTy where
  name := "normalize"
  effect := .pure
  properties := stableProperties
  declared := by simp [coreEnv, userTy]

example : Effect.pure ≤ Effect.readOnly := by decide
example : Effect.readOnly ≤ Effect.stateful := by decide
example : Effect.stateful ≤ Effect.external := by decide

example : Std.IsPartialOrder Effect := inferInstance
example : Std.LawfulOrderSup Effect := inferInstance

example : Effect.join .pure .readOnly = .readOnly := rfl
example : Effect.join .readOnly .stateful = .stateful := rfl

example (left right : Effect) :
    Effect.join left right = Effect.join right left :=
  Effect.join_comm left right

example (first second third : Effect) :
    Effect.join (Effect.join first second) third =
      Effect.join first (Effect.join second third) :=
  Effect.join_assoc first second third

example (left right : Effect) : left ≤ Effect.join left right :=
  Effect.le_join_left left right

example (left right upper : Effect) (leftBound : left ≤ upper)
    (rightBound : right ≤ upper) : Effect.join left right ≤ upper :=
  Effect.join_least left right upper leftBound rightBound

example : stableProperties .deterministic := by
  simp [stableProperties, PropertySet.ofList]

example : coreEnv.hasCapability userTy .copy := by
  simp [coreEnv, userTy]

example : normalize.effect = .pure := rfl

def borrowedUser : Value userTy where
  token := 1

theorem userCopyable : coreEnv.hasCapability userTy .copy := by
  simp [coreEnv, userTy]

def borrowedContext : OwnershipContext := fun token =>
  if token = borrowedUser.token then
    some { ty := userTy, ownership := .borrowed }
  else none

theorem borrowedContext_has_user :
    borrowedContext borrowedUser.token =
      some { ty := userTy, ownership := .borrowed } := by
  simp [borrowedContext, borrowedUser]

def retainedContext : OwnershipContext :=
  retain (Γ := coreEnv) borrowedContext borrowedUser
    borrowedContext_has_user userCopyable

theorem userMovable : coreEnv.hasCapability userTy .move := by
  simp [coreEnv, userTy]

def ownedContext : OwnershipContext := fun token =>
  if token = 2 then some { ty := userTy, ownership := .owned } else none

def ownedUser : Value userTy where
  token := 2

theorem ownedContext_has_user :
    ownedContext 2 = some { ty := userTy, ownership := .owned } := by
  simp [ownedContext]

def movedContext : OwnershipContext :=
  move (Γ := coreEnv) ownedContext ownedUser ownedContext_has_user userMovable

example : SuspendSafe retainedContext [borrowedUser.pack] :=
  retain_suspend_safe (Γ := coreEnv) borrowedContext borrowedUser
    borrowedContext_has_user userCopyable

example : ¬SuspendSafe borrowedContext [borrowedUser.pack] :=
  borrowed_not_suspend_safe borrowedContext borrowedUser borrowedContext_has_user

example : movedContext 2 = some { ty := userTy, ownership := .moved } :=
  move_updates_source (Γ := coreEnv) ownedContext ownedUser
    ownedContext_has_user userMovable

example : ¬ContextReadable movedContext ownedUser :=
  move_source_not_readable (Γ := coreEnv) ownedContext ownedUser
    ownedContext_has_user userMovable

example : ¬SuspendSafe movedContext [ownedUser.pack] :=
  move_source_not_suspend_safe (Γ := coreEnv) ownedContext ownedUser
    ownedContext_has_user userMovable

example : movedContext 3 = ownedContext 3 :=
  move_preserves_other (Γ := coreEnv) ownedContext ownedUser
    ownedContext_has_user userMovable (by decide)

def usersSource : SourceDecl coreEnv userTy where
  name := "users"
  declared := by simp [coreEnv, userTy]

def active : UnaryCallable coreEnv userTy .bool where
  name := "active"
  effect := .pure
  properties := stableProperties
  declared := by simp [coreEnv, userTy]

def userName : UnaryCallable coreEnv userTy nameTy where
  name := "name"
  effect := .pure
  properties := stableProperties
  declared := by simp [coreEnv, userTy, nameTy]

def aliases : UnaryCallable coreEnv userTy (.range nameTy) where
  name := "aliases"
  effect := .pure
  properties := stableProperties
  declared := by simp [coreEnv, userTy, nameTy]

def namesCollector : CollectorDecl coreEnv nameTy nameListTy where
  name := "names"
  declared := by simp [coreEnv, nameTy, nameListTy]

def intsSource : SourceDecl coreEnv intTy where
  name := "ints"
  declared := by simp [coreEnv, intTy]

def sum : BinaryCallable coreEnv intTy intTy intTy where
  name := "sum"
  effect := .pure
  properties := PropertySet.ofList [.associative]
  declared := by simp [coreEnv, intTy]

def collectedNames : Flow coreEnv userTy nameListTy .terminal :=
  .collect namesCollector
    (.limit 100 (.map userName (.filter active (.source usersSource))))

def skippedAliases : Flow coreEnv userTy nameTy .stream :=
  .skip 2 (.flatMap aliases (.source usersSource))

def reducedInts : Flow coreEnv intTy intTy .stream :=
  .reduce sum (.source intsSource)

example : Flow coreEnv userTy nameListTy .terminal := collectedNames
example : Flow coreEnv userTy nameTy .stream := skippedAliases
example : Flow coreEnv intTy intTy .stream := reducedInts
example : TerminalWitness coreEnv userTy nameListTy :=
  terminalWitness collectedNames

def permissiveEnv : Env where
  hasCapability := fun _ _ => True
  declaresUnary := fun _ _ _ _ _ => True
  declaresBinary := fun _ _ _ _ _ _ => True
  declaresSource := fun _ _ => True
  declaresCollector := fun _ _ _ => True

def scalarTy : Ty := .named "Scalar"

def scalarSource : SourceDecl permissiveEnv scalarTy where
  name := "scalars"
  declared := trivial

def scalarUnary : UnaryCallable permissiveEnv scalarTy scalarTy where
  name := "identity"
  effect := .pure
  properties := PropertySet.empty
  declared := trivial

def scalarPredicate : UnaryCallable permissiveEnv scalarTy .bool where
  name := "predicate"
  effect := .pure
  properties := PropertySet.empty
  declared := trivial

def scalarFlatMap : UnaryCallable permissiveEnv scalarTy (.range scalarTy) where
  name := "expand"
  effect := .pure
  properties := PropertySet.empty
  declared := trivial

def scalarReducer : BinaryCallable permissiveEnv scalarTy scalarTy scalarTy where
  name := "combine"
  effect := .pure
  properties := PropertySet.ofList [.associative]
  declared := trivial

def scalarCollector : CollectorDecl permissiveEnv scalarTy scalarTy where
  name := "scalar"
  declared := trivial

def terminalScalar : Flow permissiveEnv scalarTy scalarTy .terminal :=
  .collect scalarCollector (.source scalarSource)

/-- error: Application type mismatch: The argument -/
#guard_msgs(error, substring := true) in
example : Flow permissiveEnv scalarTy scalarTy .stream :=
  .map scalarUnary terminalScalar

/-- error: Application type mismatch: The argument -/
#guard_msgs(error, substring := true) in
example : Flow permissiveEnv scalarTy scalarTy .stream :=
  .filter scalarPredicate terminalScalar

/-- error: Application type mismatch: The argument -/
#guard_msgs(error, substring := true) in
example : Flow permissiveEnv scalarTy scalarTy .stream :=
  .flatMap scalarFlatMap terminalScalar

/-- error: Application type mismatch: The argument -/
#guard_msgs(error, substring := true) in
example : Flow permissiveEnv scalarTy scalarTy .stream :=
  .limit 1 terminalScalar

/-- error: Application type mismatch: The argument -/
#guard_msgs(error, substring := true) in
example : Flow permissiveEnv scalarTy scalarTy .stream :=
  .skip 1 terminalScalar

/-- error: Application type mismatch: The argument -/
#guard_msgs(error, substring := true) in
example : Flow permissiveEnv scalarTy scalarTy .stream :=
  .reduce scalarReducer terminalScalar

/-- error: Application type mismatch: The argument -/
#guard_msgs(error, substring := true) in
example : Flow permissiveEnv scalarTy scalarTy .terminal :=
  .collect scalarCollector terminalScalar

end CMetaCFlowCalculus.Tests
