from pathlib import Path


def replace_once(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, got {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "cnet/src/cnet_command.h",
    "  mem_buffer_t *retained_buffer;\n} cnet_command;",
    "  mem_buffer_t *retained_buffer;\n  const void *retained_data;\n} cnet_command;",
)

replace_once(
    "cnet/src/cnet_command.c",
    "  size_t copied_payload_bytes;\n  mem_buffer_t *payload;\n} cnet_command_entry;",
    "  size_t copied_payload_bytes;\n  mem_buffer_t *payload;\n  const void *payload_data;\n} cnet_command_entry;",
)

replace_once(
    "cnet/src/cnet_command.c",
    "static bool cnet_is_power_of_two(uint64_t value) {\n  return value != 0u && (value & (value - 1u)) == 0u;\n}\n\nstatic bool cnet_command_valid(const cnet_command *command) {",
    "static bool cnet_is_power_of_two(uint64_t value) {\n  return value != 0u && (value & (value - 1u)) == 0u;\n}\n\nstatic bool cnet_command_retained_range_valid(mem_buffer_t *buffer, const void *data, size_t size) {\n  const void *base_ptr;\n  uintptr_t base;\n  uintptr_t start;\n  uintptr_t delta;\n  size_t used;\n  size_t offset;\n\n  if (buffer == NULL || data == NULL || size == 0u) return false;\n  base_ptr = mem_buffer_const_data(buffer);\n  used = mem_buffer_used(buffer);\n  if (base_ptr == NULL || used == 0u) return false;\n  base = (uintptr_t)base_ptr;\n  start = (uintptr_t)data;\n  if (start < base) return false;\n  delta = start - base;\n  if (delta > (uintptr_t)SIZE_MAX) return false;\n  offset = (size_t)delta;\n  if (offset >= used) return false;\n  return size <= used - offset;\n}\n\nstatic bool cnet_command_valid(const cnet_command *command) {",
)

replace_once(
    "cnet/src/cnet_command.c",
    "  if (command == NULL || command->kind <= CNET_COMMAND_NONE || command->kind > CNET_COMMAND_STOP)\n    return false;\n\n  if (command->kind == CNET_COMMAND_STOP)",
    "  if (command == NULL || command->kind <= CNET_COMMAND_NONE || command->kind > CNET_COMMAND_STOP)\n    return false;\n  if (command->retained_buffer == NULL && command->retained_data != NULL) return false;\n\n  if (command->kind == CNET_COMMAND_STOP)",
)

replace_once(
    "cnet/src/cnet_command.c",
    "    if (retained)\n      return command->kind == CNET_COMMAND_SEND && command->data == NULL &&\n             command->segments == NULL && command->segment_count == 0u &&\n             command->size == mem_buffer_used(command->retained_buffer) &&\n             mem_buffer_const_data(command->retained_buffer) != NULL;",
    "    if (retained)\n      return command->kind == CNET_COMMAND_SEND && command->data == NULL &&\n             command->segments == NULL && command->segment_count == 0u &&\n             cnet_command_retained_range_valid(command->retained_buffer, command->retained_data,\n                                               command->size);",
)

replace_once(
    "cnet/src/cnet_command.c",
    "  entry->copied_payload_bytes = copied_bytes;\n  entry->payload = payload;\n  entry->generation = cnet_command_next_generation(entry->generation);",
    "  entry->copied_payload_bytes = copied_bytes;\n  entry->payload = payload;\n  entry->payload_data = retained ? command->retained_data\n                                 : payload != NULL ? mem_buffer_const_data(payload) : NULL;\n  entry->generation = cnet_command_next_generation(entry->generation);",
)

replace_once(
    "cnet/src/cnet_command.c",
    "  out_view->data = entry->size != 0u ? mem_buffer_const_data(entry->payload) : NULL;",
    "  out_view->data = entry->size != 0u ? entry->payload_data : NULL;",
)

replace_once(
    "cnet/src/cnet_command.c",
    "  if ((entry->payload_kind == CNET_COMMAND_PAYLOAD_COPIED &&\n       (entry->payload == NULL || entry->copied_payload_bytes != entry->size)) ||\n      (entry->payload_kind == CNET_COMMAND_PAYLOAD_RETAINED_BUFFER &&\n       (entry->payload == NULL || entry->copied_payload_bytes != 0u)) ||\n      (entry->payload_kind == CNET_COMMAND_PAYLOAD_NONE &&\n       (entry->payload != NULL || entry->size != 0u || entry->copied_payload_bytes != 0u)))\n    return SALTS_EPROTO;",
    "  if ((entry->payload_kind == CNET_COMMAND_PAYLOAD_COPIED &&\n       (entry->payload == NULL || entry->payload_data != mem_buffer_const_data(entry->payload) ||\n        entry->copied_payload_bytes != entry->size)) ||\n      (entry->payload_kind == CNET_COMMAND_PAYLOAD_RETAINED_BUFFER &&\n       (entry->payload == NULL || entry->copied_payload_bytes != 0u ||\n        !cnet_command_retained_range_valid(entry->payload, entry->payload_data, entry->size))) ||\n      (entry->payload_kind == CNET_COMMAND_PAYLOAD_NONE &&\n       (entry->payload != NULL || entry->payload_data != NULL || entry->size != 0u ||\n        entry->copied_payload_bytes != 0u)))\n    return SALTS_EPROTO;",
)

replace_once(
    "cnet/src/cnet_command.c",
    "  mem_buffer_release(entry->payload);\n  entry->payload = NULL;\n  entry->payload_kind = CNET_COMMAND_PAYLOAD_NONE;",
    "  mem_buffer_release(entry->payload);\n  entry->payload = NULL;\n  entry->payload_data = NULL;\n  entry->payload_kind = CNET_COMMAND_PAYLOAD_NONE;",
)

replace_once(
    "cnet/src/cnet_shards.c",
    "#include <salts/thread.h>\n",
    "#include <salts/thread.h>\n#include <salts_buffer.h>\n",
)

replace_once(
    "cnet/src/cnet_shards.c",
    "  const cnet_command command = {.kind = CNET_COMMAND_SEND,\n                                .connection = connection.session,\n                                .size = size,\n                                .retained_buffer = buffer};",
    "  const cnet_command command = {.kind = CNET_COMMAND_SEND,\n                                .connection = connection.session,\n                                .size = size,\n                                .retained_buffer = buffer,\n                                .retained_data = mem_buffer_const_data(buffer)};",
)

for path in ("cnet/tests/cnet_command_test.c", "cnet/tests/cnet_command_profile_test.c"):
    replace_once(
        path,
        "  command.size = mem_buffer_used(buffer);\n  command.retained_buffer = buffer;\n  return command;",
        "  command.size = mem_buffer_used(buffer);\n  command.retained_buffer = buffer;\n  command.retained_data = mem_buffer_const_data(buffer);\n  return command;",
    )
