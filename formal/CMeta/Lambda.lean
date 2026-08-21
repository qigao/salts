import CMeta.Callable

/-!
# Lambda, anonymous closure, bind and composition

Lambda is one closure model over an arbitrary finite logical argument schema.
The current CFlow lambda1/lambda2 macros are backend/consumer spellings and do
not define the Core formal type system.
-/

namespace CMeta

/-- One finite capture environment plus one body over one finite argument schema. -/
structure Lambda (Env : Type) (Args : List CType) (R : CType) where
  capture : Env
  body : Env → HArgs Args → R.denote

namespace Lambda

/-- Invoke a closure by supplying the exact heterogeneous argument list. -/
def invoke (f : Lambda Env Args R) (xs : HArgs Args) : R.denote :=
  f.body f.capture xs

/-- Erasing the explicit environment yields an ordinary typed callable. -/
def asCallable (f : Lambda Env Args R) : Callable Args R :=
  ⟨fun xs => f.body f.capture xs⟩

/-- Closure beta-reduction is independent of callable arity. -/
theorem beta (f : Lambda Env Args R) (xs : HArgs Args) :
    f.invoke xs = f.body f.capture xs := rfl

/-- Environment erasure preserves invocation for every finite argument schema. -/
theorem erasure_semantics (f : Lambda Env Args R) (xs : HArgs Args) :
    f.asCallable.run xs = f.invoke xs := rfl

/-- Current unary C backend erasure is a corollary of the general Lambda model. -/
theorem erasure_signature_unary (f : Lambda Env [A] R) :
    eraseValue f.asCallable = some ⟨.unary A R⟩ := rfl

/-- Current binary C backend erasure is a corollary of the general Lambda model. -/
theorem erasure_signature_binary (f : Lambda Env [A, B] R) :
    eraseValue f.asCallable = some ⟨.binary A B R⟩ := rfl

end Lambda

/-- Surface-level anonymous construction; names are irrelevant to Core semantics. -/
def anonymous {Env : Type} {Args : List CType} {R : CType}
    (capture : Env) (body : Env → HArgs Args → R.denote) : Lambda Env Args R :=
  ⟨capture, body⟩

/-- Anonymous construction has the same beta law as direct Lambda construction. -/
theorem anonymous_beta {Env : Type} {Args : List CType} {R : CType}
    (capture : Env) (body : Env → HArgs Args → R.denote)
    (xs : HArgs Args) :
    (anonymous capture body).invoke xs = body capture xs := rfl

/-- Partial application of the last argument of a binary semantic function.
    The result is the general Callable instantiated with one remaining argument. -/
def bindLast {A B R : CType} (f : A.denote → B.denote → R.denote)
    (bound : B.denote) : Callable [A] R :=
  ⟨fun xs =>
    match xs with
    | .cons a .nil => f a bound⟩

/-- Binding preserves ordinary beta semantics. -/
theorem bindLast_beta {A B R : CType} (f : A.denote → B.denote → R.denote)
    (bound : B.denote) (a : A.denote) :
    (bindLast f bound).invoke1 a = f a bound := rfl

/-- Binding still erases to the exact unary backend signature currently admitted by CFlow. -/
theorem bindLast_signature {A B R : CType} (f : A.denote → B.denote → R.denote)
    (bound : B.denote) :
    eraseValue (bindLast f bound) = some ⟨.unary A R⟩ := rfl

/-- Capturing a bound value and partial application are the same closure formation,
    stated pointwise without introducing an arity-specific Lambda type. -/
theorem lambda_bind_same_shape {A B R : CType}
    (f : A.denote → B.denote → R.denote) (bound : B.denote)
    (a : A.denote) :
    (bindLast f bound).invoke1 a =
      (anonymous bound (fun cap xs =>
        match xs with
        | .cons x .nil => f x cap)).asCallable.invoke1 a := rfl

end CMeta
