import CMeta.TypeIdentity
import CMeta.TypeIdentityGeneratedC

namespace CMeta

private def user : TypeId := .atom "app.User"
private def err : TypeId := .atom "app.Error"
private def resultA : TypeId := .apply "cmeta.Result" [user, err]
private def resultB : TypeId :=
  .apply "cmeta.Result" [.atom "app.User", err]
private def resultReversed : TypeId := .apply "cmeta.Result" [err, user]
private def resultCtor : GenericConstructor :=
  { stableId := "cmeta.Result", minArity := 2, maxArity := 2 }

example :
    TypeIdentityGeneratedC.atomAliasEqual =
      decide (user = TypeId.atom "app.User") := by
  native_decide

example :
    TypeIdentityGeneratedC.atomDifferent = decide (user = err) := by
  native_decide

example :
    TypeIdentityGeneratedC.resultApplicationEqual =
      decide (resultA = resultB) := by
  native_decide

example :
    TypeIdentityGeneratedC.resultArgumentOrderEqual =
      decide (resultA = resultReversed) := by
  native_decide

example :
    TypeIdentityGeneratedC.resultArityAccepted = resultCtor.acceptsArity 2 := by
  native_decide

end CMeta
