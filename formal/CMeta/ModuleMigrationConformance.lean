import CMeta.Calculus
import CMeta.Traits
import CMeta.Callable
import CMeta.Lambda
import CMeta.Dispatch
import CMeta.Flow
import CMeta.Graph
import CMeta.Optimize
import CMeta.Lowering

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

#check CMeta.HArgs
#check CMeta.Callable
#check CMeta.Callable.ofUnary
#check CMeta.Callable.invoke1
#check CMeta.Generator
#check CMeta.CallableDesc
#check CMeta.eraseValue
#check CMeta.eraseGenerator

assert_not_exists CMeta.Callable.compose_beta
assert_not_exists CMeta.Generator.signature_exact
assert_not_exists CMeta.eraseValue_unary
assert_not_exists CMeta.eraseValue_binary
assert_not_exists CMeta.eraseGenerator_preserves_signature

#check CMeta.Lambda
#check CMeta.Lambda.invoke
#check CMeta.Lambda.asCallable
#check CMeta.anonymous
#check CMeta.bindLast

assert_not_exists CMeta.Lambda.beta
assert_not_exists CMeta.Lambda.erasure_semantics
assert_not_exists CMeta.Lambda.erasure_signature_unary
assert_not_exists CMeta.Lambda.erasure_signature_binary
assert_not_exists CMeta.anonymous_beta
assert_not_exists CMeta.bindLast_beta
assert_not_exists CMeta.lambda_bind_same_shape

#check CMeta.Operator
#check CMeta.DispatchRule
#check CMeta.dispatch
#check CMeta.OperatorPolicy
#check CMeta.RulesRespectPolicy
#check CMeta.composeSignature
#check CMeta.inferAndAllow

assert_not_exists CMeta.inferAndAllow_known
assert_not_exists CMeta.dispatch_sound
assert_not_exists CMeta.dispatch_policy_sound

#check CMeta.TypedOp
#check CMeta.stepType
#check CMeta.Pipeline
#check CMeta.checkPipeline
#check CMeta.cflowBuiltInPolicy
#check CMeta.ResolvedStep
#check CMeta.TargetSignatureUnique
#check CMeta.WellFormedDispatch

assert_not_exists CMeta.TypedOp.progress
assert_not_exists CMeta.TypedOp.output_unique
assert_not_exists CMeta.Pipeline.steps_length
assert_not_exists CMeta.ResolvedStep.target_signature_safe
assert_not_exists CMeta.ResolvedStep.cannot_target_incompatible

#check CMeta.RelationResult
#check CMeta.TypedBranches
#check CMeta.TypedBranches.erase
#check CMeta.checkBranches
#check CMeta.ErasedRelation
#check CMeta.TypedRelation
#check CMeta.TypedRelation.erase
#check CMeta.checkRelation
#check CMeta.ErasedStage
#check CMeta.TypedGraph
#check CMeta.TypedGraph.stages
#check CMeta.checkGraph

assert_not_exists CMeta.checkBranchTail
assert_not_exists CMeta.TypedBranches.check_erase
assert_not_exists CMeta.TypedRelation.progress
assert_not_exists CMeta.TypedRelation.output_unique
assert_not_exists CMeta.TypedGraph.progress
assert_not_exists CMeta.TypedGraph.output_unique

#check CMeta.MapChain
#check CMeta.MapChain.run
#check CMeta.MapChain.signatures
#check CMeta.MapChain.check
#check CMeta.FusedMap
#check CMeta.canonicalizeMapLike
#check CMeta.IdempotentEndomap

assert_not_exists CMeta.MapChain.run_cons
assert_not_exists CMeta.canonicalizeMapLike_preserves_type
assert_not_exists CMeta.duplicate_idempotent_elimination_type

#check CMeta.SurfaceZip
#check CMeta.ErasedInvokeRelation
#check CMeta.checkInvokeRelation
#check CMeta.SurfaceZip.lower

assert_not_exists CMeta.SurfaceZip.lowering_progress
assert_not_exists CMeta.SurfaceZip.lowering_output_unique
