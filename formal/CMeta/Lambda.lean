import CMeta.Callable

/-!
# Lambda, anonymous closure, bind and composition

The current CFlow macros implement closures by copying a capture object into
`cmeta_callable.capture` and using generated invoke/generate adapters.  These
proofs capture the semantic content independently of the byte-level C layout.
-/

namespace CMeta

/-- One-argument capturing lambda. -/
structure Lambda1 (C A R : CType) where
  capture : C.denote
  body : C.denote → A.denote → R.denote

namespace Lambda1

def invoke (f : Lambda1 C A R) (x : A.denote) : R.denote :=
  f.body f.capture x

/-- Erasing the explicit environment yields an ordinary typed callable closure. -/
def asCallable (f : Lambda1 C A R) : Callable1 A R :=
  ⟨fun x => f.body f.capture x⟩

theorem beta (f : Lambda1 C A R) (x : A.denote) :
    f.invoke x = f.body f.capture x := rfl

theorem erasure_semantics (f : Lambda1 C A R) (x : A.denote) :
    f.asCallable.run x = f.invoke x := rfl

theorem erasure_signature (f : Lambda1 C A R) :
    (erase1 f.asCallable).sig = .unary A R := rfl

end Lambda1

/-- Two-argument capturing lambda, corresponding to reduce/zip callbacks. -/
structure Lambda2 (C A B R : CType) where
  capture : C.denote
  body : C.denote → A.denote → B.denote → R.denote

namespace Lambda2

def asCallable (f : Lambda2 C A B R) : Callable2 A B R :=
  ⟨fun a b => f.body f.capture a b⟩

theorem beta (f : Lambda2 C A B R) (a : A.denote) (b : B.denote) :
    f.asCallable.run a b = f.body f.capture a b := rfl

theorem erasure_signature (f : Lambda2 C A B R) :
    (erase2 f.asCallable).sig = .binary A B R := rfl

end Lambda2

/-- Surface-level "anonymous" construction; names are irrelevant to semantics. -/
def anonymous1 {C A R : CType} (capture : C.denote)
    (body : C.denote → A.denote → R.denote) : Lambda1 C A R :=
  ⟨capture, body⟩

theorem anonymous1_beta {C A R : CType} (capture : C.denote)
    (body : C.denote → A.denote → R.denote) (x : A.denote) :
    (anonymous1 capture body).invoke x = body capture x := rfl

/-- Partial application of the last argument, matching `cmeta_bind`. -/
def bindLast {A B R : CType} (f : A.denote → B.denote → R.denote)
    (bound : B.denote) : Callable1 A R :=
  ⟨fun a => f a bound⟩

theorem bindLast_beta {A B R : CType} (f : A.denote → B.denote → R.denote)
    (bound : B.denote) (a : A.denote) :
    (bindLast f bound).run a = f a bound := rfl

/-- Binding changes a binary semantic function into the exact unary ABI expected by CFlow. -/
theorem bindLast_signature {A B R : CType} (f : A.denote → B.denote → R.denote)
    (bound : B.denote) :
    (erase1 (bindLast f bound)).sig = .unary A R := rfl

/-- Capturing lambda and partial application are both ordinary closure formation. -/
theorem lambda_bind_same_shape {A B R : CType}
    (f : A.denote → B.denote → R.denote) (bound : B.denote) :
    (bindLast f bound).run =
      (anonymous1 bound (fun cap a => f a cap)).asCallable.run := rfl

end CMeta