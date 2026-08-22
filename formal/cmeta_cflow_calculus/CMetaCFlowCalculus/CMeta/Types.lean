namespace CMetaCFlowCalculus.CMeta

/-- Semantic type identity used by the calculus, independent of C descriptor addresses. -/
inductive Ty where
  | bool
  | named (name : String)
  | range (element : Ty)
  deriving Repr, DecidableEq

/-- Capabilities that `Γ` may declare for a semantic type. -/
inductive TypeCapability where
  | copy
  | move
  | destroy
  | equal
  | compare
  | hash
  | trivialCopy
  | trivialDestroy
  | noAlias
  deriving Repr, DecidableEq

end CMetaCFlowCalculus.CMeta
