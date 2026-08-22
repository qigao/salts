#include <cmeta/meta.h>

#ifdef Containers
#error "Containers(...) is removed; use one typed(...) declaration per type"
#endif

#ifdef CMETA_TRAITS_POSITIONAL
#error "positional Traits compatibility is removed; use tagged rows"
#endif

#define REQUIRE(expr) do { if (!(expr)) return __LINE__; } while (0)

Struct(SurfacePoint,
    (int, x),
    (long, y)
);

Enum(SurfaceState,
    (SURFACE_READY, "ready"),
    (SURFACE_DONE, 20, "done"),
    (SURFACE_AFTER, "after")
);

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

Traits(SurfaceBox,
    (equal, surface_box_equal),
    (hash, surface_box_hash),
    (compare, surface_box_compare),
    (copy, surface_box_copy),
    (move, surface_box_move),
    (destroy, surface_box_destroy)
);

typed(Option, SurfaceMaybeInt, int);
typed(Pair, SurfacePair, int, long);
typed(Tuple, SurfaceTuple3, int, long, double);
typed(Result, SurfaceResult, int, int);

typed_any(value, int, surface_increment, (int value)) {
    return value + 1;
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

int main(void) {
    SurfacePoint point = { .x = 3, .y = 4 };
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
    static const char *const contract_names[] = {
        "unknown", "value", "pure", "idempotent", "associative",
        "fallible", "io", "async", "stateful"
    };
    size_t i;

    REQUIRE(point.x == 3 && point.y == 4L);
    REQUIRE(strcmp(StructMeta(SurfacePoint)->name, "SurfacePoint") == 0);
    REQUIRE(FieldCount(SurfacePoint) == 2u);
    REQUIRE(field != NULL);
    REQUIRE(strcmp(field->name, "y") == 0);
    REQUIRE(field->offset == offsetof(SurfacePoint, y));

    REQUIRE(SURFACE_READY == 0);
    REQUIRE(SURFACE_DONE == 20);
    REQUIRE(SURFACE_AFTER == 21);
    REQUIRE(EnumParse(SurfaceState, "done", &state));
    REQUIRE(state == SURFACE_DONE);
    REQUIRE(strcmp(EnumString(SurfaceState, state), "done") == 0);
    REQUIRE(strcmp(EnumSymbol(SurfaceState, state), "SURFACE_DONE") == 0);

    REQUIRE((cmeta_traits_SurfaceBox.flags & callable_traits) == callable_traits);
    REQUIRE(cmeta_traits_SurfaceBox.equal(&left, &right));
    REQUIRE(cmeta_traits_SurfaceBox.hash(&left) == 7u);
    REQUIRE(cmeta_traits_SurfaceBox.compare(&left, &right) == 0);
    REQUIRE(cmeta_traits_SurfaceBox.copy_construct(&copied, &left));
    REQUIRE(copied.value == 7);
    cmeta_traits_SurfaceBox.move_construct(&moved, &copied);
    REQUIRE(moved.value == 7 && copied.value == 0);
    cmeta_traits_SurfaceBox.destroy(&moved);
    REQUIRE(moved.value == 0);

    REQUIRE(OptionHas(some));
    REQUIRE(some.value == 9);
    REQUIRE(!OptionHas(none));
    REQUIRE(pair.first == 4 && pair.second == 5L);
    REQUIRE(TupleArity(SurfaceTuple3) == 3u);
    REQUIRE(tuple.v0 == 1 && tuple.v1 == 2L && tuple.v2 == 3.5);
    REQUIRE(ResultIsOk(ok) && ok.data.value == 11);
    REQUIRE(ResultIsErr(err) && err.data.error == 23);

    REQUIRE(cmeta_callable_contract_valid(surface_increment));
    REQUIRE(cmeta_callable_invoke(&surface_increment, &output, args));
    REQUIRE(output == 7);
    REQUIRE(surface_increment.meta.effects == CMETA_CONTRACT_EFFECTS(value));
    REQUIRE(surface_increment.meta.properties == CMETA_CONTRACT_PROPERTIES(value));

    REQUIRE(SurfaceCounter_valid(&counter));
    REQUIRE(SurfaceCounter_has(&counter, SURFACE_COUNTER_CAN_RESET));
    REQUIRE(strcmp(SurfaceCounter_implementation(&counter),
                   "surface_counter_impl") == 0);
    REQUIRE(SurfaceCounter_add(&counter, 5) == 15);
    REQUIRE(SurfaceCounter_value(&counter) == 15);
    SurfaceCounter_reset(&counter);
    REQUIRE(SurfaceCounter_value(&counter) == 0);
    REQUIRE(interface_meta != NULL);
    REQUIRE(interface_meta->method_count == 3u);

    for (i = 0u; i < sizeof(contract_names) / sizeof(contract_names[0]); ++i)
        REQUIRE(cmeta_contract_find(contract_names[i]) != NULL);

    return 0;
}
