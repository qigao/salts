#ifndef CNET_DESTINATION_POLICY_H
#define CNET_DESTINATION_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CNET_DESTINATION_POLICY_VERSION 1u

/*
 * Client-side REMOTE endpoint selection, separate from local SG Owner placement
 * and protocol-aware connection-pool acquisition.  Selection never opens,
 * reconnects, borrows or retries a CNet connection.
 *
 * The host supplies one immutable endpoint-set snapshot, already filtered for
 * authority, TLS/security profile, protocol and caller eligibility.  Endpoint
 * IDs must be nonzero, strictly ascending and stable within the generation.
 * A changed set must publish a new nonzero generation.  The snapshot must
 * outlive this evaluation; later dial/acquisition owns its copied endpoint
 * identity and uses the same trust/authority criteria.
 */
typedef struct cnet_destination_hint {
  uint64_t endpoint_id;
  uint32_t weight;      /* positive for all entries; used by WEIGHTED_RR */
  uint64_t inflight;    /* advisory load, not reserved dial/stream capacity */
  bool eligible;        /* advisory after host authority/health filtering */
} cnet_destination_hint;

typedef enum cnet_destination_policy_kind {
  CNET_DESTINATION_EXPLICIT = 1,
  CNET_DESTINATION_ROUND_ROBIN = 2,
  CNET_DESTINATION_WEIGHTED_RR = 3,
  CNET_DESTINATION_LEAST_INFLIGHT = 4,
  CNET_DESTINATION_STRICT_KEY = 5
} cnet_destination_policy_kind;

typedef struct cnet_destination_selection {
  size_t size;
  uint32_t version;
  cnet_destination_policy_kind kind;
  const cnet_destination_hint *endpoints;
  size_t endpoint_count;
  uint64_t snapshot_generation;
  uint64_t expires_at_ms; /* monotonic; UINT64_MAX = immutable/no expiry */
  uint64_t now_ms;        /* same clock domain; caller-provided */
  uint64_t sequence;      /* host-provided ticket and tie-break seed */
  uint64_t explicit_endpoint_id;
  uint64_t key_hash;
  bool key_known;
} cnet_destination_selection;

typedef struct cnet_destination_result {
  uint64_t snapshot_generation;
  uint64_t endpoint_id;
  size_t index; /* SIZE_MAX on any error */
} cnet_destination_result;

/* Validate once when publishing a host-owned immutable endpoint-set snapshot.
 * EINVAL for null, empty, unsorted, duplicate IDs or zero weights.
 * Does not allocate or change source storage.
 */
int cnet_destination_validate(const cnet_destination_hint *endpoints, size_t count);

/*
 * Select an advisory candidate at new dial/acquisition admission.
 * EINVAL on malformed/versioned input, unknown strict key or unsupported kind;
 * ETIMEDOUT for an expired snapshot; ERANGE on weighted sum overflow;
 * ENOENT for an unknown explicit ID; ENOBUFS when no allowed remote candidate.
 *
 * EXPLICIT and STRICT_KEY never redirect when their pinned endpoint is FULL.
 * Health/pressure snapshots are not reservations.  The actual dial/transport
 * and protocol-capacity admission is still responsible for final rejection.
 * No hidden retry, alternate TLS profile, origin coalescing or fallback occurs.
 */
int cnet_destination_choose(const cnet_destination_selection *selection,
                            cnet_destination_result *out);

#ifdef __cplusplus
}
#endif

#endif
