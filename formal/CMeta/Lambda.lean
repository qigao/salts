module
public import CMeta.Callable
import all CMeta.Callable

/-!
# Lambda, anonymous closure, bind and composition

Lambda is one closure model over an arbitrary finite logical argument schema.
Consumer-specific C macros do not define the Core formal type system.
-/

namespace CMeta

/-- One finite capture environment plus one body over one finite argument schema. -/
public structure Lambda (Env : Type) (Args : List CType) (R : CType) where
  capture : Env
  body : Env → HArgs Args → R.denote

namespace Lambda

/-- Invoke a closure by supplying the exact heterogeneous argument list. -/
public def invoke (f : Lambda Env Args R) (xs : HArgs Args) : R.denote :=
  f.body f.capture xs

/-- Erasing the explicit environment yields an ordinary typed callable. -/
public def asCallable (f : Lambda Env Args R) : Callable Args R :=
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
public def anonymous {Env : Type} {Args : List CType} {R : CType}
    (capture : Env) (body : Env → HArgs Args → R.denote) : Lambda Env Args R :=
  ⟨capture, body⟩

/-- Anonymous construction has the same beta law as direct Lambda construction. -/
theorem anonymous_beta {Env : Type} {Args : List CType} {R : CType}
    (capture : Env) (body : Env → HArgs Args → R.denote)
    (xs : HArgs Args) :
    (anonymous capture body).invoke xs = body capture xs := rfl

/-- Bind one logical final argument of a finite-arity callable. -/
public def bindLast {Args : List CType} {B R : CType}
    (f : Callable (Args ++ [B]) R) (bound : B.denote) : Callable Args R :=
  ⟨fun xs => f.run (xs.snoc bound)⟩

/-- General bind beta law over any finite prefix argument schema. -/
theorem bindLast_beta {Args : List CType} {B R : CType}
    (f : Callable (Args ++ [B]) R) (bound : B.denote)
    (xs : HArgs Args) :
    (bindLast f bound).run xs = f.run (xs.snoc bound) := rfl

/-- Capturing the bound value and partial application are the same closure formation
    for an arbitrary finite remaining argument schema. -/
theorem lambda_bind_same_shape {Args : List CType} {B R : CType}
    (f : Callable (Args ++ [B]) R) (bound : B.denote)
    (xs : HArgs Args) :
    (bindLast f bound).run xs =
      (anonymous bound (fun cap args => f.run (args.snoc cap))).asCallable.run xs := rfl

/-- The historical binary bind case is now only an instance of the general theorem. -/
example {A B R : CType} (f : Callable [A, B] R)
    (bound : B.denote) (a : A.denote) :
    (bindLast f bound).invoke1 a = f.invoke2 a bound := rfl

end CMeta
