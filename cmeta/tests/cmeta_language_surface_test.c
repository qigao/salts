#include <cmeta/meta.h>
#include <tinytest.h>

#ifdef Containers
#error "Containers(...) is removed; use one cmeta_type(...) declaration per type"
#endif

#ifdef CMETA_TRAITS_POSITIONAL
#error "positional Traits compatibility is removed; use tagged rows"
#endif


cmeta_struct(SurfacePoint,
    cmeta_field(int, x)
    cmeta_field(long, y)
);

cmeta_enum(SurfaceState,
    (SURFACE_READY, "ready"),
    (SURFACE_DONE, 20, "done"),
    (SURFACE_AFTER, "after")
);

typedef struct SurfaceNode {
    int marker;
} SurfaceNode;

cmeta_struct(SurfaceOwner,
    cmeta_field(int, id)
    cmeta_field(SurfaceNode, node)
);

cmeta_intrusive(SurfaceOwner, node, SurfaceNode);

typedef struct SurfaceBox {
    int value;
} SurfaceBox;

static bool surface_box_equal(const void *left_, const void *right_) {
    const SurfaceBox *left = (const SurfaceBox *)left_;
    const SurfaceBox *right = (const SurfaceBox *)right_;
    return left != NULL && right != NULL && left->value == right->value;
}

static uint64_t surface_box_hash(const void *value_) {
    const SurfaceBox *value = (const SurfaceBox *)value_;
    return value == NULL ? 0u : (uint64_t)(unsigned)value->value;
}

static int surface_box_compare(const void *left_, const void *right_) {
    const SurfaceBox *left = (const SurfaceBox *)left_;
    const SurfaceBox *right = (const SurfaceBox *)right_;
    if (left == NULL || right == NULL) return 0;
    return left->value < right->value ? -1 : left->value > right->value;
}

static bool surface_box_copy(void *destination_, const void *source_) {
    SurfaceBox *destination = (SurfaceBox *)destination_;
    const SurfaceBox *source = (const SurfaceBox *)source_;
    if (destination == NULL || source == NULL) return false;
    *destination = *source;
    return true;
}

static void surface_box_move(void *destination_, void *source_) {
    SurfaceBox *destination = (SurfaceBox *)destination_;
    SurfaceBox *source = (SurfaceBox *)source_;
    if (destination == NULL || source == NULL) return;
    *destination = *source;
    source->value = 0;
}

static void surface_box_destroy(void *value_) {
    SurfaceBox *value = (SurfaceBox *)value_;
    if (value != NULL) value->value = 0;
}

cmeta_traits(SurfaceBox,
    (equal, surface_box_equal),
    (hash, surface_box_hash),
    (compare, surface_box_compare),
    (copy, surface_box_copy),
    (move, surface_box_move),
    (destroy, surface_box_destroy)
);

cmeta_type(Option, SurfaceMaybeInt, int);
cmeta_type(Pair, SurfacePair, int, long);
cmeta_type(Tuple, SurfaceTuple3, int, long, double);
cmeta_type(Result, SurfaceResult, int, int);

typed_any(value, int, surface_increment, (int value)) {
    return value + 1;
}

FunctionDecl(value, int, surface_reflected_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

int surface_reflected_add(int left, int right) {
    return left + right;
}

enum {
    SURFACE_COUNTER_CAN_RESET = 1u << 0
};

#define SURFACE_COUNTER_METHODS(X, I) \
    X(I, R1, int, add, int, delta) \
    X(I, R0, int, value, _) \
    X(I, V0, void, reset, _)

interface(SurfaceCounter, SURFACE_COUNTER_METHODS);

typedef struct SurfaceCounterState {
    int value;
} SurfaceCounterState;

static int surface_counter_add(void *self, int delta) {
    SurfaceCounterState *state = (SurfaceCounterState *)self;
    state->value += delta;
    return state->value;
}

static int surface_counter_value(void *self) {
    return ((SurfaceCounterState *)self)->value;
}

static void surface_counter_reset(void *self) {
    ((SurfaceCounterState *)self)->value = 0;
}

implements(SurfaceCounter, surface_counter_impl,
           SURFACE_COUNTER_CAN_RESET,
    .add = surface_counter_add,
    .value = surface_counter_value,
    .reset = surface_counter_reset
);

static void test_language_surface(void) {
    SurfacePoint point = { .x = 3, .y = 4 };
    SurfaceOwner owner = { .id = 9, .node = { .marker = 17 } };
    const SurfaceOwner const_owner = { .id = 10, .node = { .marker = 18 } };
    const cmeta_field_desc *field = FieldFind(SurfacePoint, "y");
    SurfaceState state = SURFACE_READY;
    SurfaceBox left = { 7 };
    SurfaceBox right = { 7 };
    SurfaceBox copied = { 0 };
    SurfaceBox moved = { 0 };
    const cmeta_trait_flags callable_traits =
        CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COMPARE |
        CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;
    SurfaceMaybeInt some = OptionSome(SurfaceMaybeInt, 9);
    SurfaceMaybeInt none = OptionNone(SurfaceMaybeInt);
    SurfacePair pair = PairMake(SurfacePair, 4, 5L);
    SurfaceTuple3 tuple = { .v0 = 1, .v1 = 2L, .v2 = 3.5 };
    SurfaceResult ok = ResultOk(SurfaceResult, 11);
    SurfaceResult err = ResultErr(SurfaceResult, 23);
    int input = 6;
    int output = 0;
    const void *args[] = { &input };
    SurfaceCounterState counter_state = { 10 };
    SurfaceCounter counter =
        surface_counter_impl_as_SurfaceCounter(&counter_state);
    const cmeta_interface_desc *interface_meta = SurfaceCounter_interface();
    const cmeta_function_desc *function_meta =
        FunctionMeta(surface_reflected_add);
    static const char *const contract_names[] = {
        "unknown", "value", "pure", "idempotent", "associative",
        "fallible", "io", "async", "stateful"
    };
    size_t i;

    check(point.x == 3 && point.y == 4L);
    check(SurfaceOwner_from_node(&owner.node) == &owner);
    check(SurfaceOwner_from_node_const(&const_owner.node) == &const_owner);
    check(SurfaceOwner_from_node(NULL) == NULL);
    check(strcmp(StructMeta(SurfacePoint)->name, "SurfacePoint") == 0);
    check(FieldCount(SurfacePoint) == 2u);
    check(field != NULL);
    check(strcmp(field->name, "y") == 0);
    check(field->offset == offsetof(SurfacePoint, y));

    check(SURFACE_READY == 0);
    check(SURFACE_DONE == 20);
    check(SURFACE_AFTER == 21);
    check(EnumParse(SurfaceState, "done", &state));
    check(state == SURFACE_DONE);
    check(strcmp(EnumString(SurfaceState, state), "done") == 0);
    check(strcmp(EnumSymbol(SurfaceState, state), "SURFACE_DONE") == 0);

    check((cmeta_traits_SurfaceBox.flags & callable_traits) == callable_traits);
    check(cmeta_traits_SurfaceBox.equal(&left, &right));
    check(cmeta_traits_SurfaceBox.hash(&left) == 7u);
    check(cmeta_traits_SurfaceBox.compare(&left, &right) == 0);
    check(cmeta_traits_SurfaceBox.copy_construct(&copied, &left));
    check(copied.value == 7);
    cmeta_traits_SurfaceBox.move_construct(&moved, &copied);
    check(moved.value == 7 && copied.value == 0);
    cmeta_traits_SurfaceBox.destroy(&moved);
    check(moved.value == 0);

    check(OptionHas(some));
    check(some.value == 9);
    check(!OptionHas(none));
    check(pair.first == 4 && pair.second == 5L);
    check(TupleArity(SurfaceTuple3) == 3u);
    check(tuple.v0 == 1 && tuple.v1 == 2L && tuple.v2 == 3.5);
    check(ResultIsOk(ok) && ok.data.value == 11);
    check(ResultIsErr(err) && err.data.error == 23);

    check(cmeta_callable_contract_valid(surface_increment));
    check(cmeta_callable_invoke(&surface_increment, &output, args));
    check(output == 7);
    check(surface_increment.meta.effects == CMETA_CONTRACT_EFFECTS(value));
    check(surface_increment.meta.properties == CMETA_CONTRACT_PROPERTIES(value));

    check(cmeta_function_desc_valid(function_meta));
    check(strcmp(function_meta->name, "surface_reflected_add") == 0);
    check(function_meta->param_count == 2u);
    check(cmeta_type_equal(function_meta->return_type, &cmeta_type_int));
    check(cmeta_function_find_param(function_meta, "left") != NULL);
    check(surface_reflected_add(3, 4) == 7);

    check(SurfaceCounter_valid(&counter));
    check(SurfaceCounter_has(&counter, SURFACE_COUNTER_CAN_RESET));
    check(strcmp(SurfaceCounter_implementation(&counter),
                   "surface_counter_impl") == 0);
    check(SurfaceCounter_add(&counter, 5) == 15);
    check(SurfaceCounter_value(&counter) == 15);
    SurfaceCounter_reset(&counter);
    check(SurfaceCounter_value(&counter) == 0);
    check(interface_meta != NULL);
    check(interface_meta->method_count == 3u);

    for (i = 0u; i < sizeof(contract_names) / sizeof(contract_names[0]); ++i)
        check(cmeta_contract_find(contract_names[i]) != NULL);

}

suite("CMeta language surface") {
    group("public declarations") {
        it("exposes structures, enums, traits, values, callables and interfaces") {
            test_language_surface();
        }
    }
}
