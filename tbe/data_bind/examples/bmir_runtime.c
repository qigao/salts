/**
 * @file bmir_runtime.c
 * @brief Generate BMIR once, then load it as a runtime DataBind codec.
 */

#include "data_bind.h"
#include "turbo_fs.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef DATA_BIND_BMIR_EXAMPLE_SCHEMA
  #define DATA_BIND_BMIR_EXAMPLE_SCHEMA "packet.tbe"
#endif

static int write_bmir_chunk(const void *data, size_t len, void *user) {
  FILE *output = (FILE *)user;
  return output != NULL && fwrite(data, 1, len, output) == len ? 0 : -1;
}

static int generate_bmir(const char *schema_path, const char *bmir_path) {
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;
  FILE *output = fopen(bmir_path, "wb");
  int close_status;

  if (output == NULL) {
    fprintf(stderr, "generate: cannot open %s\n", bmir_path);
    return 1;
  }
  status = data_bind_generate_mir(schema_path, write_bmir_chunk, output, 1, &error);
  close_status = fclose(output);
  if (status != DATA_BIND_OK) {
    remove(bmir_path);
    fprintf(stderr, "generate: %s (%s)\n", error.message, data_bind_status_name(status));
    return 1;
  }
  if (close_status != 0) {
    remove(bmir_path);
    fprintf(stderr, "generate: cannot close %s\n", bmir_path);
    return 1;
  }
  printf("generated: %s\n", bmir_path);
  return 0;
}

static int load_parse_and_serialize(const char *schema_path, const char *bmir_path) {
  static const uint8_t wire[] = {42, 0, 0, 0, 5, 0, 0, 0, 'T', 'u', 'r', 'b', 'o'};
  turbo_fs_buf_t schema = {NULL, 0};
  turbo_fs_buf_t bmir = {NULL, 0};
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status = DATA_BIND_OK;
  DataBind *codec = NULL;
  DataBindObject *object = NULL;
  const DataBindValue *root;
  const char *name = NULL;
  size_t name_len = 0;
  int32_t id = 0;
  char *json = NULL;
  size_t json_len = 0;
  int result = 1;

  if (turbo_fs_read_file(schema_path, &schema) != 0) {
    fprintf(stderr, "load: cannot read %s\n", schema_path);
    goto cleanup;
  }
  if (turbo_fs_read_file(bmir_path, &bmir) != 0) {
    fprintf(stderr, "load: cannot read %s\n", bmir_path);
    goto cleanup;
  }

  status = data_bind_create_from_bmir(schema.base, schema.len, bmir.base, bmir.len, &codec, &error);
  if (status != DATA_BIND_OK) goto data_bind_error;
  status = data_bind_object_from_bin(codec, "Packet", wire, sizeof(wire), &object, &error);
  if (status != DATA_BIND_OK) goto data_bind_error;

  root = data_bind_object_value(object);
  status = data_bind_value_get_int32(data_bind_value_get(root, "id"), &id);
  if (status != DATA_BIND_OK) goto value_error;
  status = data_bind_value_get_string(data_bind_value_get(root, "name"), &name, &name_len);
  if (status != DATA_BIND_OK) goto value_error;
  status = data_bind_object_serialize_json(object, &json, &json_len, &error);
  if (status != DATA_BIND_OK) goto data_bind_error;

  printf("object.id: %d\n", id);
  printf("object.name: %.*s\n", (int)name_len, name);
  printf("json: %.*s\n", (int)json_len, json);
  result = 0;
  goto cleanup;

value_error:
  fprintf(stderr, "access: field kind does not match the schema (%s)\n",
          data_bind_status_name(status));
  goto cleanup;

data_bind_error:
  fprintf(stderr, "runtime: %s (%s)\n", error.message, data_bind_status_name(status));

cleanup:
  data_bind_serialized_free(json);
  data_bind_object_free(object);
  data_bind_free(codec);
  turbo_fs_buf_free(&bmir);
  turbo_fs_buf_free(&schema);
  return result;
}

int main(int argc, char **argv) {
  const char *schema_path = argc > 1 ? argv[1] : DATA_BIND_BMIR_EXAMPLE_SCHEMA;
  const char *bmir_path = argc > 2 ? argv[2] : "packet.bmir";

  if (argc > 3) {
    fprintf(stderr, "usage: %s [schema.tbe] [parser.bmir]\n", argv[0]);
    return 1;
  }
  if (generate_bmir(schema_path, bmir_path) != 0) return 1;
  return load_parse_and_serialize(schema_path, bmir_path);
}
