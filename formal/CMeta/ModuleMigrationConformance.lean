import CMeta.Calculus
import CMeta.Traits

#check CMeta.product
#check CMeta.CoreExpr
#check CMeta.CoreExpr.eval
#check CMeta.ppRepeat
#check CMeta.replay

assert_not_exists CMeta.product_length
assert_not_exists CMeta.CoreExpr.eval_length_eq_cardinality
assert_not_exists CMeta.replay_zip

#check CMeta.CType
#check CMeta.Signature
#check CMeta.Traits
#check CMeta.Traits.inferUnary
#check CMeta.SignaturePolicy
#check CMeta.policyAllows

assert_not_exists CMeta.Traits.type_unique
assert_not_exists CMeta.Traits.inferUnary_of_known
assert_not_exists CMeta.Traits.inferUnary_unique
assert_not_exists CMeta.policyAllows_iff
