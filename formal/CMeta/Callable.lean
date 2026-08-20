import CMeta.Traits

/-!
# Typed callables

CMeta's runtime `cmeta_callable` is type-erased, but it carries a resolved
signature.  Here we model the typed side before erasure.  If construction goes
through these indexed values, argument/result type safety is obtained by Lean's
type checker rather than by a post-hoc theorem.
-/

namespace CMeta

/-- Semantic interpretation of the logical CMeta type universe. -/
def CType.denote : CType → Type
  | .bool => Bool
  | .int => Int
  | .long => Int
  | .float => Float
  | .double => Float

/-- A typed unary callable. -/
structure Callable1 (A R : CType) where
  run : A.denote → R.denote

/-- A typed binary callable. -/
structure Callable2 (A B R : CType) where
  run : A.denote → B.denote → R.denote

/-- A typed generator step; the state abstracts the cursor protocol. -/
structure Generator1 (A R : CType) (State : Type) where
  run : A.denote → State → Option (R.denote × State)

namespace Callable1

def signature (_ : Callable1 A R) : Signature := .unary A R

theorem signature_exact (f : Callable1 A R) :
    f.signature = .unary A R := rfl

/-- Typed invocation cannot return anything except the declared result type. -/
def invoke (f : Callable1 A R) (x : A.denote) : R.denote := f.run x

/-- Ordinary higher-order composition is representable without adding a new ABI. -/
def compose (g : Callable1 B R) (f : Callable1 A B) : Callable1 A R :=
  ⟨fun x => g.run (f.run x)⟩

theorem compose_beta (g : Callable1 B R) (f : Callable1 A B) (x : A.denote) :
    (compose g f).run x = g.run (f.run x) := rfl

end Callable1

namespace Callable2

def signature (_ : Callable2 A B R) : Signature := .binary A B R

theorem signature_exact (f : Callable2 A B R) :
    f.signature = .binary A B R := rfl

end Callable2

namespace Generator1

def signature (_ : Generator1 A R State) : Signature := .generator A R

theorem signature_exact (g : Generator1 A R State) :
    g.signature = .generator A R := rfl

end Generator1

/-- Runtime descriptor after type erasure. -/
structure CallableDesc where
  sig : Signature
  deriving Repr, DecidableEq

def erase1 (_ : Callable1 A R) : CallableDesc := ⟨.unary A R⟩
def erase2 (_ : Callable2 A B R) : CallableDesc := ⟨.binary A B R⟩
def eraseGenerator (_ : Generator1 A R State) : CallableDesc := ⟨.generator A R⟩

theorem erase1_preserves_signature (f : Callable1 A R) :
    (erase1 f).sig = f.signature := rfl

theorem erase2_preserves_signature (f : Callable2 A B R) :
    (erase2 f).sig = f.signature := rfl

theorem eraseGenerator_preserves_signature (g : Generator1 A R State) :
    (eraseGenerator g).sig = g.signature := rfl

end CMeta
