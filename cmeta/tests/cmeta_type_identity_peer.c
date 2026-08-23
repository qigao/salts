#include <cmeta/type_identity.h>

static const cmeta_generic_desc cmeta_peer_pair_generic =
    CMETA_GENERIC_DESC_INIT("test.Pair", "Pair", 2u, 2u, CMETA_GENERIC_VALUE);
static const cmeta_type_identity cmeta_peer_atom_a =
    CMETA_TYPE_ID_ATOM_INIT("test.A");
static const cmeta_type_identity cmeta_peer_atom_b =
    CMETA_TYPE_ID_ATOM_INIT("test.B");
static const cmeta_type_identity *const cmeta_peer_pair_args[] = {
    &cmeta_peer_atom_a, &cmeta_peer_atom_b
};
static const cmeta_type_identity cmeta_peer_pair =
    CMETA_TYPE_ID_APPLY_INIT(&cmeta_peer_pair_generic, cmeta_peer_pair_args);

const cmeta_type_identity *cmeta_type_identity_peer_pair(void) {
    return &cmeta_peer_pair;
}
