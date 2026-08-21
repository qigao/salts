import CMeta.Traits

/-!
# Typed callables

CMeta's runtime `cmeta_callable` is type-erased and already receives logical
arguments through an array.  The formal typed side therefore uses one finite
argument schema rather than an arity-specific family of Callable1/Callable2/etc.
-/

namespace CMeta

/-- Semantic interpretation of the logical CMeta type universe. -/
def CType.denote : CType → Type
  | .bool => Bool
  | .int => Int
  | .long => Int
  | .float => Float
  | .double => Float

/-- Heterogeneous argument values indexed by one finite ordered CType schema. -/
inductive HArgs : List CType → Type where
  | nil : HArgs []
  | cons {t : CType} {ts : List CType} : t.denote → HArgs ts → HArgs (t :: ts)

namespace HArgs

/-- Construct the exact one-argument value used by a unary backend adapter. -/
def one (x : A.denote) : HArgs [A] := .cons x .nil

/-- Construct the exact two-argument value used by a binary backend adapter. -/
def two (a : A.denote) (b : B.denote) : HArgs [A, B] :=
  .cons a (.cons b .nil)

end HArgs

/-- One typed value callable over an arbitrary finite argument schema. -/
structure Callable (Args : List CType) (R : CType) where
  run : HArgs Args → R.denote

namespace Callable

/-- Typed invocation for the general finite-arity model. -/
def invoke (f : Callable Args R) (xs : HArgs Args) : R.denote := f.run xs

/-- Unary convenience is an operation on the general Callable, not a type. -/
def invoke1 (f : Callable [A] R) (x : A.denote) : R.denote :=
  f.run (HArgs.one x)

/-- Binary convenience is an operation on the general Callable, not a type. -/
def invoke2 (f : Callable [A, B] R)
    (a : A.denote) (b : B.denote) : R.denote :=
  f.run (HArgs.two a b)

/-- Ordinary higher-order composition is representable without a new callable ABI. -/
def compose (g : Callable [B] R) (f : Callable [A] B) : Callable [A] R :=
  ⟨fun xs =>
    match xs with
    | .cons x .nil => g.invoke1 (f.invoke1 x)⟩

theorem compose_beta (g : Callable [B] R) (f : Callable [A] B)
    (x : A.denote) :
    (compose g f).invoke1 x = g.invoke1 (f.invoke1 x) := rfl

end Callable

/-- Generator is a separate protocol, not a value lambda whose arity happens to
    include output-buffer/cursor implementation parameters. -/
structure Generator (A R : CType) (State : Type) where
  run : A.denote → State → Option (R.denote × State)

namespace Generator

/-- Logical generator signature remains independent from its runtime protocol mechanics. -/
def signature (_ : Generator A R State) : Signature := .generator A R

theorem signature_exact (g : Generator A R State) :
    g.signature = .generator A R := rfl

end Generator

/-- Runtime descriptor after type erasure. -/
structure CallableDesc where
  sig : Signature
  deriving Repr, DecidableEq

/-- Existing C backends currently have concrete unary and binary value-function
    pointer families.  Erasure is therefore partial for the general semantic
    model until more finite backend signatures are admitted. -/
def eraseValue {Args : List CType} {R : CType}
    (_ : Callable Args R) : Option CallableDesc :=
  match Args with
  | [a] => some ⟨.unary a R⟩
  | [a, b] => some ⟨.binary a b R⟩
  | _ => none

def eraseGenerator (_ : Generator A R State) : CallableDesc := ⟨.generator A R⟩

/-- Unary backend erasure is a corollary of the general Callable model. -/
theorem eraseValue_unary (f : Callable [A] R) :
    eraseValue f = some ⟨.unary A R⟩ := rfl

/-- Binary backend erasure is a corollary of the general Callable model. -/
theorem eraseValue_binary (f : Callable [A, B] R) :
    eraseValue f = some ⟨.binary A B R⟩ := rfl

/-- Generator erasure preserves the separate generator protocol signature. -/
theorem eraseGenerator_preserves_signature (g : Generator A R State) :
    (eraseGenerator g).sig = g.signature := rfl

end CMeta
