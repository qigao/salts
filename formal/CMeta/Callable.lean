module
public import CMeta.Traits

/-!
# Typed callables

CMeta's runtime `cmeta_callable` is type-erased and already receives logical
arguments through an array. The formal typed side therefore uses one finite
argument schema rather than an arity-specific callable type family.
-/

namespace CMeta

/-- Semantic interpretation of the logical CMeta type universe. -/
public def CType.denote : CType → Type
  | .bool => Bool
  | .int => Int
  | .long => Int
  | .float => Float
  | .double => Float

/-- Heterogeneous argument values indexed by one finite ordered CType schema. -/
public inductive HArgs : List CType → Type where
  | nil : HArgs []
  | cons {t : CType} {ts : List CType} : t.denote → HArgs ts → HArgs (t :: ts)

namespace HArgs

/-- Construct the exact one-argument value used by a unary backend adapter. -/
-- TEMP-MODULE-BRIDGE(M6): legacy OptimizerConformance.identity_lists_equal
@[expose] public def one (x : A.denote) : HArgs [A] := .cons x .nil

/-- Construct the exact two-argument value used by a binary backend adapter. -/
public def two (a : A.denote) (b : B.denote) : HArgs [A, B] :=
  .cons a (.cons b .nil)

/-- Concatenate two heterogeneous argument lists while preserving the type schema. -/
public def append : HArgs xs → HArgs ys → HArgs (xs ++ ys)
  | .nil, right => right
  | .cons x rest, right => .cons x (append rest right)

/-- Append one logical last argument; this is the semantic primitive for bind-last. -/
public def snoc (xs : HArgs Args) (x : B.denote) : HArgs (Args ++ [B]) :=
  xs.append (one x)

end HArgs

/-- One typed value callable over an arbitrary finite argument schema. -/
public structure Callable (Args : List CType) (R : CType) where
  run : HArgs Args → R.denote

namespace Callable

/-- Wrap an ordinary unary function in the unified finite-argument callable. -/
-- TEMP-MODULE-BRIDGE(M6): legacy OptimizerConformance.identity_lists_equal
@[expose] public def ofUnary (f : A.denote → R.denote) : Callable [A] R :=
  ⟨fun | .cons x .nil => f x⟩

/-- Wrap an ordinary binary function in the unified finite-argument callable. -/
public def ofBinary (f : A.denote → B.denote → R.denote) : Callable [A, B] R :=
  ⟨fun | .cons a (.cons b .nil) => f a b⟩

/-- Typed invocation for the general finite-arity model. -/
public def invoke (f : Callable Args R) (xs : HArgs Args) : R.denote := f.run xs

/-- Unary convenience is an operation on the general Callable, not a type. -/
-- TEMP-MODULE-BRIDGE(M6): legacy OptimizerConformance.identity_lists_equal
@[expose] public def invoke1 (f : Callable [A] R) (x : A.denote) : R.denote :=
  f.run (HArgs.one x)

/-- Binary convenience is an operation on the general Callable, not a type. -/
public def invoke2 (f : Callable [A, B] R)
    (a : A.denote) (b : B.denote) : R.denote :=
  f.run (HArgs.two a b)

/-- Current unary C backend signature projection. This is backend metadata,
    not an arity-specific callable semantic type. -/
public def unaryBackendSignature (_ : Callable [A] R) : Signature := .unary A R

/-- Current binary C backend signature projection. This is backend metadata,
    not an arity-specific callable semantic type. -/
public def binaryBackendSignature (_ : Callable [A, B] R) : Signature := .binary A B R

/-- Ordinary higher-order composition is representable without a new callable ABI. -/
public def compose (g : Callable [B] R) (f : Callable [A] B) : Callable [A] R :=
  ofUnary (fun x => g.invoke1 (f.invoke1 x))

theorem compose_beta (g : Callable [B] R) (f : Callable [A] B)
    (x : A.denote) :
    (compose g f).invoke1 x = g.invoke1 (f.invoke1 x) := rfl

end Callable

/-- Zero logical arguments are represented by the same Callable model. -/
private def zeroArgumentExample {R : CType} (result : R.denote) : Callable [] R :=
  ⟨fun | .nil => result⟩

/-- Higher finite arity is represented without introducing another callable type. -/
private def threeArgumentFirst {A B C : CType} : Callable [A, B, C] A :=
  ⟨fun | .cons a (.cons _ (.cons _ .nil)) => a⟩

example {R : CType} (result : R.denote) :
    (zeroArgumentExample result).run .nil = result := rfl

example {A B C : CType}
    (a : A.denote) (b : B.denote) (c : C.denote) :
    threeArgumentFirst.run (.cons a (.cons b (.cons c .nil))) = a := rfl

/-- Generator is a separate protocol, not a value lambda whose arity happens to
    include output-buffer/cursor implementation parameters. -/
public structure Generator (A R : CType) (State : Type) where
  run : A.denote → State → Option (R.denote × State)

namespace Generator

/-- Logical generator signature remains independent from its runtime protocol mechanics. -/
public def signature (_ : Generator A R State) : Signature := .generator A R

theorem signature_exact (g : Generator A R State) :
    g.signature = .generator A R := rfl

end Generator

/-- Runtime descriptor after type erasure. -/
public structure CallableDesc where
  sig : Signature
  deriving Repr, DecidableEq

/-- Existing C backends currently have concrete unary and binary value-function
    pointer families. Erasure is therefore partial for the general semantic
    model until more finite backend signatures are admitted. -/
public def eraseValue {Args : List CType} {R : CType}
    (_ : Callable Args R) : Option CallableDesc :=
  match Args with
  | [a] => some ⟨.unary a R⟩
  | [a, b] => some ⟨.binary a b R⟩
  | _ => none

public def eraseGenerator (_ : Generator A R State) : CallableDesc := ⟨.generator A R⟩

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
