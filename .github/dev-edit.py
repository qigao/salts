from pathlib import Path

path = Path('cnet/src/cnet_shards.c')
text = path.read_text()
old = '''int cnet_shards_send_buffer(cnet_shards *shards, cnet_shard_connection connection,
                            mem_buffer_t *buffer, size_t size) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  const cnet_command command = {.kind = CNET_COMMAND_SEND,
                                .connection = connection.session,
                                .size = size,
                                .retained_buffer = buffer,
                                .retained_data = mem_buffer_const_data(buffer)};
  if (impl == NULL || buffer == NULL || size == 0u) return SALTS_EINVAL;
  if (size > impl->max_command_payload_bytes) return SALTS_EMSGSIZE;
  return cnet_shards_publish(impl, connection, &command);
}
'''
new = '''int cnet_shards_send_buffer(cnet_shards *shards, cnet_shard_connection connection,
                            mem_buffer_t *buffer, size_t size) {
  cnet_shards_impl *impl = cnet_shards_get(shards);
  cnet_command command;
  if (impl == NULL || buffer == NULL || size == 0u) return SALTS_EINVAL;
  if (size > impl->max_command_payload_bytes) return SALTS_EMSGSIZE;
  command = (cnet_command){.kind = CNET_COMMAND_SEND,
                           .connection = connection.session,
                           .size = size,
                           .retained_buffer = buffer,
                           .retained_data = mem_buffer_const_data(buffer)};
  return cnet_shards_publish(impl, connection, &command);
}
'''
if text.count(old) != 1:
    raise SystemExit(f'expected exactly one send_buffer block, found {text.count(old)}')
path.write_text(text.replace(old, new, 1))
