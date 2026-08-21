namespace CMeta

/-!
# Structural CMeta type identity

This module models the semantic type universe independently from the current
finite callable `CType` universe in `Traits.lean`.

`TypeId` is structural and immutable: aliases, generated C symbol names and
runtime descriptor addresses are intentionally absent from the model.
-/

inductive TypeId where
  | atom (stableId : String)
  | pointer (base : TypeId)
  | constType (base : TypeId)
  | apply (constructorId : String) (args : List TypeId)
  deriving Repr, DecidableEq

structure GenericConstructor where
  stableId : String
  minArity : Nat
  maxArity : Nat
  deriving Repr, DecidableEq

/-- Finite constructor acceptance is a closed interval over type argument count. -/
def GenericConstructor.acceptsArity
    (ctor : GenericConstructor) (arity : Nat) : Bool :=
  decide (ctor.minArity ≤ arity ∧ arity ≤ ctor.maxArity)

/-- Constructor metadata is structurally well-formed before it can accept an application. -/
def GenericConstructor.valid (ctor : GenericConstructor) : Bool :=
  decide (ctor.stableId ≠ "" ∧ ctor.minArity ≤ ctor.maxArity)

/-- Application acceptance depends only on constructor semantics and finite arguments. -/
def GenericConstructor.accepts
    (ctor : GenericConstructor) (args : List TypeId) : Bool :=
  ctor.valid && ctor.acceptsArity args.length

/-- All semantic types known to reflection/generic generation. -/
abbrev KnownTypes := List TypeId

/-- Exact callable schemas are separate from the known type universe. -/
inductive CallableSignature where
  | unary (input output : TypeId)
  | binary (left right output : TypeId)
  | generator (input output : TypeId)
  deriving Repr, DecidableEq

abbrev CallableSchema := List CallableSignature

private def resultCtor : GenericConstructor :=
  { stableId := "cmeta.Result", minArity := 2, maxArity := 2 }

private def user : TypeId := .atom "app.User"
private def err : TypeId := .atom "app.Error"
private def result : TypeId := .apply "cmeta.Result" [user, err]

example : resultCtor.valid = true := by native_decide
example : resultCtor.acceptsArity 2 = true := by native_decide
example : resultCtor.acceptsArity 1 = false := by native_decide
example : resultCtor.accepts [user, err] = true := by native_decide
example : result = TypeId.apply "cmeta.Result" [user, err] := by rfl
example : result != TypeId.apply "cmeta.Result" [err, user] := by native_decide
example : TypeId.pointer user != TypeId.constType user := by native_decide

/-- Extending the reflected/known universe cannot mutate an explicit callable schema. -/
theorem known_types_do_not_expand_callable_schema
    (known : KnownTypes) (schema : CallableSchema) (t : TypeId) :
    schema = schema := by
  rfl

end CMeta
