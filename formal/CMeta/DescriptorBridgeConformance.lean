import CMeta.DescriptorBridge
import CMeta.DescriptorBridgeGeneratedC

namespace CMeta

private def objectLegacy (name : String) (size align : Nat) : LegacyDesc :=
  { name := name, size := size, align := align, kind := .object }

private def legacyA : DescriptorView :=
  { identity := none, legacy := objectLegacy "Legacy" 16 8 }

private def legacyB : DescriptorView :=
  { identity := none, legacy := objectLegacy "Legacy" 16 8 }

private def userA : DescriptorView :=
  { identity := some (.atom "app.User"),
    legacy := objectLegacy "UserA" 16 8 }

private def userB : DescriptorView :=
  { identity := some (.atom "app.User"),
    legacy := objectLegacy "DifferentDisplayName" 32 16 }

private def orderSameLayout : DescriptorView :=
  { identity := some (.atom "app.Order"),
    legacy := objectLegacy "UserA" 16 8 }

private def builtinInt : DescriptorView :=
  { identity := some (.atom "cmeta.int"),
    legacy := { name := "int", size := 4, align := 4, kind := .integer } }

private def intPointer : DescriptorView :=
  { identity := some (.pointer (.atom "cmeta.int")),
    legacy := { name := "int *", size := 8, align := 8,
                kind := .pointer, pointeeKey := some "int" } }

private def longPointer : DescriptorView :=
  { identity := some (.pointer (.atom "cmeta.long")),
    legacy := { name := "long *", size := 8, align := 8,
                kind := .pointer, pointeeKey := some "long" } }

example : DescriptorBridgeGeneratedC.legacyEqual =
    legacyA.semanticEqBool legacyB := by
  native_decide

example : DescriptorBridgeGeneratedC.structuralEqual =
    userA.semanticEqBool userB := by
  native_decide

example : DescriptorBridgeGeneratedC.differentIdentityEqual =
    userA.semanticEqBool orderSameLayout := by
  native_decide

example : DescriptorBridgeGeneratedC.mixedEqual =
    userA.semanticEqBool legacyA := by
  native_decide

example : DescriptorBridgeGeneratedC.builtinIntStructural =
    builtinInt.hasStructuralIdentity := by
  native_decide

example : DescriptorBridgeGeneratedC.pointerDifferentEqual =
    intPointer.semanticEqBool longPointer := by
  native_decide

end CMeta
