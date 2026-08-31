#ifndef CNET_SHARDS_H
#define CNET_SHARDS_H

#include "cnet_owner.h"

typedef struct cnet_shards {
  void *impl;
} cnet_shards;

typedef struct cnet_shard_connection {
  uint32_t shard;
  cnet_session_handle session;
} cnet_shard_connection;

typedef struct cnet_shards_config {
  native_io_backend_kind backend_kind;
  size_t shard_count;
  size_t connection_capacity_per_shard;
  size_t command_capacity_per_shard;
  size_t request_capacity_per_shard;
  size_t completion_batch_capacity;
  size_t event_capacity_per_shard;
  size_t receive_buffer_bytes;
  size_t max_command_payload_bytes;
} cnet_shards_config;

typedef struct cnet_shards_layout {
  size_t shard_count;
  size_t connection_capacity_per_shard;
  size_t max_event_payload_bytes;
} cnet_shards_layout;

bool cnet_shard_connection_valid(cnet_shard_connection connection);

/** Starts exactly one long-lived owner task for each configured shard. */
int cnet_shards_init(cnet_shards *shards, const cnet_shards_config *config);
bool cnet_shards_get_layout(const cnet_shards *shards, cnet_shards_layout *out_layout);

/** Reserves one stable shard/session pair and publishes a copied connect command. */
int cnet_shards_connect(cnet_shards *shards, const cnet_owner_connect_payload *payload,
                        cnet_shard_connection *out_connection);
int cnet_shards_send(cnet_shards *shards, cnet_shard_connection connection, const void *data,
                     size_t size);
int cnet_shards_receive(cnet_shards *shards, cnet_shard_connection connection, size_t demand);
int cnet_shards_close(cnet_shards *shards, cnet_shard_connection connection);

int cnet_shards_state(cnet_shards *shards, cnet_shard_connection connection,
                      cnet_session_state *out_state);
int cnet_shards_take_event(cnet_shards *shards, uint32_t shard, cnet_event_view *out_event);
int cnet_shards_release_event(cnet_shards *shards, uint32_t shard, cnet_event_view *event);

/** Consumes the terminal record and releases the stable shard assignment. */
int cnet_shards_recycle(cnet_shards *shards, cnet_shard_connection connection,
                        cnet_session_terminal *out_terminal);

/**
 * Stops quiescent owner tasks within a bounded deadline. Live connections
 * return TURBO_EBUSY; the future client stop protocol closes them first.
 */
int cnet_shards_stop(cnet_shards *shards, uint32_t timeout_ms);
int cnet_shards_destroy(cnet_shards *shards);

#endif /* CNET_SHARDS_H */
