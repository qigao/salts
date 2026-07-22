/**
 * @file data_bind.c
 * @brief Direct MIR runtime binary codec
 */

#include "data_bind.h"
#include "fmt.h"
#include "mir-gen.h"
#include "mir.h"
#include "node_tree.h"
#include "re.h"
#include "schema_parser_dsl.h"
#include "tbe_error.h"
#include "tbe_wire.h"
#include "turbo_fs.h"
#include "turbo_parser.h"
#include "turbo_str.h"
#include "turbo_thread.h"
#include "turbo_uuid.h"
#include "turbo_vec.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char *tbe_read_varstring(const uint8_t *buf, size_t offset, size_t buf_remaining);

typedef struct {
  const char *name;
  int size;
  MIR_type_t mir_type;
  unsigned char is_float : 1;
  unsigned char is_64 : 1;
} type_meta_t;
static const type_meta_t TYPE_METAS[] = {
    {"uint8_t", 1, MIR_T_U8, 0, 0},   {"uint8", 1, MIR_T_U8, 0, 0},
    {"u8", 1, MIR_T_U8, 0, 0},        {"byte", 1, MIR_T_U8, 0, 0},
    {"int8_t", 1, MIR_T_I8, 0, 0},    {"int8", 1, MIR_T_I8, 0, 0},
    {"i8", 1, MIR_T_I8, 0, 0},
    {"uint16_t", 2, MIR_T_U16, 0, 0}, {"uint16", 2, MIR_T_U16, 0, 0},
    {"u16", 2, MIR_T_U16, 0, 0},      {"int16_t", 2, MIR_T_I16, 0, 0},
    {"int16", 2, MIR_T_I16, 0, 0},    {"i16", 2, MIR_T_I16, 0, 0},
    {"uint32_t", 4, MIR_T_U32, 0, 0},
    {"uint32", 4, MIR_T_U32, 0, 0},   {"u32", 4, MIR_T_U32, 0, 0},
    {"int32_t", 4, MIR_T_I32, 0, 0},  {"int32", 4, MIR_T_I32, 0, 0},
    {"i32", 4, MIR_T_I32, 0, 0},      {"uint64_t", 8, MIR_T_U64, 0, 1},
    {"uint64", 8, MIR_T_U64, 0, 1},   {"u64", 8, MIR_T_U64, 0, 1},
    {"int64_t", 8, MIR_T_I64, 0, 1},  {"int64", 8, MIR_T_I64, 0, 1},
    {"i64", 8, MIR_T_I64, 0, 1},
    {"float", 4, MIR_T_F, 1, 0},      {"f32", 4, MIR_T_F, 1, 0},
    {"double", 8, MIR_T_D, 1, 0},     {"f64", 8, MIR_T_D, 1, 0},
    {"bool", 1, MIR_T_U8, 0, 0},      {NULL, 0, MIR_T_UNDEF, 0, 0}};

typedef struct owned_alloc_node {
  void *ptr;
  struct owned_alloc_node *next;
} owned_alloc_node_t;
typedef struct mir_func_node {
  char *type_name;
  void *parse_fn;
  void *parse_record_fn;
  struct mir_func_node *next;
} mir_func_node_t;

typedef struct data_bind_record_layout data_bind_record_layout_t;
typedef struct data_bind_record_layout_field {
  char *name;
  data_bind_record_layout_t *child;
} data_bind_record_layout_field_t;
struct data_bind_record_layout {
  data_bind_record_layout_field_t *fields;
  size_t count;
};

typedef struct data_bind_record_plan {
  char *type_name;
  data_bind_record_layout_t *layout;
  struct data_bind_record_plan *next;
} data_bind_record_plan_t;

typedef struct mir_cache_entry {
  char schema_hash[65];
  MIR_context_t shared_ctx;
  mir_func_node_t *func_head;
  int ref_count;
  int evicted;
  struct mir_cache_entry *next;
} mir_cache_entry_t;

/* Small object pool for frequently allocated DataBindValue nodes */
#define VALUE_POOL_SIZE 64
#define DATA_BIND_FILE_STREAM_CHUNK_SIZE 65536
#define DATA_BIND_MIR_PARSER_ABI_VERSION UINT32_C(1)
#define DATA_BIND_MIR_MODULE_NAME "data_bind_binary"
#define DATA_BIND_MIR_ABI_ITEM_NAME "__data_bind_mir_abi"
#define DATA_BIND_MIR_SCHEMA_ITEM_NAME "__data_bind_schema_fingerprint"

/*
 * Fixed atomic slots avoid the ABA reclamation problem of a shared lock-free
 * linked list. A ready bitmap avoids scanning empty/full pools; operations are
 * otherwise bounded by VALUE_POOL_SIZE.
 */
#define VALUE_POOL_FULL_MASK UINT64_MAX
_Static_assert(VALUE_POOL_SIZE == sizeof(uint64_t) * CHAR_BIT,
               "value pool bitmap must cover every slot");

static _Atomic(DataBindValue *) g_value_pool_slots[VALUE_POOL_SIZE];
static uintptr_t g_value_pool_closed_slot_storage;
#define VALUE_POOL_CLOSED_SLOT ((DataBindValue *)(void *)&g_value_pool_closed_slot_storage)

enum value_pool_state { VALUE_POOL_DISABLED = 0, VALUE_POOL_ENABLED, VALUE_POOL_SUSPENDED };

static turbo_mutex_t g_value_pool_control_mutex;
static atomic_int g_value_pool_state = VALUE_POOL_ENABLED;
static _Atomic uint64_t g_value_pool_ready_mask;
static atomic_size_t g_value_pool_allocated_count;
static atomic_size_t g_value_pool_reused_count;
static turbo_once_t g_value_pool_once = TURBO_ONCE_INIT;
static TURBO_THREAD_LOCAL size_t g_value_pool_take_cursor;
static TURBO_THREAD_LOCAL size_t g_value_pool_put_cursor;
static TURBO_THREAD_LOCAL int g_dynamic_runtime_oom;

typedef struct data_bind_bmir_input {
  const uint8_t *data;
  size_t len;
  size_t offset;
} data_bind_bmir_input_t;

static TURBO_THREAD_LOCAL data_bind_bmir_input_t *g_data_bind_bmir_input;

static void value_pool_init_once(void) { turbo_mutex_init(&g_value_pool_control_mutex); }

static int value_pool_is_enabled(void) {
  return atomic_load_explicit(&g_value_pool_state, memory_order_acquire) == VALUE_POOL_ENABLED;
}

static DataBindValue *value_pool_take(void) {
  uint64_t ready = atomic_load_explicit(&g_value_pool_ready_mask, memory_order_acquire);
  size_t offset;
  size_t start = g_value_pool_take_cursor;
  while (ready != 0) {
    uint64_t desired;
    uint64_t bit;
    size_t slot = 0;
    DataBindValue *value;

    for (offset = 0; offset < VALUE_POOL_SIZE; ++offset) {
      slot = (start + offset) % VALUE_POOL_SIZE;
      bit = UINT64_C(1) << slot;
      if ((ready & bit) != 0) break;
    }
    if (offset == VALUE_POOL_SIZE) return NULL;

    desired = ready & ~bit;
    if (!atomic_compare_exchange_strong_explicit(&g_value_pool_ready_mask, &ready, desired,
                                                 memory_order_acq_rel, memory_order_acquire)) {
      int expected = VALUE_POOL_ENABLED;
      atomic_compare_exchange_strong_explicit(&g_value_pool_state, &expected, VALUE_POOL_SUSPENDED,
                                              memory_order_release, memory_order_relaxed);
      return NULL;
    }

    value = atomic_load_explicit(&g_value_pool_slots[slot], memory_order_acquire);
    if (value != NULL && value != VALUE_POOL_CLOSED_SLOT &&
        atomic_compare_exchange_strong_explicit(&g_value_pool_slots[slot], &value, NULL,
                                                memory_order_acquire, memory_order_relaxed)) {
      g_value_pool_take_cursor = (slot + 1U) % VALUE_POOL_SIZE;
      return value;
    }
    ready = atomic_load_explicit(&g_value_pool_ready_mask, memory_order_acquire);
  }
  return NULL;
}

static int value_pool_put(DataBindValue *value) {
  uint64_t ready = atomic_load_explicit(&g_value_pool_ready_mask, memory_order_relaxed);
  size_t offset;
  size_t start = g_value_pool_put_cursor;

  if (ready == VALUE_POOL_FULL_MASK) return 0;
  for (offset = 0; offset < VALUE_POOL_SIZE; ++offset) {
    size_t slot = (start + offset) % VALUE_POOL_SIZE;
    DataBindValue *expected = NULL;
    if (atomic_compare_exchange_strong_explicit(&g_value_pool_slots[slot], &expected, value,
                                                memory_order_release, memory_order_relaxed)) {
      g_value_pool_put_cursor = (slot + 1U) % VALUE_POOL_SIZE;
      atomic_fetch_or_explicit(&g_value_pool_ready_mask, UINT64_C(1) << slot, memory_order_release);
      return 1;
    }
  }
  return 0;
}

static mir_cache_entry_t *g_mir_cache_head = NULL;
static int g_mir_cache_enabled = 1;

typedef struct data_bind_runtime_api {
  DataBindValue *(*create_object)(void);
  void (*free_value)(DataBindValue *value);
  int (*set_field_int)(DataBindValue *obj, const char *name, int32_t val);
  int (*set_field_uint32)(DataBindValue *obj, const char *name, uint32_t val);
  int (*set_field_int64)(DataBindValue *obj, const char *name, int64_t val);
  int (*set_field_uint64)(DataBindValue *obj, const char *name, uint64_t val);
  int (*set_field_double)(DataBindValue *obj, const char *name, double val);
  int (*set_field_bool)(DataBindValue *obj, const char *name, int val);
  int (*set_field_string)(DataBindValue *obj, const char *name, const char *val);
  int (*set_field_bytes)(DataBindValue *obj, const char *name, const uint8_t *data, size_t len);
  int (*set_field_uuid)(DataBindValue *obj, const char *name, const uint8_t *data);
  DataBindValue *(*create_list)(void);
  int (*add_list_item_int)(DataBindValue *list, int32_t val);
  int (*add_list_item_uint32)(DataBindValue *list, uint32_t val);
  int (*add_list_item_int64)(DataBindValue *list, int64_t val);
  int (*add_list_item_uint64)(DataBindValue *list, uint64_t val);
  int (*add_list_item_double)(DataBindValue *list, double val);
  int (*add_list_item_bool)(DataBindValue *list, int val);
  int (*add_list_item_string)(DataBindValue *list, const char *val);
  int (*add_list_item_object)(DataBindValue *list, DataBindValue *obj);
  int (*set_field_list)(DataBindValue *obj, const char *name, DataBindValue *list);
  int (*set_field_object)(DataBindValue *obj, const char *name, DataBindValue *child);
  DataBindValue *(*create_set)(void);
  int (*add_set_item_int)(DataBindValue *set, int32_t val);
  int (*add_set_item_uint32)(DataBindValue *set, uint32_t val);
  int (*add_set_item_int64)(DataBindValue *set, int64_t val);
  int (*add_set_item_uint64)(DataBindValue *set, uint64_t val);
  int (*add_set_item_double)(DataBindValue *set, double val);
  int (*add_set_item_bool)(DataBindValue *set, int val);
  int (*add_set_item_string)(DataBindValue *set, const char *val);
  int (*set_field_set)(DataBindValue *obj, const char *name, DataBindValue *set);
  DataBindValue *(*create_map)(void);
  int (*add_map_entry_string_string)(DataBindValue *map, const char *key, const char *val);
  int (*add_map_entry_string_int)(DataBindValue *map, const char *key, int32_t val);
  int (*add_map_entry_string_uint32)(DataBindValue *map, const char *key, uint32_t val);
  int (*add_map_entry_string_int64)(DataBindValue *map, const char *key, int64_t val);
  int (*add_map_entry_string_uint64)(DataBindValue *map, const char *key, uint64_t val);
  int (*add_map_entry_string_double)(DataBindValue *map, const char *key, double val);
  int (*add_map_entry_string_bool)(DataBindValue *map, const char *key, int val);
  int (*set_field_map)(DataBindValue *obj, const char *name, DataBindValue *map);
} data_bind_runtime_api_t;

struct DataBind {
  MIR_context_t ctx;
  Node *schema_root;
  mir_func_node_t *func_head;
  owned_alloc_node_t *owned_allocs;
  char error[256];
  char binary_error[256];
  data_bind_runtime_api_t api;
  char schema_hash[65]; /* SHA-256 hash of schema for caching */
  int is_cloned;        /* Whether this codec shares MIR context with another */
  int mir_gen_initialized;
  data_bind_record_plan_t *record_plans;
};

typedef struct data_bind_json_stream_frame {
  json_value_t *value;
  char *pending_key;
  int is_object;
} data_bind_json_stream_frame_t;

struct data_bind_stream_t {
  DataBind *codec;
  char *type_name;
  char *path_or_expr;
  DataBindValue **out_value;
  DataBindError *error;
  DataBindRecordFn record_callback;
  void *record_callback_user;
  uint64_t record_callback_index;
  DataBindStatus (*feed_fn)(data_bind_stream_t *parser, const char *data, size_t len,
                            DataBindError *error);
  DataBindStatus (*finish_fn)(data_bind_stream_t *parser, DataBindValue **out_value,
                              DataBindError *error);
  DataBindStatus (*bind_fn)(DataBind *codec, const char *type_name, const char *text, size_t len,
                            const char *path, DataBindValue **out_value, DataBindError *error);
  char *buffer;
  size_t size;
  size_t capacity;
  char *csv_header;
  size_t csv_header_len;
  char *csv_record;
  size_t csv_record_len;
  size_t csv_record_capacity;
  char *csv_field;
  size_t csv_field_len;
  size_t csv_field_capacity;
  tstr_v *csv_fields;
  char **csv_field_storage;
  size_t csv_field_count;
  size_t csv_fields_capacity;
  turbo_csv_doc_t *csv_filter_doc;
  turbo_dsv_filter_t *csv_filter;
  DataBindValue *csv_values;
  DataBindValue *stream_values;
  turbo_json_sax_parser_t *json_sax;
  turbo_yaml_sax_parser_t *yaml_sax;
  turbo_xml_sax_parser_t *xml_sax;
  data_bind_json_stream_frame_t *json_frames;
  size_t json_frame_count;
  size_t json_frame_capacity;
  size_t json_sax_depth;
  char *xml_stream_target;
  tstr_t xml_capture;
  size_t xml_capture_depth;
  size_t csv_data_row;
  int csv_header_seen;
  int csv_in_quotes;
  int csv_quote_pending;
  int csv_skip_next_lf;
  int csv_failed;
  int sax_failed;
  int is_csv;
  int json_stream_candidate;
  int json_stream_active;
  int json_stream_done;
  int json_root_seen;
  int xml_stream_candidate;
  int xml_capture_active;
  int xml_open_start;
  char stream_error[256];
  int finished;
  int started;
  int record_callback_stopped;
  int record_callback_failed;
};

typedef struct data_bind_value_field {
  char *name;
  DataBindValue *value;
} data_bind_value_field_t;

typedef struct data_bind_value_array {
  DataBindValue **items;
  size_t count;
  size_t capacity;
} data_bind_value_array_t;

typedef struct data_bind_value_field_array {
  data_bind_value_field_t *items;
  size_t count;
  size_t capacity;
} data_bind_value_field_array_t;

typedef struct data_bind_value_map_entry {
  char *key;
  DataBindValue *value;
} data_bind_value_map_entry_t;

typedef struct data_bind_value_map_array {
  data_bind_value_map_entry_t *items;
  size_t count;
  size_t capacity;
} data_bind_value_map_array_t;

struct DataBindValue {
  DataBindValueKind kind;
  const data_bind_record_layout_t *record_layout;
  union {
    int32_t int_val;
    int64_t int64_val;
    uint64_t uint64_val;
    double double_val;
    int bool_val;
    struct {
      char *ptr;
    } string_val;
    struct {
      uint8_t *ptr;
      size_t len;
    } bytes_val;
    turbo_uuid_t uuid_val;
    turbo_datetime_t datetime_val;
    DataBindDate date_val;
    DataBindTime time_val;
    int64_t duration_ms;
    DataBindDecimal decimal_val;
    struct {
      char *ptr;
    } bigint_val;
    DataBindMoney money_val;
    data_bind_value_field_array_t object_val;
    data_bind_value_array_t array_val;
    data_bind_value_map_array_t map_val;
  } data;
};

struct DataBindObject {
  char *type_name;
  DataBindValue *value;
};

typedef enum {
  EF_INT,
  EF_U32,
  EF_I64,
  EF_U64,
  EF_DBL,
  EF_BOOL,
  EF_UUID,
  EF_STR,
  EF_FIX_BYTES,
  EF_VAR_BYTES,
  EF_LIST_INT,
  EF_LIST_U32,
  EF_LIST_I64,
  EF_LIST_U64,
  EF_LIST_DBL,
  EF_LIST_BOOL,
  EF_LIST_STR,
  EF_LIST_OBJ,
  EF_SET_INT,
  EF_SET_U32,
  EF_SET_I64,
  EF_SET_U64,
  EF_SET_DBL,
  EF_SET_BOOL,
  EF_SET_STR,
  EF_MAP_STR_STR,
  EF_MAP_STR_INT,
  EF_MAP_STR_U32,
  EF_MAP_STR_I64,
  EF_MAP_STR_U64,
  EF_MAP_STR_DBL,
  EF_MAP_STR_BOOL,
  EF_GROUP,
  EF_OBJECT
} emit_kind_t;

typedef struct emit_field emit_field_t;
typedef struct {
  emit_field_t *items;
  size_t count;
  size_t capacity;
} emit_field_array_t;
struct emit_field {
  char *name;
  emit_kind_t kind;
  int size;
  MIR_type_t mir_type;
  unsigned char is_float : 1;
  unsigned char is_64 : 1;
  unsigned char has_set_bytes : 1;
  size_t fixed_count;
  int group_dim;
  emit_field_array_t children;
};

typedef struct {
  MIR_item_t import_item;
  MIR_item_t proto_item;
} external_ref_t;
typedef struct {
  external_ref_t create_obj, free_value, set_int, set_u32, set_i64, set_u64, set_dbl, set_bool,
      set_str, set_bytes;
  external_ref_t set_uuid;
  external_ref_t create_list, add_list_int, add_list_u32, add_list_i64, add_list_u64, add_list_dbl,
      add_list_bool, add_list_str, add_list_obj, set_list;
  external_ref_t create_set, add_set_int, add_set_u32, add_set_i64, add_set_u64, add_set_dbl,
      add_set_bool, add_set_str, set_set;
  external_ref_t create_map, add_map_str_str, add_map_str_int, add_map_str_u32, add_map_str_i64,
      add_map_str_u64, add_map_str_dbl, add_map_str_bool, set_map;
  external_ref_t read_varstr, free_fn;
  external_ref_t create_record_child_v1, create_record_field_v1, set_slot_int_v1, set_slot_u32_v1,
      set_slot_i64_v1, set_slot_u64_v1, set_slot_dbl_v1, set_slot_bool_v1, set_slot_str_v1,
      set_slot_bytes_v1, set_slot_uuid_v1, set_slot_list_v1, set_slot_set_v1, set_slot_map_v1;
} external_items_t;

typedef struct {
  DataBind *codec;
  MIR_context_t ctx;
  MIR_module_t module;
  external_items_t ext;
  size_t temp_name_id;
  size_t data_name_id;
  int include_record_v1;
} mir_builder_t;
typedef struct {
  mir_builder_t *builder;
  MIR_item_t func_item;
  MIR_func_t func;
  MIR_reg_t buf_reg;
  MIR_reg_t len_reg;
  MIR_reg_t off_reg;
  MIR_reg_t obj_reg;
  MIR_label_t fail_label;
  int slot_mode;
} mir_emitter_t;

static const type_meta_t *find_type_meta(const char *type) {
  const type_meta_t *m;
  if (type == NULL) return NULL;
  for (m = TYPE_METAS; m->name != NULL; m++)
    if (strcmp(type, m->name) == 0) return m;
  return NULL;
}

static int set_i64_noop(DataBindValue *o, const char *n, int64_t v) {
  (void)o;
  (void)n;
  (void)v;
  return 1;
}
static int set_u64_noop(DataBindValue *o, const char *n, uint64_t v) {
  (void)o;
  (void)n;
  (void)v;
  return 1;
}
static int set_bytes_noop(DataBindValue *o, const char *n, const uint8_t *d, size_t l) {
  (void)o;
  (void)n;
  (void)d;
  (void)l;
  return 1;
}
static int set_uuid_noop(DataBindValue *o, const char *n, const uint8_t *d) {
  (void)o;
  (void)n;
  (void)d;
  return 1;
}
static int set_i32_noop(DataBindValue *o, const char *n, int32_t v) {
  (void)o;
  (void)n;
  (void)v;
  return 1;
}
static int set_u32_noop(DataBindValue *o, const char *n, uint32_t v) {
  (void)o;
  (void)n;
  (void)v;
  return 1;
}
static int set_dbl_noop(DataBindValue *o, const char *n, double v) {
  (void)o;
  (void)n;
  (void)v;
  return 1;
}
static int set_bool_noop(DataBindValue *o, const char *n, int v) {
  (void)o;
  (void)n;
  (void)v;
  return 1;
}
static int set_str_noop(DataBindValue *o, const char *n, const char *v) {
  (void)o;
  (void)n;
  (void)v;
  return 1;
}

static char *dbv_strdup(const char *src) {
  size_t len;
  char *dst;
  if (src == NULL) return NULL;
  len = strlen(src) + 1;
  dst = (char *)malloc(len);
  if (dst == NULL) g_dynamic_runtime_oom = 1;
  if (dst == NULL) return NULL;
  memcpy(dst, src, len);
  return dst;
}

static DataBindValue *dbv_new(DataBindValueKind kind) {
  DataBindValue *value = NULL;
  int pool_enabled = value_pool_is_enabled();

  if (pool_enabled && atomic_load_explicit(&g_value_pool_ready_mask, memory_order_relaxed) != 0) {
    value = value_pool_take();
    if (value != NULL)
      atomic_fetch_add_explicit(&g_value_pool_reused_count, 1U, memory_order_relaxed);
  }

  if (value != NULL) {
    memset(value, 0, sizeof(*value));
  } else {
    value = (DataBindValue *)calloc(1, sizeof(*value));
    if (pool_enabled && value != NULL) {
      atomic_fetch_add_explicit(&g_value_pool_allocated_count, 1U, memory_order_relaxed);
    }
  }

  if (value == NULL) g_dynamic_runtime_oom = 1;
  if (value != NULL) value->kind = kind;
  return value;
}

static int dbv_reserve_capacity(size_t current_capacity, size_t min_capacity, size_t item_size,
                                size_t *new_capacity) {
  size_t capacity;
  if (new_capacity == NULL || item_size == 0) return 0;
  if (min_capacity <= current_capacity) {
    *new_capacity = current_capacity;
    return 1;
  }
  if (min_capacity > SIZE_MAX / item_size) return 0;

  capacity = current_capacity;
  if (capacity == 0) capacity = min_capacity > 8 ? min_capacity : 8;
  while (capacity < min_capacity) {
    if (capacity > SIZE_MAX / 2) {
      capacity = min_capacity;
      break;
    }
    capacity *= 2;
  }
  if (capacity > SIZE_MAX / item_size) return 0;
  *new_capacity = capacity;
  return 1;
}

static int dbv_array_reserve(data_bind_value_array_t *array, size_t min_capacity) {
  DataBindValue **items;
  size_t capacity;
  if (array == NULL || min_capacity <= array->capacity) return array != NULL;
  if (!dbv_reserve_capacity(array->capacity, min_capacity, sizeof(*array->items), &capacity))
    return 0;
  items = (DataBindValue **)realloc(array->items, capacity * sizeof(*items));
  if (items == NULL) {
    g_dynamic_runtime_oom = 1;
    return 0;
  }
  array->items = items;
  array->capacity = capacity;
  return 1;
}

static int dbv_object_reserve(DataBindValue *obj, size_t min_capacity) {
  data_bind_value_field_array_t *fields;
  data_bind_value_field_t *items;
  size_t capacity;
  if (obj == NULL || obj->kind != DATA_BIND_VALUE_OBJECT) return 0;
  fields = &obj->data.object_val;
  if (min_capacity <= fields->capacity) return 1;
  if (!dbv_reserve_capacity(fields->capacity, min_capacity, sizeof(*fields->items), &capacity))
    return 0;
  items = (data_bind_value_field_t *)realloc(fields->items, capacity * sizeof(*items));
  if (items == NULL) {
    g_dynamic_runtime_oom = 1;
    return 0;
  }
  fields->items = items;
  fields->capacity = capacity;
  return 1;
}

static int dbv_map_reserve(DataBindValue *map, size_t min_capacity) {
  data_bind_value_map_array_t *entries;
  data_bind_value_map_entry_t *items;
  size_t capacity;
  if (map == NULL || map->kind != DATA_BIND_VALUE_MAP) return 0;
  entries = &map->data.map_val;
  if (min_capacity <= entries->capacity) return 1;
  if (!dbv_reserve_capacity(entries->capacity, min_capacity, sizeof(*entries->items), &capacity))
    return 0;
  items = (data_bind_value_map_entry_t *)realloc(entries->items, capacity * sizeof(*items));
  if (items == NULL) {
    g_dynamic_runtime_oom = 1;
    return 0;
  }
  entries->items = items;
  entries->capacity = capacity;
  return 1;
}

static int dbv_array_push(data_bind_value_array_t *array, DataBindValue *value) {
  if (array == NULL || value == NULL) return 0;
  if (array->count == SIZE_MAX || !dbv_array_reserve(array, array->count + 1)) return 0;
  array->items[array->count++] = value;
  return 1;
}

static int dbv_object_set(DataBindValue *obj, const char *name, DataBindValue *value) {
  data_bind_value_field_array_t *fields;
  if (obj == NULL || obj->kind != DATA_BIND_VALUE_OBJECT || name == NULL || value == NULL) return 0;
  fields = &obj->data.object_val;
  if (fields->count == SIZE_MAX || !dbv_object_reserve(obj, fields->count + 1)) return 0;
  fields->items[fields->count].name = dbv_strdup(name);
  if (fields->items[fields->count].name == NULL) return 0;
  fields->items[fields->count].value = value;
  fields->count++;
  return 1;
}

static int dbv_map_set(DataBindValue *map, const char *key, DataBindValue *value) {
  data_bind_value_map_array_t *entries;
  if (map == NULL || map->kind != DATA_BIND_VALUE_MAP || key == NULL || value == NULL) return 0;
  entries = &map->data.map_val;
  if (entries->count == SIZE_MAX || !dbv_map_reserve(map, entries->count + 1)) return 0;
  entries->items[entries->count].key = dbv_strdup(key);
  if (entries->items[entries->count].key == NULL) return 0;
  entries->items[entries->count].value = value;
  entries->count++;
  return 1;
}

static int dbv_map_has_key(const DataBindValue *map, const char *key) {
  size_t i;
  if (map == NULL || map->kind != DATA_BIND_VALUE_MAP || key == NULL) return 0;
  for (i = 0; i < map->data.map_val.count; i++) {
    if (map->data.map_val.items[i].key != NULL && strcmp(map->data.map_val.items[i].key, key) == 0)
      return 1;
  }
  return 0;
}

void data_bind_value_free(DataBindValue *value) {
  size_t i;
  if (value == NULL) return;
  switch (value->kind) {
  case DATA_BIND_VALUE_OBJECT:
    for (i = 0; i < value->data.object_val.count; i++) {
      free(value->data.object_val.items[i].name);
      data_bind_value_free(value->data.object_val.items[i].value);
    }
    free(value->data.object_val.items);
    break;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET:
    for (i = 0; i < value->data.array_val.count; i++)
      data_bind_value_free(value->data.array_val.items[i]);
    free(value->data.array_val.items);
    break;
  case DATA_BIND_VALUE_MAP:
    for (i = 0; i < value->data.map_val.count; i++) {
      free(value->data.map_val.items[i].key);
      data_bind_value_free(value->data.map_val.items[i].value);
    }
    free(value->data.map_val.items);
    break;
  case DATA_BIND_VALUE_STRING:
    free(value->data.string_val.ptr);
    break;
  case DATA_BIND_VALUE_BIGINT:
    free(value->data.bigint_val.ptr);
    break;
  case DATA_BIND_VALUE_BYTES:
    free(value->data.bytes_val.ptr);
    break;
  default:
    break;
  }

  if (value_pool_is_enabled() && value_pool_put(value)) return;
  free(value);
}

static DataBindValue *dbv_int(int32_t value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_INT);
  if (v != NULL) v->data.int_val = value;
  return v;
}

static DataBindValue *dbv_int64(int64_t value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_INT64);
  if (v != NULL) v->data.int64_val = value;
  return v;
}

static DataBindValue *dbv_uint32_compat(uint32_t value) {
  return value <= INT32_MAX ? dbv_int((int32_t)value) : dbv_int64((int64_t)value);
}

static DataBindValue *dbv_uint64(uint64_t value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_UINT64);
  if (v != NULL) v->data.uint64_val = value;
  return v;
}

static DataBindValue *dbv_double(double value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_DOUBLE);
  if (v != NULL) v->data.double_val = value;
  return v;
}

static DataBindValue *dbv_bool(int value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_BOOL);
  if (v != NULL) v->data.bool_val = value != 0;
  return v;
}

static DataBindValue *dbv_string(const char *value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_STRING);
  if (v == NULL) return NULL;
  v->data.string_val.ptr = dbv_strdup(value != NULL ? value : "");
  if (v->data.string_val.ptr == NULL) {
    data_bind_value_free(v);
    return NULL;
  }
  return v;
}

static DataBindValue *dbv_bytes(const uint8_t *data, size_t len) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_BYTES);
  if (v == NULL) return NULL;
  if (len > 0) {
    v->data.bytes_val.ptr = (uint8_t *)malloc(len);
    if (v->data.bytes_val.ptr == NULL) {
      g_dynamic_runtime_oom = 1;
      data_bind_value_free(v);
      return NULL;
    }
    if (data != NULL) memcpy(v->data.bytes_val.ptr, data, len);
  }
  v->data.bytes_val.len = len;
  return v;
}

static char *data_bind_read_varstring(const uint8_t *buf, size_t offset, size_t buf_remaining) {
  char *value = tbe_read_varstring(buf, offset, buf_remaining);
  if (value == NULL) g_dynamic_runtime_oom = 1;
  return value;
}

static DataBindValue *dbv_uuid_bytes(const uint8_t *data) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_UUID);
  if (v == NULL || data == NULL) {
    data_bind_value_free(v);
    return NULL;
  }
  memcpy(v->data.uuid_val.bytes, data, sizeof(v->data.uuid_val.bytes));
  return v;
}

static DataBindValue *dbv_uuid_text(const char *text) {
  turbo_uuid_t uuid;
  if (text == NULL || turbo_uuid_parse(text, &uuid) != TURBO_OK) return NULL;
  return dbv_uuid_bytes(uuid.bytes);
}

static DataBindValue *dbv_datetime(turbo_datetime_t value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_DATETIME);
  if (v == NULL) return NULL;
  v->data.datetime_val = value;
  return v;
}

static DataBindValue *dbv_datetime_text(const char *text) {
  turbo_datetime_t dt;
  if (text == NULL || turbo_parse_datetime(text, strlen(text), &dt) != 0) return NULL;
  return dbv_datetime(dt);
}

static int db_date_valid(int year, int month, int day) {
  static const int days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days;
  if (year < 1 || month < 1 || month > 12 || day < 1) return 0;
  days = days_per_month[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) days = 29;
  return day <= days;
}

static int db_parse_date_text(const char *text, DataBindDate *out) {
  turbo_datetime_t dt;
  int year = 0, month = 0, day = 0, consumed = 0;
  if (text == NULL || out == NULL) return 0;
  if (sscanf(text, "%d-%d-%d%n", &year, &month, &day, &consumed) == 3 ||
      sscanf(text, "%d/%d/%d%n", &year, &month, &day, &consumed) == 3) {
    if (text[consumed] != '\0' || !db_date_valid(year, month, day)) return 0;
    out->year = year;
    out->month = month;
    out->day = day;
    return 1;
  }
  if (turbo_parse_datetime(text, strlen(text), &dt) == 0) {
    out->year = dt.year;
    out->month = dt.month;
    out->day = dt.day;
    return 1;
  }
  return 0;
}

static int db_parse_time_text(const char *text, DataBindTime *out) {
  turbo_datetime_t dt;
  if (text == NULL || out == NULL) return 0;
  if (strchr(text, ':') == NULL || turbo_parse_datetime(text, strlen(text), &dt) != 0) return 0;
  if (dt.year != 0 || dt.month != 0 || dt.day != 0 || dt.has_tz || dt.hour < 0 || dt.hour > 23 ||
      dt.minute < 0 || dt.minute > 59 || dt.second < 0 || dt.second > 60 ||
      dt.millisecond < 0 || dt.millisecond > 999)
    return 0;
  out->hour = dt.hour;
  out->minute = dt.minute;
  out->second = dt.second;
  out->millisecond = dt.millisecond;
  return 1;
}

static int db_parse_duration_text(const char *text, int64_t *out) {
  const char *p;
  int sign = 1;
  double total = 0.0;
  int saw_value = 0;
  long long hours = 0, minutes = 0, seconds = 0, millis = 0;
  int consumed = 0;
  int colon_sign;
  if (text == NULL || out == NULL) return 0;
  if (sscanf(text, "%lld:%lld:%lld.%lld%n", &hours, &minutes, &seconds, &millis, &consumed) == 4 &&
      text[consumed] == '\0' && minutes >= 0 && minutes <= 59 && seconds >= 0 && seconds <= 60 &&
      millis >= 0 && millis <= 999) {
    colon_sign = hours < 0 ? -1 : 1;
    if (hours < 0) hours = -hours;
    *out =
        (int64_t)(colon_sign * (hours * 3600000LL + minutes * 60000LL + seconds * 1000LL + millis));
    return 1;
  }
  if (sscanf(text, "%lld:%lld:%lld%n", &hours, &minutes, &seconds, &consumed) == 3 &&
      text[consumed] == '\0' && minutes >= 0 && minutes <= 59 && seconds >= 0 && seconds <= 60) {
    colon_sign = hours < 0 ? -1 : 1;
    if (hours < 0) hours = -hours;
    *out = (int64_t)(colon_sign * (hours * 3600000LL + minutes * 60000LL + seconds * 1000LL));
    return 1;
  }
  p = text;
  while (isspace((unsigned char)*p))
    p++;
  if (*p == '-') {
    sign = -1;
    p++;
  } else if (*p == '+') {
    p++;
  }
  while (*p != '\0') {
    char *next = NULL;
    double n;
    while (isspace((unsigned char)*p))
      p++;
    if (*p == '\0') break;
    errno = 0;
    n = strtod(p, &next);
    if (errno != 0 || next == p) return 0;
    p = next;
    while (isspace((unsigned char)*p))
      p++;
    if (*p == '\0') {
      total += n;
      saw_value = 1;
      break;
    }
    if (p[0] == 'm' && p[1] == 's') {
      total += n;
      p += 2;
    } else if (*p == 's') {
      total += n * 1000.0;
      p++;
    } else if (*p == 'm') {
      total += n * 60000.0;
      p++;
    } else if (*p == 'h') {
      total += n * 3600000.0;
      p++;
    } else if (*p == 'd') {
      total += n * 86400000.0;
      p++;
    } else {
      return 0;
    }
    saw_value = 1;
  }
  if (!saw_value || total > (double)INT64_MAX) return 0;
  *out = (int64_t)(sign * total);
  return 1;
}

static int db_date_to_text(DataBindDate date, char *out, size_t len) {
  return out != NULL && len > 0 && db_date_valid(date.year, date.month, date.day) &&
         snprintf(out, len, "%04d-%02d-%02d", date.year, date.month, date.day) > 0;
}

static int db_time_to_text(DataBindTime time, char *out, size_t len) {
  if (out == NULL || len == 0 || time.hour < 0 || time.hour > 23 || time.minute < 0 ||
      time.minute > 59 || time.second < 0 || time.second > 60 || time.millisecond < 0 ||
      time.millisecond > 999)
    return 0;
  if (time.millisecond > 0)
    return snprintf(out, len, "%02d:%02d:%02d.%03d", time.hour, time.minute, time.second,
                    time.millisecond) > 0;
  return snprintf(out, len, "%02d:%02d:%02d", time.hour, time.minute, time.second) > 0;
}

static int db_duration_to_text(int64_t ms, char *out, size_t len) {
  int64_t rem;
  int64_t hours;
  int64_t minutes;
  int64_t seconds;
  if (out == NULL || len == 0) return 0;
  rem = ms < 0 ? -ms : ms;
  hours = rem / 3600000;
  rem %= 3600000;
  minutes = rem / 60000;
  rem %= 60000;
  seconds = rem / 1000;
  rem %= 1000;
  return snprintf(out, len, "%s%lld:%02lld:%02lld.%03lld", ms < 0 ? "-" : "", (long long)hours,
                  (long long)minutes, (long long)seconds, (long long)rem) > 0;
}

static DataBindValue *dbv_date(DataBindDate value) {
  DataBindValue *v;
  if (!db_date_valid(value.year, value.month, value.day)) return NULL;
  v = dbv_new(DATA_BIND_VALUE_DATE);
  if (v != NULL) v->data.date_val = value;
  return v;
}

static DataBindValue *dbv_date_text(const char *text) {
  DataBindDate date;
  if (!db_parse_date_text(text, &date)) return NULL;
  return dbv_date(date);
}

static DataBindValue *dbv_time(DataBindTime value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_TIME);
  if (v != NULL) v->data.time_val = value;
  return v;
}

static DataBindValue *dbv_time_text(const char *text) {
  DataBindTime time;
  if (!db_parse_time_text(text, &time)) return NULL;
  return dbv_time(time);
}

static DataBindValue *dbv_duration(int64_t value) {
  DataBindValue *v = dbv_new(DATA_BIND_VALUE_DURATION);
  if (v != NULL) v->data.duration_ms = value;
  return v;
}

static DataBindValue *dbv_duration_text(const char *text) {
  int64_t ms = 0;
  if (!db_parse_duration_text(text, &ms)) return NULL;
  return dbv_duration(ms);
}

static int db_decimal_normalize(DataBindDecimal *value) {
  if (value == NULL) return 0;
  if (value->scale < 0) return 0;
  while (value->scale > 0 && value->mantissa % 10 == 0) {
    value->mantissa /= 10;
    value->scale--;
  }
  if (value->mantissa == 0) value->scale = 0;
  return 1;
}

static int db_parse_decimal_text(const char *text, DataBindDecimal *out) {
  const char *p;
  int sign = 1;
  int saw_digit = 0;
  int saw_dot = 0;
  int32_t scale = 0;
  uint64_t acc = 0;
  uint64_t limit;

  if (text == NULL || out == NULL) return 0;
  p = text;
  while (isspace((unsigned char)*p))
    p++;
  if (*p == '-') {
    sign = -1;
    p++;
  } else if (*p == '+') {
    p++;
  }
  limit = sign < 0 ? (uint64_t)INT64_MAX + 1ULL : (uint64_t)INT64_MAX;
  while (*p != '\0') {
    if (isdigit((unsigned char)*p)) {
      unsigned digit = (unsigned)(*p - '0');
      if (acc > (limit - digit) / 10ULL) return 0;
      acc = acc * 10ULL + digit;
      saw_digit = 1;
      if (saw_dot) {
        if (scale == INT32_MAX) return 0;
        scale++;
      }
      p++;
      continue;
    }
    if (*p == '.') {
      if (saw_dot) return 0;
      saw_dot = 1;
      p++;
      continue;
    }
    if (isspace((unsigned char)*p)) {
      while (isspace((unsigned char)*p))
        p++;
      if (*p == '\0') break;
    }
    return 0;
  }
  if (!saw_digit) return 0;
  if (sign < 0) {
    out->mantissa = acc == (uint64_t)INT64_MAX + 1ULL ? INT64_MIN : -(int64_t)acc;
  } else {
    out->mantissa = (int64_t)acc;
  }
  out->scale = scale;
  return db_decimal_normalize(out);
}

static int db_decimal_to_text(DataBindDecimal value, char *out, size_t len) {
  char digits[32];
  char *p = digits + sizeof(digits);
  uint64_t mag;
  size_t digit_count;
  size_t pos = 0;
  int negative;

  if (out == NULL || len == 0 || value.scale < 0) return 0;
  db_decimal_normalize(&value);
  negative = value.mantissa < 0;
  mag = negative ? (uint64_t)(-(value.mantissa + 1)) + 1ULL : (uint64_t)value.mantissa;
  *--p = '\0';
  do {
    *--p = (char)('0' + (mag % 10ULL));
    mag /= 10ULL;
  } while (mag != 0);
  digit_count = strlen(p);

  if (negative) {
    if (pos + 1 >= len) return 0;
    out[pos++] = '-';
  }

  if (value.scale == 0) {
    if (pos + digit_count >= len) return 0;
    memcpy(out + pos, p, digit_count + 1);
    return 1;
  }

  if ((size_t)value.scale >= digit_count) {
    size_t zeros = (size_t)value.scale - digit_count;
    if (pos + 2 + zeros + digit_count >= len) return 0;
    out[pos++] = '0';
    out[pos++] = '.';
    while (zeros-- > 0)
      out[pos++] = '0';
    memcpy(out + pos, p, digit_count);
    pos += digit_count;
    out[pos] = '\0';
    return 1;
  }

  {
    size_t whole = digit_count - (size_t)value.scale;
    if (pos + digit_count + 1 >= len) return 0;
    memcpy(out + pos, p, whole);
    pos += whole;
    out[pos++] = '.';
    memcpy(out + pos, p + whole, (size_t)value.scale);
    pos += (size_t)value.scale;
    out[pos] = '\0';
    return 1;
  }
}

static DataBindValue *dbv_decimal(DataBindDecimal value) {
  DataBindValue *v;
  if (!db_decimal_normalize(&value)) return NULL;
  v = dbv_new(DATA_BIND_VALUE_DECIMAL);
  if (v != NULL) v->data.decimal_val = value;
  return v;
}

static DataBindValue *dbv_decimal_text(const char *text) {
  DataBindDecimal value;
  if (!db_parse_decimal_text(text, &value)) return NULL;
  return dbv_decimal(value);
}

static int db_validate_currency(const char *text);

static int db_bigint_canonical_text(const char *text, char *out, size_t len) {
  const char *p;
  const char *digits;
  size_t digits_len;
  int negative = 0;
  if (text == NULL || out == NULL || len == 0) return 0;
  p = text;
  while (isspace((unsigned char)*p))
    p++;
  if (*p == '-') {
    negative = 1;
    p++;
  } else if (*p == '+') {
    p++;
  }
  digits = p;
  while (*p == '0')
    p++;
  if (!isdigit((unsigned char)*p)) {
    const char *q = digits;
    int saw_zero = 0;
    while (*q == '0') {
      saw_zero = 1;
      q++;
    }
    while (isspace((unsigned char)*q))
      q++;
    if (!saw_zero || *q != '\0' || len < 2) return 0;
    memcpy(out, "0", 2);
    return 1;
  }
  digits = p;
  while (isdigit((unsigned char)*p))
    p++;
  digits_len = (size_t)(p - digits);
  while (isspace((unsigned char)*p))
    p++;
  if (*p != '\0' || digits_len == 0) return 0;
  if ((negative ? 1 : 0) + digits_len + 1 > len) return 0;
  if (negative) {
    out[0] = '-';
    memcpy(out + 1, digits, digits_len);
    out[digits_len + 1] = '\0';
  } else {
    memcpy(out, digits, digits_len);
    out[digits_len] = '\0';
  }
  return 1;
}

static DataBindValue *dbv_bigint_text(const char *text) {
  char stack_buf[256];
  char *canonical = stack_buf;
  size_t need;
  DataBindValue *v;
  if (text == NULL) return NULL;
  need = strlen(text) + 2;
  if (need > sizeof(stack_buf)) {
    canonical = (char *)malloc(need);
    if (canonical == NULL) return NULL;
  }
  if (!db_bigint_canonical_text(text, canonical, need)) {
    if (canonical != stack_buf) free(canonical);
    return NULL;
  }
  v = dbv_new(DATA_BIND_VALUE_BIGINT);
  if (v == NULL) {
    if (canonical != stack_buf) free(canonical);
    return NULL;
  }
  v->data.bigint_val.ptr = dbv_strdup(canonical);
  if (canonical != stack_buf) free(canonical);
  if (v->data.bigint_val.ptr == NULL) {
    data_bind_value_free(v);
    return NULL;
  }
  return v;
}

static int db_parse_money_text(const char *text, DataBindMoney *out) {
  char token_a[128];
  char token_b[128];
  char extra[2];
  DataBindDecimal amount;
  if (text == NULL || out == NULL) return 0;
  token_a[0] = token_b[0] = extra[0] = '\0';
  if (sscanf(text, " %127s %127s %1s", token_a, token_b, extra) != 2) return 0;
  if (db_validate_currency(token_a) && db_parse_decimal_text(token_b, &amount)) {
    out->amount = amount;
    memcpy(out->currency, token_a, 4);
    return 1;
  }
  if (db_parse_decimal_text(token_a, &amount) && db_validate_currency(token_b)) {
    out->amount = amount;
    memcpy(out->currency, token_b, 4);
    return 1;
  }
  return 0;
}

static int db_money_to_text(DataBindMoney value, char *out, size_t len) {
  char amount[64];
  if (out == NULL || len == 0 || !db_validate_currency(value.currency)) return 0;
  if (!db_decimal_to_text(value.amount, amount, sizeof(amount))) return 0;
  return snprintf(out, len, "%s %s", value.currency, amount) > 0;
}

static DataBindValue *dbv_money(DataBindMoney value) {
  DataBindValue *v;
  if (!db_validate_currency(value.currency) || !db_decimal_normalize(&value.amount)) return NULL;
  v = dbv_new(DATA_BIND_VALUE_MONEY);
  if (v != NULL) v->data.money_val = value;
  return v;
}

static DataBindValue *dbv_money_text(const char *text) {
  DataBindMoney value;
  if (!db_parse_money_text(text, &value)) return NULL;
  return dbv_money(value);
}

#define DATA_BIND_VALUE_CLONE_MAX_DEPTH 32u

static DataBindStatus dbv_clone_tree(const DataBindValue *source, size_t depth,
                                     DataBindValue **out_value) {
  DataBindValue *copy = NULL;
  DataBindValue *child = NULL;
  size_t i;
  DataBindStatus status;

  if (out_value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *out_value = NULL;
  if (source == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (depth > DATA_BIND_VALUE_CLONE_MAX_DEPTH) return DATA_BIND_ERR_RUNTIME;

  switch (source->kind) {
  case DATA_BIND_VALUE_NULL:
    copy = dbv_new(DATA_BIND_VALUE_NULL);
    break;
  case DATA_BIND_VALUE_OBJECT:
    copy = dbv_new(DATA_BIND_VALUE_OBJECT);
    if (copy == NULL) return DATA_BIND_ERR_OOM;
    for (i = 0; i < source->data.object_val.count; ++i) {
      const data_bind_value_field_t *field = &source->data.object_val.items[i];
      if (field->name == NULL || field->value == NULL) {
        status = DATA_BIND_ERR_RUNTIME;
        goto fail;
      }
      status = dbv_clone_tree(field->value, depth + 1u, &child);
      if (status != DATA_BIND_OK) goto fail;
      if (!dbv_object_set(copy, field->name, child)) {
        status = DATA_BIND_ERR_OOM;
        goto fail;
      }
      child = NULL;
    }
    break;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET:
    copy = dbv_new(source->kind);
    if (copy == NULL) return DATA_BIND_ERR_OOM;
    for (i = 0; i < source->data.array_val.count; ++i) {
      if (source->data.array_val.items[i] == NULL) {
        status = DATA_BIND_ERR_RUNTIME;
        goto fail;
      }
      status = dbv_clone_tree(source->data.array_val.items[i], depth + 1u, &child);
      if (status != DATA_BIND_OK) goto fail;
      if (!dbv_array_push(&copy->data.array_val, child)) {
        status = DATA_BIND_ERR_OOM;
        goto fail;
      }
      child = NULL;
    }
    break;
  case DATA_BIND_VALUE_MAP:
    copy = dbv_new(DATA_BIND_VALUE_MAP);
    if (copy == NULL) return DATA_BIND_ERR_OOM;
    for (i = 0; i < source->data.map_val.count; ++i) {
      const data_bind_value_map_entry_t *entry = &source->data.map_val.items[i];
      if (entry->key == NULL || entry->value == NULL) {
        status = DATA_BIND_ERR_RUNTIME;
        goto fail;
      }
      status = dbv_clone_tree(entry->value, depth + 1u, &child);
      if (status != DATA_BIND_OK) goto fail;
      if (!dbv_map_set(copy, entry->key, child)) {
        status = DATA_BIND_ERR_OOM;
        goto fail;
      }
      child = NULL;
    }
    break;
  case DATA_BIND_VALUE_INT:
    copy = dbv_int(source->data.int_val);
    break;
  case DATA_BIND_VALUE_INT64:
    copy = dbv_int64(source->data.int64_val);
    break;
  case DATA_BIND_VALUE_UINT64:
    copy = dbv_uint64(source->data.uint64_val);
    break;
  case DATA_BIND_VALUE_DOUBLE:
    copy = dbv_double(source->data.double_val);
    break;
  case DATA_BIND_VALUE_BOOL:
    copy = dbv_bool(source->data.bool_val);
    break;
  case DATA_BIND_VALUE_STRING:
    if (source->data.string_val.ptr == NULL) return DATA_BIND_ERR_RUNTIME;
    copy = dbv_string(source->data.string_val.ptr);
    break;
  case DATA_BIND_VALUE_BYTES:
    if (source->data.bytes_val.len > 0u && source->data.bytes_val.ptr == NULL) {
      return DATA_BIND_ERR_RUNTIME;
    }
    copy = dbv_bytes(source->data.bytes_val.ptr, source->data.bytes_val.len);
    break;
  case DATA_BIND_VALUE_UUID:
    copy = dbv_uuid_bytes(source->data.uuid_val.bytes);
    break;
  case DATA_BIND_VALUE_DATETIME:
    copy = dbv_datetime(source->data.datetime_val);
    break;
  case DATA_BIND_VALUE_DATE:
    copy = dbv_date(source->data.date_val);
    break;
  case DATA_BIND_VALUE_TIME:
    copy = dbv_time(source->data.time_val);
    break;
  case DATA_BIND_VALUE_DURATION:
    copy = dbv_duration(source->data.duration_ms);
    break;
  case DATA_BIND_VALUE_DECIMAL:
    copy = dbv_decimal(source->data.decimal_val);
    break;
  case DATA_BIND_VALUE_BIGINT:
    if (source->data.bigint_val.ptr == NULL) return DATA_BIND_ERR_RUNTIME;
    copy = dbv_bigint_text(source->data.bigint_val.ptr);
    break;
  case DATA_BIND_VALUE_MONEY:
    copy = dbv_money(source->data.money_val);
    break;
  default:
    return DATA_BIND_ERR_RUNTIME;
  }

  if (copy == NULL) return DATA_BIND_ERR_OOM;
  *out_value = copy;
  return DATA_BIND_OK;

fail:
  data_bind_value_free(child);
  data_bind_value_free(copy);
  return status;
}

DataBindStatus data_bind_value_clone(const DataBindValue *value, DataBindValue **out_value) {
  return dbv_clone_tree(value, 0u, out_value);
}

static DataBindValue *dynamic_create_object(void) {
  return (DataBindValue *)dbv_new(DATA_BIND_VALUE_OBJECT);
}
static DataBindValue *dynamic_create_list(void) {
  return (DataBindValue *)dbv_new(DATA_BIND_VALUE_LIST);
}
static DataBindValue *dynamic_create_set(void) {
  return (DataBindValue *)dbv_new(DATA_BIND_VALUE_SET);
}
static DataBindValue *dynamic_create_map(void) {
  return (DataBindValue *)dbv_new(DATA_BIND_VALUE_MAP);
}

static DataBindValue *record_create_from_layout_v1(const data_bind_record_layout_t *layout) {
  DataBindValue *value;
  size_t i;
  if (layout == NULL || layout->count > UINT32_MAX) return NULL;
  value = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (value == NULL) return NULL;
  value->record_layout = layout;
  if (layout->count == 0) return value;
  value->data.object_val.items =
      (data_bind_value_field_t *)calloc(layout->count, sizeof(*value->data.object_val.items));
  if (value->data.object_val.items == NULL) {
    g_dynamic_runtime_oom = 1;
    data_bind_value_free(value);
    return NULL;
  }
  value->data.object_val.capacity = layout->count;
  for (i = 0; i < layout->count; ++i) {
    value->data.object_val.items[i].name = dbv_strdup(layout->fields[i].name);
    if (value->data.object_val.items[i].name == NULL) {
      value->data.object_val.count = i;
      data_bind_value_free(value);
      return NULL;
    }
    value->data.object_val.count = i + 1;
  }
  return value;
}

static DataBindValue *record_create_child_v1(DataBindValue *container) {
  if (container == NULL ||
      (container->kind != DATA_BIND_VALUE_LIST && container->kind != DATA_BIND_VALUE_SET))
    return NULL;
  return record_create_from_layout_v1(container->record_layout);
}

static int record_set_slot_value_v1(DataBindValue *obj, uint32_t slot, DataBindValue *child) {
  const data_bind_record_layout_field_t *field;
  if (obj == NULL || obj->kind != DATA_BIND_VALUE_OBJECT || obj->record_layout == NULL ||
      child == NULL || (size_t)slot >= obj->data.object_val.count ||
      obj->data.object_val.items[slot].value != NULL) {
    data_bind_value_free(child);
    return 0;
  }
  field = &obj->record_layout->fields[slot];
  child->record_layout = field->child;
  obj->data.object_val.items[slot].value = child;
  return 1;
}

static DataBindValue *record_create_field_v1(DataBindValue *obj, uint32_t slot) {
  DataBindValue *child;
  if (obj == NULL || obj->kind != DATA_BIND_VALUE_OBJECT || obj->record_layout == NULL ||
      (size_t)slot >= obj->data.object_val.count)
    return NULL;
  child = record_create_from_layout_v1(obj->record_layout->fields[slot].child);
  if (child == NULL) return NULL;
  if (!record_set_slot_value_v1(obj, slot, child)) return NULL;
  return child;
}

static int record_set_slot_int_v1(DataBindValue *obj, uint32_t slot, int32_t val) {
  return record_set_slot_value_v1(obj, slot, dbv_int(val));
}

static int record_set_slot_uint32_v1(DataBindValue *obj, uint32_t slot, uint32_t val) {
  return record_set_slot_value_v1(obj, slot, dbv_uint32_compat(val));
}

static int record_set_slot_int64_v1(DataBindValue *obj, uint32_t slot, int64_t val) {
  return record_set_slot_value_v1(obj, slot, dbv_int64(val));
}

static int record_set_slot_uint64_v1(DataBindValue *obj, uint32_t slot, uint64_t val) {
  return record_set_slot_value_v1(obj, slot, dbv_uint64(val));
}

static int record_set_slot_double_v1(DataBindValue *obj, uint32_t slot, double val) {
  return record_set_slot_value_v1(obj, slot, dbv_double(val));
}

static int record_set_slot_bool_v1(DataBindValue *obj, uint32_t slot, int val) {
  return record_set_slot_value_v1(obj, slot, dbv_bool(val));
}

static int record_set_slot_string_v1(DataBindValue *obj, uint32_t slot, const char *val) {
  return record_set_slot_value_v1(obj, slot, dbv_string(val));
}

static int record_set_slot_bytes_v1(DataBindValue *obj, uint32_t slot, const uint8_t *data,
                                    size_t len) {
  return record_set_slot_value_v1(obj, slot, dbv_bytes(data, len));
}

static int record_set_slot_uuid_v1(DataBindValue *obj, uint32_t slot, const uint8_t *data) {
  return record_set_slot_value_v1(obj, slot, dbv_uuid_bytes(data));
}

static int record_set_slot_list_v1(DataBindValue *obj, uint32_t slot, DataBindValue *list) {
  if (list == NULL || list->kind != DATA_BIND_VALUE_LIST) {
    data_bind_value_free(list);
    return 0;
  }
  return record_set_slot_value_v1(obj, slot, list);
}

static int record_set_slot_set_v1(DataBindValue *obj, uint32_t slot, DataBindValue *set) {
  if (set == NULL || set->kind != DATA_BIND_VALUE_SET) {
    data_bind_value_free(set);
    return 0;
  }
  return record_set_slot_value_v1(obj, slot, set);
}

static int record_set_slot_map_v1(DataBindValue *obj, uint32_t slot, DataBindValue *map) {
  if (map == NULL || map->kind != DATA_BIND_VALUE_MAP) {
    data_bind_value_free(map);
    return 0;
  }
  return record_set_slot_value_v1(obj, slot, map);
}

static void record_clear_layout_v1(DataBindValue *value) {
  size_t i;
  if (value == NULL) return;
  value->record_layout = NULL;
  if (value->kind == DATA_BIND_VALUE_OBJECT) {
    for (i = 0; i < value->data.object_val.count; ++i)
      record_clear_layout_v1(value->data.object_val.items[i].value);
  } else if (value->kind == DATA_BIND_VALUE_LIST || value->kind == DATA_BIND_VALUE_SET) {
    for (i = 0; i < value->data.array_val.count; ++i)
      record_clear_layout_v1(value->data.array_val.items[i]);
  } else if (value->kind == DATA_BIND_VALUE_MAP) {
    for (i = 0; i < value->data.map_val.count; ++i)
      record_clear_layout_v1(value->data.map_val.items[i].value);
  }
}

static int dynamic_set_field_int(DataBindValue *obj, const char *name, int32_t val) {
  DataBindValue *child = dbv_int(val);
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_set_field_uint32(DataBindValue *obj, const char *name, uint32_t val) {
  DataBindValue *child = dbv_uint32_compat(val);
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_set_field_int64(DataBindValue *obj, const char *name, int64_t val) {
  DataBindValue *child = dbv_int64(val);
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_set_field_uint64(DataBindValue *obj, const char *name, uint64_t val) {
  DataBindValue *child = dbv_uint64(val);
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_set_field_double(DataBindValue *obj, const char *name, double val) {
  DataBindValue *child = dbv_double(val);
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_set_field_bool(DataBindValue *obj, const char *name, int val) {
  DataBindValue *child = dbv_bool(val);
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_set_field_string(DataBindValue *obj, const char *name, const char *val) {
  DataBindValue *child = dbv_string(val);
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_set_field_bytes(DataBindValue *obj, const char *name, const uint8_t *data,
                                   size_t len) {
  DataBindValue *child = dbv_bytes(data, len);
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_set_field_uuid(DataBindValue *obj, const char *name, const uint8_t *data) {
  DataBindValue *child = dbv_uuid_bytes(data);
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_set_field_value(DataBindValue *obj, const char *name, DataBindValue *child) {
  if (child == NULL || !dbv_object_set((DataBindValue *)obj, name, (DataBindValue *)child)) {
    data_bind_value_free((DataBindValue *)child);
    return 0;
  }
  return 1;
}
static int dynamic_add_list_item_int(DataBindValue *list, int32_t val) {
  DataBindValue *child = dbv_int(val);
  if (child == NULL || !dbv_array_push(&((DataBindValue *)list)->data.array_val, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_list_item_uint32(DataBindValue *list, uint32_t val) {
  DataBindValue *child = dbv_uint32_compat(val);
  if (child == NULL || !dbv_array_push(&((DataBindValue *)list)->data.array_val, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_list_item_int64(DataBindValue *list, int64_t val) {
  DataBindValue *child = dbv_int64(val);
  if (child == NULL || !dbv_array_push(&((DataBindValue *)list)->data.array_val, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_list_item_uint64(DataBindValue *list, uint64_t val) {
  DataBindValue *child = dbv_uint64(val);
  if (child == NULL || !dbv_array_push(&((DataBindValue *)list)->data.array_val, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_list_item_double(DataBindValue *list, double val) {
  DataBindValue *child = dbv_double(val);
  if (child == NULL || !dbv_array_push(&((DataBindValue *)list)->data.array_val, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_list_item_bool(DataBindValue *list, int val) {
  DataBindValue *child = dbv_bool(val);
  if (child == NULL || !dbv_array_push(&((DataBindValue *)list)->data.array_val, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_list_item_string(DataBindValue *list, const char *val) {
  DataBindValue *child = dbv_string(val);
  if (child == NULL || !dbv_array_push(&((DataBindValue *)list)->data.array_val, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_list_item_object(DataBindValue *list, DataBindValue *obj) {
  if (obj == NULL ||
      !dbv_array_push(&((DataBindValue *)list)->data.array_val, (DataBindValue *)obj)) {
    data_bind_value_free((DataBindValue *)obj);
    return 0;
  }
  return 1;
}
static int dynamic_add_map_entry_string_string(DataBindValue *map, const char *key,
                                               const char *val) {
  DataBindValue *child = dbv_string(val);
  if (child == NULL || !dbv_map_set((DataBindValue *)map, key, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_map_entry_string_int(DataBindValue *map, const char *key, int32_t val) {
  DataBindValue *child = dbv_int(val);
  if (child == NULL || !dbv_map_set((DataBindValue *)map, key, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_map_entry_string_uint32(DataBindValue *map, const char *key, uint32_t val) {
  DataBindValue *child = dbv_uint32_compat(val);
  if (child == NULL || !dbv_map_set((DataBindValue *)map, key, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_map_entry_string_int64(DataBindValue *map, const char *key, int64_t val) {
  DataBindValue *child = dbv_int64(val);
  if (child == NULL || !dbv_map_set((DataBindValue *)map, key, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_map_entry_string_uint64(DataBindValue *map, const char *key, uint64_t val) {
  DataBindValue *child = dbv_uint64(val);
  if (child == NULL || !dbv_map_set((DataBindValue *)map, key, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_map_entry_string_double(DataBindValue *map, const char *key, double val) {
  DataBindValue *child = dbv_double(val);
  if (child == NULL || !dbv_map_set((DataBindValue *)map, key, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}
static int dynamic_add_map_entry_string_bool(DataBindValue *map, const char *key, int val) {
  DataBindValue *child = dbv_bool(val);
  if (child == NULL || !dbv_map_set((DataBindValue *)map, key, child)) {
    data_bind_value_free(child);
    return 0;
  }
  return 1;
}

static const data_bind_runtime_api_t DYNAMIC_VALUE_API = {
    .create_object = dynamic_create_object,
    .free_value = data_bind_value_free,
    .set_field_int = dynamic_set_field_int,
    .set_field_uint32 = dynamic_set_field_uint32,
    .set_field_int64 = dynamic_set_field_int64,
    .set_field_uint64 = dynamic_set_field_uint64,
    .set_field_double = dynamic_set_field_double,
    .set_field_bool = dynamic_set_field_bool,
    .set_field_string = dynamic_set_field_string,
    .set_field_bytes = dynamic_set_field_bytes,
    .set_field_uuid = dynamic_set_field_uuid,
    .create_list = dynamic_create_list,
    .add_list_item_int = dynamic_add_list_item_int,
    .add_list_item_uint32 = dynamic_add_list_item_uint32,
    .add_list_item_int64 = dynamic_add_list_item_int64,
    .add_list_item_uint64 = dynamic_add_list_item_uint64,
    .add_list_item_double = dynamic_add_list_item_double,
    .add_list_item_bool = dynamic_add_list_item_bool,
    .add_list_item_string = dynamic_add_list_item_string,
    .add_list_item_object = dynamic_add_list_item_object,
    .set_field_list = dynamic_set_field_value,
    .set_field_object = dynamic_set_field_value,
    .create_set = dynamic_create_set,
    .add_set_item_int = dynamic_add_list_item_int,
    .add_set_item_uint32 = dynamic_add_list_item_uint32,
    .add_set_item_int64 = dynamic_add_list_item_int64,
    .add_set_item_uint64 = dynamic_add_list_item_uint64,
    .add_set_item_double = dynamic_add_list_item_double,
    .add_set_item_bool = dynamic_add_list_item_bool,
    .add_set_item_string = dynamic_add_list_item_string,
    .set_field_set = dynamic_set_field_value,
    .create_map = dynamic_create_map,
    .add_map_entry_string_string = dynamic_add_map_entry_string_string,
    .add_map_entry_string_int = dynamic_add_map_entry_string_int,
    .add_map_entry_string_uint32 = dynamic_add_map_entry_string_uint32,
    .add_map_entry_string_int64 = dynamic_add_map_entry_string_int64,
    .add_map_entry_string_uint64 = dynamic_add_map_entry_string_uint64,
    .add_map_entry_string_double = dynamic_add_map_entry_string_double,
    .add_map_entry_string_bool = dynamic_add_map_entry_string_bool,
    .set_field_map = dynamic_set_field_value};

static DataBindValue *container_noop(void) { return NULL; }
static int add_i32_noop(DataBindValue *v, int32_t x) {
  (void)v;
  (void)x;
  return 1;
}
static int add_u32_noop(DataBindValue *v, uint32_t x) {
  (void)v;
  (void)x;
  return 1;
}
static int add_i64_noop(DataBindValue *v, int64_t x) {
  (void)v;
  (void)x;
  return 1;
}
static int add_u64_noop(DataBindValue *v, uint64_t x) {
  (void)v;
  (void)x;
  return 1;
}
static int add_dbl_noop(DataBindValue *v, double x) {
  (void)v;
  (void)x;
  return 1;
}
static int add_bool_noop(DataBindValue *v, int x) {
  (void)v;
  (void)x;
  return 1;
}
static int add_str_noop(DataBindValue *v, const char *s) {
  (void)v;
  (void)s;
  return 1;
}
static int add_obj_noop(DataBindValue *v, DataBindValue *o) {
  (void)v;
  (void)o;
  return 1;
}
static int set_container_noop(DataBindValue *o, const char *n, DataBindValue *v) {
  (void)o;
  (void)n;
  (void)v;
  return 1;
}
static int add_map_str_str_noop(DataBindValue *m, const char *k, const char *v) {
  (void)m;
  (void)k;
  (void)v;
  return 1;
}
static int add_map_str_int_noop(DataBindValue *m, const char *k, int32_t v) {
  (void)m;
  (void)k;
  (void)v;
  return 1;
}
static int add_map_str_u32_noop(DataBindValue *m, const char *k, uint32_t v) {
  (void)m;
  (void)k;
  (void)v;
  return 1;
}
static int add_map_str_i64_noop(DataBindValue *m, const char *k, int64_t v) {
  (void)m;
  (void)k;
  (void)v;
  return 1;
}
static int add_map_str_u64_noop(DataBindValue *m, const char *k, uint64_t v) {
  (void)m;
  (void)k;
  (void)v;
  return 1;
}
static int add_map_str_dbl_noop(DataBindValue *m, const char *k, double v) {
  (void)m;
  (void)k;
  (void)v;
  return 1;
}
static int add_map_str_bool_noop(DataBindValue *m, const char *k, int v) {
  (void)m;
  (void)k;
  (void)v;
  return 1;
}
static DataBindValue *create_value_noop(void) { return NULL; }
static void free_value_noop(DataBindValue *value) { (void)value; }

static const data_bind_runtime_api_t MIR_OUTPUT_API = {
    .create_object = create_value_noop,
    .free_value = free_value_noop,
    .set_field_int = set_i32_noop,
    .set_field_uint32 = set_u32_noop,
    .set_field_int64 = set_i64_noop,
    .set_field_uint64 = set_u64_noop,
    .set_field_double = set_dbl_noop,
    .set_field_bool = set_bool_noop,
    .set_field_string = set_str_noop,
    .set_field_bytes = set_bytes_noop,
    .set_field_uuid = set_uuid_noop,
    .create_list = create_value_noop,
    .add_list_item_int = add_i32_noop,
    .add_list_item_uint32 = add_u32_noop,
    .add_list_item_int64 = add_i64_noop,
    .add_list_item_uint64 = add_u64_noop,
    .add_list_item_double = add_dbl_noop,
    .add_list_item_bool = add_bool_noop,
    .add_list_item_string = add_str_noop,
    .add_list_item_object = add_obj_noop,
    .set_field_list = set_container_noop,
    .set_field_object = set_container_noop,
    .create_set = create_value_noop,
    .add_set_item_int = add_i32_noop,
    .add_set_item_uint32 = add_u32_noop,
    .add_set_item_int64 = add_i64_noop,
    .add_set_item_uint64 = add_u64_noop,
    .add_set_item_double = add_dbl_noop,
    .add_set_item_bool = add_bool_noop,
    .add_set_item_string = add_str_noop,
    .set_field_set = set_container_noop,
    .create_map = create_value_noop,
    .add_map_entry_string_string = add_map_str_str_noop,
    .add_map_entry_string_int = add_map_str_int_noop,
    .add_map_entry_string_uint32 = add_map_str_u32_noop,
    .add_map_entry_string_int64 = add_map_str_i64_noop,
    .add_map_entry_string_uint64 = add_map_str_u64_noop,
    .add_map_entry_string_double = add_map_str_dbl_noop,
    .add_map_entry_string_bool = add_map_str_bool_noop,
    .set_field_map = set_container_noop};

typedef enum data_bind_text_kind {
  DB_TEXT_NUMBER,
  DB_TEXT_INTEGER,
  DB_TEXT_STRING,
  DB_TEXT_BYTES,
  DB_TEXT_BOOL,
  DB_TEXT_UUID,
  DB_TEXT_DATETIME,
  DB_TEXT_DATE,
  DB_TEXT_TIME,
  DB_TEXT_DURATION,
  DB_TEXT_DECIMAL,
  DB_TEXT_BIGINT,
  DB_TEXT_MONEY,
  DB_TEXT_UNSUPPORTED
} data_bind_text_kind_t;

typedef struct data_bind_csv_headers {
  char **names;
  size_t count;
  size_t capacity;
} data_bind_csv_headers_t;

typedef struct data_bind_index_list {
  size_t *values;
  size_t count;
  size_t capacity;
} data_bind_index_list_t;

static int set_codec_error(DataBind *codec, const char *fmt, ...) {
  va_list ap;
  if (codec == NULL) return 0;
  va_start(ap, fmt);
  vsnprintf(codec->error, sizeof(codec->error), fmt, ap);
  va_end(ap);
  return 0;
}

/**
 * @brief Compute a simple hash of schema text for caching.
 * Uses FNV-1a hash algorithm for simplicity.
 */
static void compute_schema_hash(const char *schema_text, size_t len, char *hash_out) {
  uint64_t hash = 14695981039346656037ULL; /* FNV offset basis */
  size_t i;
  if (schema_text == NULL || hash_out == NULL) return;

  for (i = 0; i < len; i++) {
    hash ^= (uint64_t)(unsigned char)schema_text[i];
    hash *= 1099511628211ULL; /* FNV prime */
  }

  snprintf(hash_out, 65, "%016llx", (unsigned long long)hash);
}

static int data_bind_bmir_read_byte(MIR_context_t ctx) {
  data_bind_bmir_input_t *input = g_data_bind_bmir_input;
  (void)ctx;
  if (input == NULL || input->offset >= input->len) return EOF;
  return input->data[input->offset++];
}

static mir_cache_entry_t *mir_cache_find(const char *schema_hash) {
  mir_cache_entry_t *entry;
  if (!g_mir_cache_enabled || schema_hash == NULL) return NULL;

  for (entry = g_mir_cache_head; entry != NULL; entry = entry->next) {
    if (!entry->evicted && strcmp(entry->schema_hash, schema_hash) == 0) {
      return entry;
    }
  }
  return NULL;
}

static mir_cache_entry_t *mir_cache_insert(const char *schema_hash, MIR_context_t ctx,
                                           mir_func_node_t *func_head) {
  mir_cache_entry_t *entry;
  if (!g_mir_cache_enabled || schema_hash == NULL || ctx == NULL) return NULL;

  entry = (mir_cache_entry_t *)malloc(sizeof(*entry));
  if (entry == NULL) return NULL;

  snprintf(entry->schema_hash, sizeof(entry->schema_hash), "%s", schema_hash);
  entry->shared_ctx = ctx;
  entry->func_head = func_head;
  entry->ref_count = 1;
  entry->evicted = 0;
  entry->next = g_mir_cache_head;
  g_mir_cache_head = entry;

  return entry;
}

static void mir_cache_entry_destroy(mir_cache_entry_t *entry) {
  mir_func_node_t *func_node;
  if (entry == NULL) return;
  if (entry->shared_ctx != NULL) {
    MIR_gen_finish(entry->shared_ctx);
    MIR_finish(entry->shared_ctx);
  }
  func_node = entry->func_head;
  while (func_node != NULL) {
    mir_func_node_t *next = func_node->next;
    free(func_node->type_name);
    free(func_node);
    func_node = next;
  }
  free(entry);
}

static void mir_cache_release(const char *schema_hash, MIR_context_t shared_ctx) {
  mir_cache_entry_t **prev_ptr = &g_mir_cache_head;
  mir_cache_entry_t *entry;

  if (schema_hash == NULL) return;

  entry = g_mir_cache_head;
  while (entry != NULL) {
    if (entry->shared_ctx == shared_ctx && strcmp(entry->schema_hash, schema_hash) == 0) {
      entry->ref_count--;
      if (entry->ref_count <= 0) {
        *prev_ptr = entry->next;
        mir_cache_entry_destroy(entry);
      }
      return;
    }
    prev_ptr = &entry->next;
    entry = entry->next;
  }
}

static void db_error_clear(DataBindError *error) {
  if (error == NULL || error->size < offsetof(DataBindError, message)) return;
  error->code = DATA_BIND_OK;
  error->line = -1;
  error->column = -1;
  if (error->size >= offsetof(DataBindError, path) + sizeof(error->path)) error->path[0] = '\0';
  if (error->size >= offsetof(DataBindError, message) + sizeof(error->message))
    error->message[0] = '\0';
}

/**
 * @brief Format error path for consistent error reporting across input formats.
 * @param out Output buffer for formatted path
 * @param out_size Size of output buffer
 * @param format Format identifier: "binary", "json", "csv", "xml"
 * @param location Format-specific location (e.g., "offset 123", "$.path", "row 5 col 3")
 */
static void db_error_format_path(char *out, size_t out_size, const char *format,
                                 const char *location) {
  if (out == NULL || out_size == 0) return;
  if (format == NULL || format[0] == '\0') {
    snprintf(out, out_size, "%s", location != NULL ? location : "");
  } else if (location != NULL && location[0] != '\0') {
    snprintf(out, out_size, "%s: %s", format, location);
  } else {
    snprintf(out, out_size, "%s", format);
  }
}

static DataBindStatus db_error_set(DataBindError *error, DataBindStatus code, const char *path,
                                   int line, int column, const char *fmt, ...) {
  va_list ap;
  if (error != NULL && error->size >= offsetof(DataBindError, message)) {
    error->code = code;
    error->line = line;
    error->column = column;
    if (error->size >= offsetof(DataBindError, path) + sizeof(error->path)) {
      snprintf(error->path, sizeof(error->path), "%s", path != NULL ? path : "");
    }
    if (error->size >= offsetof(DataBindError, message) + sizeof(error->message)) {
      va_start(ap, fmt);
      vsnprintf(error->message, sizeof(error->message), fmt, ap);
      va_end(ap);
    }
  }
  return code;
}

static DataBindStatus db_codec_error(DataBind *codec, DataBindError *error, DataBindStatus code,
                                     const char *fmt, ...) {
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  if (codec != NULL) snprintf(codec->error, sizeof(codec->error), "%s", msg);
  return db_error_set(error, code, NULL, -1, -1, "%s", msg);
}

static void *codec_alloc(DataBind *codec, size_t size) {
  owned_alloc_node_t *node = NULL;
  void *ptr = calloc(1, size);
  if (ptr == NULL) return NULL;
  node = (owned_alloc_node_t *)malloc(sizeof(*node));
  if (node == NULL) {
    free(ptr);
    return NULL;
  }
  node->ptr = ptr;
  node->next = codec->owned_allocs;
  codec->owned_allocs = node;
  return ptr;
}

static char *codec_strdup(DataBind *codec, const char *src) {
  size_t len;
  char *dst;
  if (src == NULL) return NULL;
  len = strlen(src) + 1;
  dst = (char *)codec_alloc(codec, len);
  if (dst == NULL) return NULL;
  memcpy(dst, src, len);
  return dst;
}

static char *codec_strdup_n(DataBind *codec, const char *src, size_t len) {
  char *dst = (char *)codec_alloc(codec, len + 1);
  if (dst == NULL) return NULL;
  memcpy(dst, src, len);
  dst[len] = '\0';
  return dst;
}

static Node *find_child(Node *parent, const char *name) {
  size_t i;
  if (parent == NULL || parent->type != NODE_MAP) return NULL;
  for (i = 0; i < parent->data.map.count; i++)
    if (strcmp(parent->data.map.items[i]->name, name) == 0) return parent->data.map.items[i];
  return NULL;
}

static const char *get_string_val(Node *node) {
  return (node != NULL && node->type == NODE_STRING) ? node->data.string_val : NULL;
}
static int field_flag(Node *field_node, const char *name) {
  Node *n = find_child(field_node, name);
  return n != NULL && n->type == NODE_STRING && strcmp(n->data.string_val, "1") == 0;
}

static int record_flag(Node *node, const char *name) { return field_flag(node, name); }

static const char *node_attribute_value(Node *node, const char *name) {
  Node *attrs;
  size_t i;
  if (node == NULL || name == NULL) return NULL;
  attrs = find_child(node, "attributes");
  if (attrs == NULL || attrs->type != NODE_LIST) return NULL;
  for (i = 0; i < attrs->data.list.count; i++) {
    Node *attr = attrs->data.list.items[i];
    const char *attr_name = get_string_val(find_child(attr, "name"));
    if (attr_name != NULL && strcmp(attr_name, name) == 0)
      return get_string_val(find_child(attr, "value"));
  }
  return NULL;
}

static const char *field_format(Node *field) { return node_attribute_value(field, "format"); }

static int db_text_is_empty(const char *text) { return text == NULL || text[0] == '\0'; }

static int db_parse_i64_text(const char *text, int64_t *out) {
  char *end = NULL;
  long long value;
  if (text == NULL || out == NULL) return 0;
  errno = 0;
  value = strtoll(text, &end, 10);
  if (errno != 0 || end == text || end == NULL || *end != '\0') return 0;
  *out = (int64_t)value;
  return 1;
}

static int db_parse_double_text(const char *text, double *out) {
  char *end = NULL;
  double value;
  if (text == NULL || out == NULL) return 0;
  errno = 0;
  value = strtod(text, &end);
  if (errno != 0 || end == text || end == NULL || *end != '\0') return 0;
  *out = value;
  return 1;
}

static int db_parse_bool_text(const char *text, int *out) {
  if (text == NULL || out == NULL) return 0;
  if (strcmp(text, "true") == 0 || strcmp(text, "1") == 0 || strcmp(text, "yes") == 0) {
    *out = 1;
    return 1;
  }
  if (strcmp(text, "false") == 0 || strcmp(text, "0") == 0 || strcmp(text, "no") == 0) {
    *out = 0;
    return 1;
  }
  return 0;
}

static int db_is_alpha_num(char c) { return isalnum((unsigned char)c) != 0; }

static int db_is_hex_char(char c) { return isxdigit((unsigned char)c) != 0; }

static int db_validate_ipv4_n(const char *text, size_t len) {
  size_t pos = 0;
  int part;
  if (text == NULL || len == 0) return 0;
  for (part = 0; part < 4; part++) {
    int value = 0;
    int digits = 0;
    if (pos >= len || !isdigit((unsigned char)text[pos])) return 0;
    while (pos < len && isdigit((unsigned char)text[pos])) {
      value = value * 10 + (text[pos] - '0');
      digits++;
      if (digits > 3 || value > 255) return 0;
      pos++;
    }
    if (part < 3) {
      if (pos >= len || text[pos] != '.') return 0;
      pos++;
    }
  }
  return pos == len;
}

static int db_validate_ipv6_n(const char *text, size_t len) {
  size_t pos = 0;
  int groups = 0;
  int compressed = 0;
  if (text == NULL || len < 2) return 0;
  while (pos < len) {
    size_t start;
    int digits = 0;
    if (text[pos] == ':') {
      if (pos + 1 >= len || text[pos + 1] != ':' || compressed) return 0;
      compressed = 1;
      pos += 2;
      if (pos == len) break;
      continue;
    }
    start = pos;
    while (pos < len && db_is_hex_char(text[pos]) && digits < 4) {
      digits++;
      pos++;
    }
    if (pos < len && text[pos] == '.') {
      size_t ipv4_start = start;
      while (ipv4_start > 0 && text[ipv4_start - 1] != ':')
        ipv4_start--;
      if (!db_validate_ipv4_n(text + ipv4_start, len - ipv4_start)) return 0;
      groups += 2;
      pos = len;
      break;
    }
    if (digits == 0 || (pos < len && db_is_hex_char(text[pos]))) return 0;
    groups++;
    if (pos == len) break;
    if (text[pos] != ':') return 0;
    pos++;
    if (pos < len && text[pos] == ':') {
      if (compressed) return 0;
      compressed = 1;
      pos++;
      if (pos == len) break;
    }
    if (pos == len) return 0;
  }
  return compressed ? groups < 8 : groups == 8;
}

static int db_validate_ipaddr(const char *text) {
  size_t len;
  if (text == NULL) return 0;
  len = strlen(text);
  return db_validate_ipv4_n(text, len) || db_validate_ipv6_n(text, len);
}

static int db_parse_uint_n(const char *text, size_t len, int *out) {
  size_t i;
  int value = 0;
  if (text == NULL || len == 0 || out == NULL) return 0;
  for (i = 0; i < len; i++) {
    if (!isdigit((unsigned char)text[i])) return 0;
    value = value * 10 + (text[i] - '0');
    if (value > 1000) return 0;
  }
  *out = value;
  return 1;
}

static int db_validate_cidr(const char *text) {
  const char *slash;
  size_t addr_len;
  int prefix = 0;
  int is_v4;
  if (text == NULL) return 0;
  slash = strchr(text, '/');
  if (slash == NULL || slash == text || slash[1] == '\0') return 0;
  addr_len = (size_t)(slash - text);
  is_v4 = db_validate_ipv4_n(text, addr_len);
  if (!is_v4 && !db_validate_ipv6_n(text, addr_len)) return 0;
  if (!db_parse_uint_n(slash + 1, strlen(slash + 1), &prefix)) return 0;
  return prefix >= 0 && prefix <= (is_v4 ? 32 : 128);
}

static int db_validate_hostname_like(const char *text, int require_dot) {
  size_t len;
  size_t label_len = 0;
  int saw_dot = 0;
  char prev = '\0';
  size_t i;
  if (text == NULL) return 0;
  len = strlen(text);
  if (len == 0 || len > 253) return 0;
  for (i = 0; i < len; i++) {
    char c = text[i];
    if (c == '.') {
      if (label_len == 0 || prev == '-') return 0;
      saw_dot = 1;
      label_len = 0;
    } else if (db_is_alpha_num(c) || c == '-') {
      if (label_len == 0 && c == '-') return 0;
      label_len++;
      if (label_len > 63) return 0;
    } else {
      return 0;
    }
    prev = c;
  }
  if (label_len == 0 || prev == '-') return 0;
  return !require_dot || saw_dot;
}

static int db_validate_email(const char *text) {
  const char *at;
  size_t local_len;
  size_t i;
  if (text == NULL) return 0;
  at = strchr(text, '@');
  if (at == NULL || strchr(at + 1, '@') != NULL) return 0;
  local_len = (size_t)(at - text);
  if (local_len == 0 || local_len > 64 || at[1] == '\0') return 0;
  if (text[0] == '.' || text[local_len - 1] == '.') return 0;
  for (i = 0; i < local_len; i++) {
    char c = text[i];
    if (c == '.' && i > 0 && text[i - 1] == '.') return 0;
    if (!(db_is_alpha_num(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-')) return 0;
  }
  return db_validate_hostname_like(at + 1, 1);
}

static int db_validate_scheme(const char *text, const char **after_colon) {
  const char *p;
  if (text == NULL || !isalpha((unsigned char)text[0])) return 0;
  p = text + 1;
  while (*p != '\0' && *p != ':') {
    if (!(db_is_alpha_num(*p) || *p == '+' || *p == '-' || *p == '.')) return 0;
    p++;
  }
  if (*p != ':') return 0;
  if (after_colon != NULL) *after_colon = p + 1;
  return 1;
}

static int db_validate_uri_text(const char *text, int require_authority) {
  const char *rest;
  const char *host_start;
  const char *host_end;
  const char *p;
  if (!db_validate_scheme(text, &rest) || rest[0] == '\0') return 0;
  for (p = rest; *p != '\0'; p++) {
    if ((unsigned char)*p <= 0x20 || (unsigned char)*p == 0x7f) return 0;
  }
  if (!require_authority) return 1;
  if (rest[0] != '/' || rest[1] != '/') return 0;
  host_start = rest + 2;
  if (*host_start == '\0') return 0;
  if (*host_start == '[') {
    host_end = strchr(host_start, ']');
    if (host_end == NULL ||
        !db_validate_ipv6_n(host_start + 1, (size_t)(host_end - host_start - 1)))
      return 0;
    return host_end[1] == '\0' || host_end[1] == ':' || host_end[1] == '/' || host_end[1] == '?' ||
           host_end[1] == '#';
  }
  host_end = host_start;
  while (*host_end != '\0' && *host_end != ':' && *host_end != '/' && *host_end != '?' &&
         *host_end != '#')
    host_end++;
  if (host_end == host_start) return 0;
  if (db_validate_ipv4_n(host_start, (size_t)(host_end - host_start))) return 1;
  {
    char host[256];
    size_t host_len = (size_t)(host_end - host_start);
    if (host_len >= sizeof(host)) return 0;
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';
    return db_validate_hostname_like(host, 0);
  }
}

static int db_validate_macaddr(const char *text) {
  char sep;
  int i;
  if (text == NULL || strlen(text) != 17) return 0;
  sep = text[2];
  if (sep != ':' && sep != '-') return 0;
  for (i = 0; i < 17; i++) {
    if ((i + 1) % 3 == 0) {
      if (text[i] != sep) return 0;
    } else if (!db_is_hex_char(text[i])) {
      return 0;
    }
  }
  return 1;
}

static int db_validate_semver_ident(const char *text, size_t len, int numeric_core) {
  size_t i;
  if (len == 0) return 0;
  for (i = 0; i < len; i++) {
    if (numeric_core) {
      if (!isdigit((unsigned char)text[i])) return 0;
    } else if (!(db_is_alpha_num(text[i]) || text[i] == '-')) {
      return 0;
    }
  }
  if (numeric_core && len > 1 && text[0] == '0') return 0;
  return 1;
}

static int db_validate_semver_tail_n(const char *text, size_t len) {
  size_t part = 0;
  size_t i;
  if (text == NULL || len == 0) return 0;
  for (i = 0; i <= len; i++) {
    if (i == len || text[i] == '.') {
      if (!db_validate_semver_ident(text + part, i - part, 0)) return 0;
      part = i + 1;
    }
  }
  return 1;
}

static int db_validate_semver(const char *text) {
  const char *p = text;
  int part;
  if (text == NULL) return 0;
  for (part = 0; part < 3; part++) {
    const char *start = p;
    while (isdigit((unsigned char)*p))
      p++;
    if (!db_validate_semver_ident(start, (size_t)(p - start), 1)) return 0;
    if (part < 2) {
      if (*p != '.') return 0;
      p++;
    }
  }
  if (*p == '-') {
    const char *start = ++p;
    while (*p != '\0' && *p != '+')
      p++;
    if (!db_validate_semver_tail_n(start, (size_t)(p - start))) return 0;
  }
  if (*p == '+') {
    if (!db_validate_semver_tail_n(p + 1, strlen(p + 1))) return 0;
  } else if (*p != '\0') {
    return 0;
  }
  return 1;
}

static int db_validate_hex_text(const char *text) {
  size_t i, len;
  if (text == NULL) return 0;
  len = strlen(text);
  if (len == 0) return 0;
  for (i = 0; i < len; i++)
    if (!db_is_hex_char(text[i])) return 0;
  return 1;
}

static int db_validate_base64_text(const char *text, int urlsafe) {
  size_t i, len;
  int padding = 0;
  if (text == NULL) return 0;
  len = strlen(text);
  if (len == 0 || (!urlsafe && len % 4 != 0) || (urlsafe && len % 4 == 1)) return 0;
  for (i = 0; i < len; i++) {
    char c = text[i];
    int ok = db_is_alpha_num(c) || (!urlsafe && (c == '+' || c == '/')) ||
             (urlsafe && (c == '-' || c == '_'));
    if (c == '=') {
      padding++;
      if (padding > 2) return 0;
    } else {
      if (padding > 0 || !ok) return 0;
    }
  }
  return 1;
}

static int db_validate_currency(const char *text) {
  return text != NULL && strlen(text) == 3 && text[0] >= 'A' && text[0] <= 'Z' && text[1] >= 'A' &&
         text[1] <= 'Z' && text[2] >= 'A' && text[2] <= 'Z';
}

static int db_validate_json_pointer(const char *text) {
  const char *p;
  if (text == NULL) return 0;
  if (text[0] == '\0') return 1;
  if (text[0] != '/') return 0;
  for (p = text; *p != '\0'; p++) {
    if ((unsigned char)*p < 0x20) return 0;
    if (*p == '~' && p[1] != '0' && p[1] != '1') return 0;
  }
  return 1;
}

static int db_validate_balanced_expr(const char *text, int require_dollar) {
  int paren = 0, bracket = 0;
  char quote = '\0';
  const char *p;
  if (text == NULL || text[0] == '\0') return 0;
  if (require_dollar && text[0] != '$') return 0;
  for (p = text; *p != '\0'; p++) {
    unsigned char c = (unsigned char)*p;
    if (c < 0x20 || c == 0x7f) return 0;
    if (quote != '\0') {
      if (*p == '\\' && p[1] != '\0') {
        p++;
      } else if (*p == quote) {
        quote = '\0';
      }
      continue;
    }
    if (*p == '\'' || *p == '"') {
      quote = *p;
    } else if (*p == '(') {
      paren++;
    } else if (*p == ')') {
      if (paren == 0) return 0;
      paren--;
    } else if (*p == '[') {
      bracket++;
    } else if (*p == ']') {
      if (bracket == 0) return 0;
      bracket--;
    }
  }
  return quote == '\0' && paren == 0 && bracket == 0;
}

static int db_validate_cron_field(const char *text, size_t len) {
  size_t i;
  if (text == NULL || len == 0) return 0;
  for (i = 0; i < len; i++) {
    char c = text[i];
    if (!(db_is_alpha_num(c) || c == '*' || c == '/' || c == '?' || c == ',' || c == '-' ||
          c == '.' || c == '#' || c == 'L' || c == 'W'))
      return 0;
  }
  return 1;
}

static int db_validate_cron(const char *text) {
  const char *p;
  int fields = 0;
  if (text == NULL) return 0;
  p = text;
  while (*p != '\0') {
    const char *start;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '\0') break;
    start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t')
      p++;
    if (!db_validate_cron_field(start, (size_t)(p - start))) return 0;
    fields++;
  }
  return fields == 5 || fields == 6 || fields == 7;
}

static int db_validate_color(const char *text) {
  size_t len;
  size_t i;
  if (text == NULL) return 0;
  len = strlen(text);
  if (len == 4 || len == 7 || len == 9) {
    if (text[0] != '#') return 0;
    for (i = 1; i < len; i++)
      if (!db_is_hex_char(text[i])) return 0;
    return 1;
  }
  return 0;
}

static int db_is_mime_token_char(char c) {
  return db_is_alpha_num(c) || c == '!' || c == '#' || c == '$' || c == '&' || c == '^' ||
         c == '_' || c == '.' || c == '+' || c == '-';
}

static int db_validate_mime_token(const char *text, size_t len) {
  size_t i;
  if (text == NULL || len == 0) return 0;
  for (i = 0; i < len; i++)
    if (!db_is_mime_token_char(text[i])) return 0;
  return 1;
}

static int db_validate_mime(const char *text) {
  const char *slash;
  if (text == NULL) return 0;
  slash = strchr(text, '/');
  if (slash == NULL || slash == text || slash[1] == '\0' || strchr(slash + 1, '/') != NULL)
    return 0;
  return db_validate_mime_token(text, (size_t)(slash - text)) &&
         db_validate_mime_token(slash + 1, strlen(slash + 1));
}

static int db_validate_regex(const char *text) {
  return text != NULL && re_validate_n(text, strlen(text), NULL) == RE_STATUS_OK;
}

static int db_validate_string_format(const char *format, const char *text) {
  if (format == NULL || format[0] == '\0') return 1;
  if (strcmp(format, "ipaddr") == 0 || strcmp(format, "ip") == 0) return db_validate_ipaddr(text);
  if (strcmp(format, "cidr") == 0) return db_validate_cidr(text);
  if (strcmp(format, "hostname") == 0) return db_validate_hostname_like(text, 0);
  if (strcmp(format, "domain") == 0) return db_validate_hostname_like(text, 1);
  if (strcmp(format, "email") == 0) return db_validate_email(text);
  if (strcmp(format, "url") == 0) return db_validate_uri_text(text, 1);
  if (strcmp(format, "uri") == 0) return db_validate_uri_text(text, 0);
  if (strcmp(format, "macaddr") == 0 || strcmp(format, "mac") == 0)
    return db_validate_macaddr(text);
  if (strcmp(format, "semver") == 0) return db_validate_semver(text);
  if (strcmp(format, "hex") == 0) return db_validate_hex_text(text);
  if (strcmp(format, "base64") == 0) return db_validate_base64_text(text, 0);
  if (strcmp(format, "base64url") == 0) return db_validate_base64_text(text, 1);
  if (strcmp(format, "currency") == 0) return db_validate_currency(text);
  if (strcmp(format, "json_pointer") == 0 || strcmp(format, "json-pointer") == 0)
    return db_validate_json_pointer(text);
  if (strcmp(format, "jsonpath") == 0 || strcmp(format, "json_path") == 0)
    return db_validate_balanced_expr(text, 1);
  if (strcmp(format, "xpath") == 0) return db_validate_balanced_expr(text, 0);
  if (strcmp(format, "cron") == 0) return db_validate_cron(text);
  if (strcmp(format, "color") == 0) return db_validate_color(text);
  if (strcmp(format, "mime") == 0 || strcmp(format, "mime_type") == 0)
    return db_validate_mime(text);
  if (strcmp(format, "regex") == 0) return db_validate_regex(text);
  return 1;
}

static int db_value_matches_field_format(Node *field, const DataBindValue *value) {
  const char *format = field_format(field);
  if (format == NULL || format[0] == '\0') return 1;
  if (value == NULL || value->kind != DATA_BIND_VALUE_STRING) return 0;
  return db_validate_string_format(format, value->data.string_val.ptr);
}

static int parse_positive_int(const char *text) {
  char *end = NULL;
  long value;
  if (text == NULL) return 0;
  value = strtol(text, &end, 10);
  if (end == text || value <= 0 || value > 0x7fffffffL) return 0;
  return (int)value;
}

static int parse_size_value(const char *text, size_t *out) {
  char *end = NULL;
  unsigned long long value;
  if (out != NULL) *out = 0;
  if (text == NULL || text[0] == '\0') return 0;
  value = strtoull(text, &end, 10);
  if (end == text || *end != '\0') return 0;
  if (out != NULL) *out = (size_t)value;
  return 1;
}

static Node *find_named_record(Node *schema_root, const char *list_name, const char *record_name) {
  Node *list = find_child(schema_root, list_name);
  size_t i;
  if (list == NULL || list->type != NODE_LIST || record_name == NULL) return NULL;
  for (i = 0; i < list->data.list.count; i++) {
    Node *record = list->data.list.items[i];
    const char *name = get_string_val(find_child(record, "name"));
    if (name != NULL && strcmp(name, record_name) == 0) return record;
  }
  return NULL;
}

static const type_meta_t *find_enum_meta(Node *schema_root, const char *enum_name) {
  Node *enum_node = find_named_record(schema_root, "enums", enum_name);
  const char *underlying =
      enum_node != NULL ? get_string_val(find_child(enum_node, "underlying_type")) : NULL;
  const type_meta_t *meta = find_type_meta(underlying != NULL ? underlying : "uint8");
  return meta != NULL ? meta : find_type_meta("uint8");
}

static const type_meta_t *find_scalar_meta(Node *schema_root, const char *type_name) {
  if (type_name == NULL) return NULL;
  if (find_named_record(schema_root, "enums", type_name) != NULL)
    return find_enum_meta(schema_root, type_name);
  return find_type_meta(type_name);
}

static Node *find_schema_record(Node *schema_root, const char *type_name) {
  Node *record;
  if (schema_root == NULL || type_name == NULL) return NULL;
  record = find_named_record(schema_root, "messages", type_name);
  if (record != NULL) return record;
  record = find_named_record(schema_root, "composites", type_name);
  if (record != NULL) return record;
  record = find_named_record(schema_root, "groups", type_name);
  if (record != NULL) return record;
  record = find_named_record(schema_root, "unions", type_name);
  if (record != NULL) return record;
  return find_named_record(schema_root, "enums", type_name);
}

static Node *find_data_record(Node *schema_root, const char *type_name) {
  Node *record;
  if (schema_root == NULL || type_name == NULL) return NULL;
  record = find_named_record(schema_root, "messages", type_name);
  if (record != NULL) return record;
  record = find_named_record(schema_root, "composites", type_name);
  if (record != NULL) return record;
  return find_named_record(schema_root, "groups", type_name);
}

static Node *find_union_record(Node *schema_root, const char *type_name) {
  return find_named_record(schema_root, "unions", type_name);
}

static Node *find_enum_record(Node *schema_root, const char *type_name) {
  return find_named_record(schema_root, "enums", type_name);
}

static int is_flags_type(Node *schema_root, const char *type_name) {
  Node *e = find_enum_record(schema_root, type_name);
  return e != NULL && record_flag(e, "is_flags");
}

static data_bind_text_kind_t bind_type_kind(Node *schema_root, const char *type) {
  const type_meta_t *meta;
  if (type == NULL) return DB_TEXT_UNSUPPORTED;
  if (strcmp(type, "uuid") == 0) return DB_TEXT_UUID;
  if (strcmp(type, "datetime") == 0) return DB_TEXT_DATETIME;
  if (strcmp(type, "date") == 0) return DB_TEXT_DATE;
  if (strcmp(type, "time") == 0) return DB_TEXT_TIME;
  if (strcmp(type, "duration") == 0) return DB_TEXT_DURATION;
  if (strcmp(type, "decimal") == 0) return DB_TEXT_DECIMAL;
  if (strcmp(type, "bigint") == 0) return DB_TEXT_BIGINT;
  if (strcmp(type, "money") == 0) return DB_TEXT_MONEY;
  if (strcmp(type, "bytes") == 0) return DB_TEXT_BYTES;
  if (strcmp(type, "string") == 0) return DB_TEXT_STRING;
  if (strcmp(type, "bool") == 0) return DB_TEXT_BOOL;
  meta = find_scalar_meta(schema_root, type);
  if (meta != NULL) return meta->is_float ? DB_TEXT_NUMBER : DB_TEXT_INTEGER;
  return DB_TEXT_UNSUPPORTED;
}

static data_bind_text_kind_t bind_field_kind(Node *schema_root, Node *field) {
  const char *type = get_string_val(find_child(field, "type"));
  if (type == NULL || field_flag(field, "is_collection") || field_flag(field, "is_composite_ref") ||
      field_flag(field, "is_group_field"))
    return DB_TEXT_UNSUPPORTED;
  return bind_type_kind(schema_root, type);
}

static int bind_type_supported(Node *schema_root, const char *type_name) {
  return find_data_record(schema_root, type_name) != NULL ||
         find_union_record(schema_root, type_name) != NULL ||
         bind_type_kind(schema_root, type_name) != DB_TEXT_UNSUPPORTED;
}

static int bind_field_missing_allowed(Node *field) {
  return field_flag(field, "is_optional") || field_flag(field, "has_default");
}

static Node *fields_node_for_record(Node *record);
static Node *items_node_for_enum(Node *record);

static const char *enum_item_value(Node *schema_root, const char *type_name,
                                   const char *item_name) {
  Node *e = find_enum_record(schema_root, type_name);
  Node *items = items_node_for_enum(e);
  size_t i;
  if (items == NULL || item_name == NULL) return NULL;
  for (i = 0; i < items->data.list.count; i++) {
    Node *item = items->data.list.items[i];
    const char *name = get_string_val(find_child(item, "name"));
    if (name != NULL && strcmp(name, item_name) == 0)
      return get_string_val(find_child(item, "value"));
  }
  return NULL;
}

static int enum_text_value(Node *schema_root, const char *type_name, const char *text,
                           int64_t *out) {
  const char *value_text;
  if (db_parse_i64_text(text, out)) return 1;
  value_text = enum_item_value(schema_root, type_name, text);
  return value_text != NULL ? db_parse_i64_text(value_text, out) : 0;
}

static int flags_text_value(Node *schema_root, const char *type_name, const char *text,
                            int64_t *out) {
  const char *p = text;
  int64_t acc = 0;
  int any = 0;
  if (text == NULL || out == NULL) return 0;
  if (db_parse_i64_text(text, out)) return 1;
  while (*p != '\0') {
    char token[128];
    size_t len = 0;
    int64_t value = 0;
    while (*p == ' ' || *p == '\t' || *p == '|' || *p == ',' || *p == '+')
      p++;
    if (*p == '\0') break;
    while (*p != '\0' && *p != '|' && *p != ',' && *p != '+' && *p != ' ' && *p != '\t' &&
           len + 1 < sizeof(token))
      token[len++] = *p++;
    token[len] = '\0';
    if (len == 0 || !enum_text_value(schema_root, type_name, token, &value)) return 0;
    acc |= value;
    any = 1;
  }
  if (!any) return 0;
  *out = acc;
  return 1;
}

static int schema_text_integer_value(Node *schema_root, const char *type_name,
                                     data_bind_text_kind_t kind, const char *text, int64_t *out) {
  if (is_flags_type(schema_root, type_name))
    return flags_text_value(schema_root, type_name, text, out);
  if (find_enum_record(schema_root, type_name) != NULL)
    return enum_text_value(schema_root, type_name, text, out);
  (void)kind;
  return 0;
}

static int db_parse_integer_magnitude(const char *text, size_t len, uint64_t max_value,
                                      int allow_negative, uint64_t *out, int *negative) {
  size_t pos = 0, exponent_pos = len, digits = 0, fraction_digits = 0, digit_index = 0;
  int seen_dot = 0, nonzero = 0, exponent_negative = 0;
  int64_t exponent = 0, effective_digits;
  uint64_t value = 0;
  if (text == NULL || out == NULL || negative == NULL || len == 0 || len > INT_MAX) return 0;
  *negative = 0;
  if (text[pos] == '-' || text[pos] == '+') {
    *negative = text[pos] == '-';
    if (*negative && !allow_negative) return 0;
    if (++pos == len) return 0;
  }
  for (; pos < len && text[pos] != 'e' && text[pos] != 'E'; ++pos) {
    if (text[pos] == '.') {
      if (seen_dot) return 0;
      seen_dot = 1;
      continue;
    }
    if (text[pos] < '0' || text[pos] > '9') return 0;
    nonzero |= text[pos] != '0';
    digits++;
    if (seen_dot) fraction_digits++;
  }
  if (digits == 0) return 0;
  if (pos < len) {
    int64_t cap = (int64_t)len + 64;
    exponent_pos = pos++;
    if (pos < len && (text[pos] == '-' || text[pos] == '+')) {
      exponent_negative = text[pos] == '-';
      pos++;
    }
    if (pos == len) return 0;
    for (; pos < len; ++pos) {
      int digit;
      if (text[pos] < '0' || text[pos] > '9') return 0;
      digit = text[pos] - '0';
      exponent = exponent > (cap - digit) / 10 ? cap : exponent * 10 + digit;
    }
    if (exponent_negative) exponent = -exponent;
  }
  if (!nonzero) {
    *out = 0;
    *negative = 0;
    return 1;
  }
  effective_digits = (int64_t)digits - (int64_t)fraction_digits + exponent;
  if (effective_digits <= 0) return 0;
  for (pos = (*negative || text[0] == '+') ? 1u : 0u; pos < exponent_pos; ++pos) {
    int digit;
    if (text[pos] == '.') continue;
    digit = text[pos] - '0';
    if ((int64_t)digit_index < effective_digits) {
      if (value > (max_value - (uint64_t)digit) / 10u) return 0;
      value = value * 10u + (uint64_t)digit;
    } else if (digit != 0) {
      return 0;
    }
    digit_index++;
  }
  while ((int64_t)digit_index < effective_digits) {
    if (value > max_value / 10u) return 0;
    value *= 10u;
    digit_index++;
  }
  *out = value;
  return 1;
}

static DataBindValue *dbv_integer_text(Node *schema_root, const char *type_name, const char *text,
                                       size_t len) {
  const type_meta_t *meta;
  uint64_t max_value, magnitude;
  int negative;
  int64_t named_value;
  if (text == NULL || type_name == NULL) return NULL;
  if (is_flags_type(schema_root, type_name) && flags_text_value(schema_root, type_name, text,
                                                                &named_value))
    return named_value >= INT32_MIN && named_value <= INT32_MAX ? dbv_int((int32_t)named_value)
                                                               : dbv_int64(named_value);
  if (find_enum_record(schema_root, type_name) != NULL &&
      enum_text_value(schema_root, type_name, text, &named_value))
    return named_value >= INT32_MIN && named_value <= INT32_MAX ? dbv_int((int32_t)named_value)
                                                               : dbv_int64(named_value);
  meta = find_scalar_meta(schema_root, type_name);
  if (meta == NULL || meta->is_float) return NULL;
  switch (meta->mir_type) {
  case MIR_T_U8: max_value = UINT8_MAX; break;
  case MIR_T_U16: max_value = UINT16_MAX; break;
  case MIR_T_U32: max_value = UINT32_MAX; break;
  case MIR_T_U64: max_value = UINT64_MAX; break;
  case MIR_T_I8: max_value = (uint64_t)INT8_MAX + 1u; break;
  case MIR_T_I16: max_value = (uint64_t)INT16_MAX + 1u; break;
  case MIR_T_I32: max_value = (uint64_t)INT32_MAX + 1u; break;
  case MIR_T_I64: max_value = (uint64_t)INT64_MAX + 1u; break;
  default: return NULL;
  }
  if (!db_parse_integer_magnitude(text, len, max_value, meta->mir_type == MIR_T_I8 ||
                                                             meta->mir_type == MIR_T_I16 ||
                                                             meta->mir_type == MIR_T_I32 ||
                                                             meta->mir_type == MIR_T_I64,
                                  &magnitude, &negative))
    return NULL;
  if (meta->mir_type == MIR_T_U64) return dbv_uint64(magnitude);
  if (negative) {
    int64_t signed_value = magnitude == (uint64_t)INT64_MAX + 1u
                               ? INT64_MIN
                               : -(int64_t)magnitude;
    if (meta->mir_type == MIR_T_I8 && signed_value < INT8_MIN) return NULL;
    if (meta->mir_type == MIR_T_I16 && signed_value < INT16_MIN) return NULL;
    if (meta->mir_type == MIR_T_I32 && signed_value < INT32_MIN) return NULL;
    return signed_value >= INT32_MIN ? dbv_int((int32_t)signed_value)
                                     : dbv_int64(signed_value);
  }
  if (meta->mir_type == MIR_T_I8 && magnitude > INT8_MAX) return NULL;
  if (meta->mir_type == MIR_T_I16 && magnitude > INT16_MAX) return NULL;
  if (meta->mir_type == MIR_T_I32 && magnitude > INT32_MAX) return NULL;
  if (meta->mir_type == MIR_T_I64 && magnitude > INT64_MAX) return NULL;
  return magnitude <= INT32_MAX ? dbv_int((int32_t)magnitude) : dbv_int64((int64_t)magnitude);
}

static DataBindValue *bind_text_scalar(Node *schema_root, const char *type_name,
                                       data_bind_text_kind_t kind, const char *text) {
  int64_t i64 = 0;
  double dbl = 0.0;
  int b = 0;
  if (kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (schema_text_integer_value(schema_root, type_name, kind, text, &i64)) {
    if (i64 >= INT32_MIN && i64 <= INT32_MAX) return dbv_int((int32_t)i64);
    return dbv_int64(i64);
  }
  if (kind == DB_TEXT_INTEGER)
    return dbv_integer_text(schema_root, type_name, text, text != NULL ? strlen(text) : 0u);
  switch (kind) {
  case DB_TEXT_STRING:
    return dbv_string(text != NULL ? text : "");
  case DB_TEXT_BYTES:
    return dbv_bytes((const uint8_t *)(text != NULL ? text : ""), text != NULL ? strlen(text) : 0);
  case DB_TEXT_UUID:
    return dbv_uuid_text(text);
  case DB_TEXT_DATETIME:
    return dbv_datetime_text(text);
  case DB_TEXT_DATE:
    return dbv_date_text(text);
  case DB_TEXT_TIME:
    return dbv_time_text(text);
  case DB_TEXT_DURATION:
    return dbv_duration_text(text);
  case DB_TEXT_DECIMAL:
    return dbv_decimal_text(text);
  case DB_TEXT_BIGINT:
    return dbv_bigint_text(text);
  case DB_TEXT_MONEY:
    return dbv_money_text(text);
  case DB_TEXT_BOOL:
    if (!db_parse_bool_text(text, &b)) return NULL;
    return dbv_bool(b);
  case DB_TEXT_INTEGER:
    return NULL;
  case DB_TEXT_NUMBER:
    if (!db_parse_double_text(text, &dbl)) return NULL;
    return dbv_double(dbl);
  default:
    return NULL;
  }
}

static DataBindValue *bind_field_default(Node *schema_root, Node *field) {
  const char *default_text = get_string_val(find_child(field, "default_value"));
  const char *type = get_string_val(find_child(field, "type"));
  data_bind_text_kind_t kind;
  const char *enum_value;
  if (default_text == NULL || type == NULL) return NULL;
  kind = bind_field_kind(schema_root, field);
  if (kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (kind == DB_TEXT_INTEGER) {
    if (strcmp(default_text, "true") == 0) return dbv_int(1);
    if (strcmp(default_text, "false") == 0) return dbv_int(0);
  }
  enum_value = enum_item_value(schema_root, type, default_text);
  if (enum_value != NULL) default_text = enum_value;
  return bind_text_scalar(schema_root, type, kind, default_text);
}

static int json_integer_value(Node *schema_root, const char *type_name, json_value_t *value,
                              int64_t *out) {
  DataBindValue *integer = NULL;
  const char *text = NULL;
  size_t len = 0;
  if (value == NULL || out == NULL) return 0;
  if (turbo_json_type(value) == TURBO_JSON_NUMBER) {
    text = turbo_json_number_text(value, &len);
    if (text == NULL) return 0;
  } else if (turbo_json_type(value) == TURBO_JSON_STRING) {
    text = turbo_json_string(value);
    len = turbo_json_string_len(value);
  } else {
    return 0;
  }
  integer = dbv_integer_text(schema_root, type_name, text, len);
  if (integer == NULL) return 0;
  if (integer->kind == DATA_BIND_VALUE_INT) *out = integer->data.int_val;
  else if (integer->kind == DATA_BIND_VALUE_INT64) *out = integer->data.int64_val;
  else if (integer->kind == DATA_BIND_VALUE_UINT64 && integer->data.uint64_val <= INT64_MAX)
    *out = (int64_t)integer->data.uint64_val;
  else {
    data_bind_value_free(integer);
    return 0;
  }
  data_bind_value_free(integer);
  return 1;
}

static int json_flags_value(Node *schema_root, const char *type_name, json_value_t *value,
                            int64_t *out) {
  int64_t acc = 0;
  size_t i;
  if (json_integer_value(schema_root, type_name, value, out)) return 1;
  if (value == NULL || turbo_json_type(value) != TURBO_JSON_ARRAY || out == NULL) return 0;
  for (i = 0; i < turbo_json_array_size(value); i++) {
    int64_t item = 0;
    if (!json_flags_value(schema_root, type_name, turbo_json_array_get(value, i), &item)) return 0;
    acc |= item;
  }
  *out = acc;
  return 1;
}

static DataBindValue *bind_json_value(Node *schema_root, const char *type_name,
                                      data_bind_text_kind_t kind, json_value_t *value) {
  int64_t i64 = 0;
  char number_buf[64];
  char decimal_buf[64];
  char bigint_buf[64];
  if (value == NULL || kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (is_flags_type(schema_root, type_name)) {
    if (!json_flags_value(schema_root, type_name, value, &i64)) return NULL;
    if (i64 >= INT32_MIN && i64 <= INT32_MAX) return dbv_int((int32_t)i64);
    return dbv_int64(i64);
  }
  if (kind == DB_TEXT_INTEGER && turbo_json_type(value) == TURBO_JSON_NUMBER) {
    const char *integer_text;
    size_t integer_len = 0;
    integer_text = turbo_json_number_text(value, &integer_len);
    return integer_text != NULL
               ? dbv_integer_text(schema_root, type_name, integer_text, integer_len)
               : NULL;
  }
  switch (kind) {
  case DB_TEXT_STRING:
    if (turbo_json_type(value) == TURBO_JSON_STRING) {
      return dbv_string(turbo_json_string(value));
    }
    if (turbo_json_type(value) == TURBO_JSON_NUMBER) {
      snprintf(number_buf, sizeof(number_buf), "%.17g", turbo_json_number(value));
      return dbv_string(number_buf);
    }
    if (turbo_json_type(value) == TURBO_JSON_BOOL)
      return dbv_string(turbo_json_bool(value) ? "true" : "false");
    return NULL;
  case DB_TEXT_BYTES:
    if (turbo_json_type(value) != TURBO_JSON_STRING) return NULL;
    return dbv_bytes((const uint8_t *)turbo_json_string(value), turbo_json_string_len(value));
  case DB_TEXT_UUID:
    if (turbo_json_type(value) != TURBO_JSON_STRING) return NULL;
    return dbv_uuid_text(turbo_json_string(value));
  case DB_TEXT_DATETIME:
    if (turbo_json_type(value) != TURBO_JSON_STRING) return NULL;
    return dbv_datetime_text(turbo_json_string(value));
  case DB_TEXT_DATE:
    if (turbo_json_type(value) != TURBO_JSON_STRING) return NULL;
    return dbv_date_text(turbo_json_string(value));
  case DB_TEXT_TIME:
    if (turbo_json_type(value) != TURBO_JSON_STRING) return NULL;
    return dbv_time_text(turbo_json_string(value));
  case DB_TEXT_DURATION:
    if (turbo_json_type(value) == TURBO_JSON_NUMBER)
      return dbv_duration((int64_t)turbo_json_number(value));
    if (turbo_json_type(value) != TURBO_JSON_STRING) return NULL;
    return dbv_duration_text(turbo_json_string(value));
  case DB_TEXT_DECIMAL:
    if (turbo_json_type(value) == TURBO_JSON_STRING)
      return dbv_decimal_text(turbo_json_string(value));
    if (turbo_json_type(value) == TURBO_JSON_NUMBER) {
      snprintf(decimal_buf, sizeof(decimal_buf), "%.17g", turbo_json_number(value));
      return dbv_decimal_text(decimal_buf);
    }
    return NULL;
  case DB_TEXT_BIGINT:
    if (turbo_json_type(value) == TURBO_JSON_STRING)
      return dbv_bigint_text(turbo_json_string(value));
    if (turbo_json_type(value) == TURBO_JSON_NUMBER) {
      double n = turbo_json_number(value);
      if (!isfinite(n) || floor(n) != n || n < -9007199254740991.0 || n > 9007199254740991.0)
        return NULL;
      snprintf(bigint_buf, sizeof(bigint_buf), "%.0f", n);
      return dbv_bigint_text(bigint_buf);
    }
    return NULL;
  case DB_TEXT_MONEY:
    if (turbo_json_type(value) == TURBO_JSON_STRING)
      return dbv_money_text(turbo_json_string(value));
    if (turbo_json_type(value) == TURBO_JSON_OBJECT) {
      json_value_t *amount_value = turbo_json_object_get(value, "amount");
      json_value_t *currency_value = turbo_json_object_get(value, "currency");
      DataBindMoney money;
      char amount_buf[64];
      if (amount_value == NULL || currency_value == NULL ||
          turbo_json_type(currency_value) != TURBO_JSON_STRING ||
          !db_validate_currency(turbo_json_string(currency_value)))
        return NULL;
      if (turbo_json_type(amount_value) == TURBO_JSON_STRING) {
        if (!db_parse_decimal_text(turbo_json_string(amount_value), &money.amount)) return NULL;
      } else if (turbo_json_type(amount_value) == TURBO_JSON_NUMBER) {
        snprintf(amount_buf, sizeof(amount_buf), "%.17g", turbo_json_number(amount_value));
        if (!db_parse_decimal_text(amount_buf, &money.amount)) return NULL;
      } else {
        return NULL;
      }
      memcpy(money.currency, turbo_json_string(currency_value), 3);
      money.currency[3] = '\0';
      return dbv_money(money);
    }
    return NULL;
  case DB_TEXT_BOOL:
    if (turbo_json_type(value) == TURBO_JSON_BOOL) return dbv_bool(turbo_json_bool(value));
    if (turbo_json_type(value) == TURBO_JSON_NUMBER)
      return dbv_bool(turbo_json_number(value) != 0.0);
    if (turbo_json_type(value) == TURBO_JSON_STRING)
      return bind_text_scalar(schema_root, type_name, kind, turbo_json_string(value));
    return NULL;
  case DB_TEXT_INTEGER:
    if (turbo_json_type(value) == TURBO_JSON_STRING)
      return bind_text_scalar(schema_root, type_name, kind, turbo_json_string(value));
    return NULL;
  case DB_TEXT_NUMBER:
    if (turbo_json_type(value) == TURBO_JSON_NUMBER) return dbv_double(turbo_json_number(value));
    if (turbo_json_type(value) == TURBO_JSON_BOOL)
      return dbv_double(turbo_json_bool(value) ? 1.0 : 0.0);
    if (turbo_json_type(value) == TURBO_JSON_STRING)
      return bind_text_scalar(schema_root, type_name, kind, turbo_json_string(value));
    return NULL;
  default:
    return NULL;
  }
}

static Node *union_variant(Node *union_node, const char *variant_name) {
  Node *fields = fields_node_for_record(union_node);
  size_t i;
  if (fields == NULL || variant_name == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *field = fields->data.list.items[i];
    const char *name = get_string_val(find_child(field, "name"));
    if (name != NULL && strcmp(name, variant_name) == 0) return field;
  }
  return NULL;
}

static DataBindValue *bind_json_typed_value(Node *schema_root, const char *type_name,
                                            json_value_t *value);

static DataBindValue *bind_json_array(Node *schema_root, Node *field, json_value_t *value,
                                      DataBindValueKind list_kind) {
  const char *inner_type = get_string_val(find_child(field, "inner_type"));
  data_bind_text_kind_t scalar_kind;
  DataBindValue *list;
  size_t expected = 0;
  size_t i;
  if (value == NULL || turbo_json_type(value) != TURBO_JSON_ARRAY || inner_type == NULL)
    return NULL;
  if (parse_size_value(get_string_val(find_child(field, "length_field")), &expected) &&
      turbo_json_array_size(value) != expected)
    return NULL;
  list = dbv_new(list_kind);
  if (list == NULL) return NULL;
  if (!dbv_array_reserve(&list->data.array_val, turbo_json_array_size(value))) {
    data_bind_value_free(list);
    return NULL;
  }
  scalar_kind = bind_type_kind(schema_root, inner_type);
  for (i = 0; i < turbo_json_array_size(value); i++) {
    json_value_t *item = turbo_json_array_get(value, i);
    DataBindValue *bound;
    if (field_flag(field, "collection_element_is_composite") ||
        find_data_record(schema_root, inner_type) != NULL ||
        find_union_record(schema_root, inner_type) != NULL)
      bound = bind_json_typed_value(schema_root, inner_type, item);
    else bound = bind_json_value(schema_root, inner_type, scalar_kind, item);
    if (bound == NULL || !dbv_array_push(&list->data.array_val, bound)) {
      data_bind_value_free(bound);
      data_bind_value_free(list);
      return NULL;
    }
  }
  return list;
}

static DataBindValue *bind_json_record_array(Node *schema_root, const char *type_name,
                                             json_value_t *value) {
  DataBindValue *list;
  size_t i;
  if (value == NULL || turbo_json_type(value) != TURBO_JSON_ARRAY || type_name == NULL) return NULL;
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list == NULL) return NULL;
  if (!dbv_array_reserve(&list->data.array_val, turbo_json_array_size(value))) {
    data_bind_value_free(list);
    return NULL;
  }
  for (i = 0; i < turbo_json_array_size(value); i++) {
    DataBindValue *bound =
        bind_json_typed_value(schema_root, type_name, turbo_json_array_get(value, i));
    if (bound == NULL || !dbv_array_push(&list->data.array_val, bound)) {
      data_bind_value_free(bound);
      data_bind_value_free(list);
      return NULL;
    }
  }
  return list;
}

static DataBindValue *bind_json_map(Node *schema_root, Node *field, json_value_t *value) {
  const char *value_type = get_string_val(find_child(field, "value_type"));
  data_bind_text_kind_t value_kind = bind_type_kind(schema_root, value_type);
  DataBindValue *map;
  size_t i;
  if (value == NULL || turbo_json_type(value) != TURBO_JSON_OBJECT || value_type == NULL)
    return NULL;
  map = dbv_new(DATA_BIND_VALUE_MAP);
  if (map == NULL) return NULL;
  if (!dbv_map_reserve(map, turbo_json_object_size(value))) {
    data_bind_value_free(map);
    return NULL;
  }
  for (i = 0; i < turbo_json_object_size(value); i++) {
    const char *key = turbo_json_object_key(value, i);
    json_value_t *item = turbo_json_object_value(value, i);
    DataBindValue *bound;
    if (key == NULL || item == NULL) continue;
    if (find_data_record(schema_root, value_type) != NULL ||
        find_union_record(schema_root, value_type) != NULL)
      bound = bind_json_typed_value(schema_root, value_type, item);
    else bound = bind_json_value(schema_root, value_type, value_kind, item);
    if (bound == NULL || !dbv_map_set(map, key, bound)) {
      data_bind_value_free(bound);
      data_bind_value_free(map);
      return NULL;
    }
  }
  return map;
}

static DataBindValue *bind_json_union(Node *schema_root, Node *union_node, json_value_t *object) {
  const char *variant_name;
  const char *variant_type;
  json_value_t *payload;
  Node *variant;
  DataBindValue *bound;
  DataBindValue *result;
  data_bind_text_kind_t scalar_kind;
  if (union_node == NULL || object == NULL || turbo_json_type(object) != TURBO_JSON_OBJECT ||
      turbo_json_object_size(object) != 1)
    return NULL;
  variant_name = turbo_json_object_key(object, 0);
  payload = turbo_json_object_value(object, 0);
  variant = union_variant(union_node, variant_name);
  variant_type = get_string_val(find_child(variant, "type"));
  if (variant == NULL || variant_type == NULL || payload == NULL) return NULL;
  if (find_data_record(schema_root, variant_type) != NULL ||
      find_union_record(schema_root, variant_type) != NULL)
    bound = bind_json_typed_value(schema_root, variant_type, payload);
  else {
    scalar_kind = bind_type_kind(schema_root, variant_type);
    bound = bind_json_value(schema_root, variant_type, scalar_kind, payload);
  }
  if (bound == NULL) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL || !dbv_object_set(result, variant_name, bound)) {
    data_bind_value_free(bound);
    data_bind_value_free(result);
    return NULL;
  }
  return result;
}

static DataBindValue *bind_json_object(Node *schema_root, Node *record, json_value_t *object) {
  DataBindValue *result;
  Node *fields;
  size_t i;
  if (record == NULL || object == NULL || turbo_json_type(object) != TURBO_JSON_OBJECT) return NULL;
  fields = fields_node_for_record(record);
  if (fields == NULL) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *field = fields->data.list.items[i];
    const char *name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    json_value_t *value;
    DataBindValue *bound = NULL;
    data_bind_text_kind_t kind;
    if (name == NULL) continue;
    value = turbo_json_object_get(object, name);
    if (value == NULL) {
      bound = bind_field_default(schema_root, field);
      if (bound != NULL && db_value_matches_field_format(field, bound)) {
        if (!dbv_object_set(result, name, bound)) {
          data_bind_value_free(bound);
          data_bind_value_free(result);
          return NULL;
        }
      } else if (bound != NULL) {
        data_bind_value_free(bound);
        data_bind_value_free(result);
        return NULL;
      } else if (!bind_field_missing_allowed(field)) {
        data_bind_value_free(result);
        return NULL;
      }
      continue;
    }
    if (field_flag(field, "is_group_field")) {
      bound = bind_json_record_array(schema_root, get_string_val(find_child(field, "group_type")),
                                     value);
    } else if (field_flag(field, "is_map")) {
      bound = bind_json_map(schema_root, field, value);
    } else if (field_flag(field, "is_collection")) {
      bound =
          bind_json_array(schema_root, field, value,
                          field_flag(field, "is_set") ? DATA_BIND_VALUE_SET : DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_composite_ref") && field_type != NULL) {
      bound = bind_json_typed_value(schema_root, field_type, value);
    } else if (field_type != NULL && find_union_record(schema_root, field_type) != NULL) {
      bound = bind_json_typed_value(schema_root, field_type, value);
    } else {
      kind = bind_field_kind(schema_root, field);
      bound = bind_json_value(schema_root, field_type, kind, value);
    }
    if (bound == NULL || !db_value_matches_field_format(field, bound) ||
        !dbv_object_set(result, name, bound)) {
      data_bind_value_free(bound);
      data_bind_value_free(result);
      return NULL;
    }
  }
  return result;
}

static DataBindValue *bind_json_typed_value(Node *schema_root, const char *type_name,
                                            json_value_t *value) {
  Node *record = find_data_record(schema_root, type_name);
  Node *union_node;
  data_bind_text_kind_t scalar_kind;
  if (record != NULL) return bind_json_object(schema_root, record, value);
  union_node = find_union_record(schema_root, type_name);
  if (union_node != NULL) return bind_json_union(schema_root, union_node, value);
  scalar_kind = bind_type_kind(schema_root, type_name);
  return bind_json_value(schema_root, type_name, scalar_kind, value);
}

static int xml_join_path(char *out, size_t out_size, const char *prefix, const char *name) {
  int written;
  if (out == NULL || out_size == 0 || name == NULL) return 0;
  if (prefix != NULL && prefix[0] != '\0') written = snprintf(out, out_size, "%s/%s", prefix, name);
  else written = snprintf(out, out_size, "/*/%s", name);
  return written > 0 && (size_t)written < out_size;
}

static int xml_attr_path(char *out, size_t out_size, const char *prefix, const char *name) {
  int written;
  if (out == NULL || out_size == 0 || name == NULL) return 0;
  if (prefix != NULL && prefix[0] != '\0')
    written = snprintf(out, out_size, "%s/@%s", prefix, name);
  else written = snprintf(out, out_size, "/*/@%s", name);
  return written > 0 && (size_t)written < out_size;
}

static int xml_children_path(char *out, size_t out_size, const char *prefix) {
  int written;
  if (out == NULL || out_size == 0) return 0;
  written = snprintf(out, out_size, "%s/*", prefix != NULL && prefix[0] != '\0' ? prefix : "/*");
  return written > 0 && (size_t)written < out_size;
}

static int xml_path_exists(turbo_xml_doc_t *doc, const char *path) {
  return doc != NULL && path != NULL && turbo_xml_xpath_count(doc, path) > 0;
}

static const char *xml_path_text(turbo_xml_doc_t *doc, const char *path) {
  turbo_xml_xpath_node_t *node;
  const char *text;
  if (doc == NULL || path == NULL) return NULL;
  node = turbo_xml_xpath_get(doc, path);
  if (node == NULL) return NULL;
  text = turbo_xml_xpath_node_text(node);
  if (text != NULL) return text;
  return turbo_xml_xpath_text(doc, path);
}

static int xml_field_path(turbo_xml_doc_t *doc, const char *prefix, const char *name, char *out,
                          size_t out_size) {
  char child[256], attr[256];
  if (!xml_join_path(child, sizeof(child), prefix, name)) return 0;
  if (xml_path_exists(doc, child)) {
    snprintf(out, out_size, "%s", child);
    return out[0] != '\0' && strlen(out) < out_size;
  }
  if (!xml_attr_path(attr, sizeof(attr), prefix, name)) return 0;
  if (xml_path_exists(doc, attr)) {
    snprintf(out, out_size, "%s", attr);
    return out[0] != '\0' && strlen(out) < out_size;
  }
  return 0;
}

static DataBindValue *bind_xml_typed_value(Node *schema_root, const char *type_name,
                                           turbo_xml_doc_t *doc, const char *path);

static DataBindValue *bind_xml_scalar_at_path(Node *schema_root, const char *type_name,
                                              data_bind_text_kind_t kind, turbo_xml_doc_t *doc,
                                              const char *path) {
  const char *text;
  if (doc == NULL || path == NULL || kind == DB_TEXT_UNSUPPORTED) return NULL;
  text = xml_path_text(doc, path);
  if (text == NULL) return NULL;
  return bind_text_scalar(schema_root, type_name, kind, text);
}

static DataBindValue *bind_xml_list_at_path(Node *schema_root, Node *field, turbo_xml_doc_t *doc,
                                            const char *path, DataBindValueKind list_kind) {
  const char *inner_type = get_string_val(find_child(field, "inner_type"));
  data_bind_text_kind_t scalar_kind = bind_type_kind(schema_root, inner_type);
  DataBindValue *list;
  turbo_xml_list_t nodes;
  size_t count = 0;
  int fixed_count;
  if (schema_root == NULL || field == NULL || doc == NULL || path == NULL || inner_type == NULL)
    return NULL;
  fixed_count = parse_size_value(get_string_val(find_child(field, "length_field")), &count);
  turbo_xml_xpath_query(doc, path, &nodes);
  if (fixed_count && (size_t)nodes.len != count) {
    turbo_xml_list_free(&nodes);
    return NULL;
  }
  list = dbv_new(list_kind);
  if (list == NULL) {
    turbo_xml_list_free(&nodes);
    return NULL;
  }
  for (int i = 0; i < nodes.len; i++) {
    char item_path[320];
    DataBindValue *item;
    if (snprintf(item_path, sizeof(item_path), "%s[%d]", path, i + 1) >= (int)sizeof(item_path)) {
      data_bind_value_free(list);
      turbo_xml_list_free(&nodes);
      return NULL;
    }
    if (field_flag(field, "collection_element_is_composite") ||
        find_data_record(schema_root, inner_type) != NULL ||
        find_union_record(schema_root, inner_type) != NULL)
      item = bind_xml_typed_value(schema_root, inner_type, doc, item_path);
    else item = bind_xml_scalar_at_path(schema_root, inner_type, scalar_kind, doc, item_path);
    if (item == NULL || !dbv_array_push(&list->data.array_val, item)) {
      data_bind_value_free(item);
      data_bind_value_free(list);
      turbo_xml_list_free(&nodes);
      return NULL;
    }
  }
  turbo_xml_list_free(&nodes);
  if (data_bind_value_count(list) == 0) {
    data_bind_value_free(list);
    return NULL;
  }
  return list;
}

static DataBindValue *bind_xml_map_at_path(Node *schema_root, Node *field, turbo_xml_doc_t *doc,
                                           const char *path) {
  const char *value_type = get_string_val(find_child(field, "value_type"));
  DataBindValue *map;
  turbo_xml_list_t nodes;
  char children[256];
  if (schema_root == NULL || field == NULL || doc == NULL || path == NULL || value_type == NULL)
    return NULL;
  if (!xml_children_path(children, sizeof(children), path)) return NULL;
  turbo_xml_xpath_query(doc, children, &nodes);
  map = dbv_new(DATA_BIND_VALUE_MAP);
  if (map == NULL) {
    turbo_xml_list_free(&nodes);
    return NULL;
  }
  turbo_xml_for(node, &nodes) {
    const char *key = turbo_xml_xpath_node_name((const turbo_xml_xpath_node_t *)node);
    DataBindValue *item;
    char item_path[320];
    if (key == NULL || dbv_map_has_key(map, key)) continue;
    if (!xml_join_path(item_path, sizeof(item_path), path, key)) {
      data_bind_value_free(map);
      turbo_xml_list_free(&nodes);
      return NULL;
    }
    if (find_data_record(schema_root, value_type) != NULL ||
        find_union_record(schema_root, value_type) != NULL)
      item = bind_xml_typed_value(schema_root, value_type, doc, item_path);
    else
      item = bind_xml_scalar_at_path(schema_root, value_type,
                                     bind_type_kind(schema_root, value_type), doc, item_path);
    if (item != NULL) {
      if (!dbv_map_set(map, key, item)) {
        data_bind_value_free(item);
        data_bind_value_free(map);
        turbo_xml_list_free(&nodes);
        return NULL;
      }
    } else if (!db_text_is_empty(turbo_xml_xpath_node_text((const turbo_xml_xpath_node_t *)node))) {
      data_bind_value_free(map);
      turbo_xml_list_free(&nodes);
      return NULL;
    }
  }
  turbo_xml_list_free(&nodes);
  if (data_bind_value_count(map) == 0) {
    data_bind_value_free(map);
    return NULL;
  }
  return map;
}

static DataBindValue *bind_xml_union_at_path(Node *schema_root, Node *union_node,
                                             turbo_xml_doc_t *doc, const char *path) {
  Node *fields = fields_node_for_record(union_node);
  DataBindValue *result;
  int matches = 0;
  size_t i;
  if (fields == NULL || doc == NULL || path == NULL) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *variant = fields->data.list.items[i];
    const char *name = get_string_val(find_child(variant, "name"));
    const char *variant_type = get_string_val(find_child(variant, "type"));
    data_bind_text_kind_t scalar_kind = bind_type_kind(schema_root, variant_type);
    char item_path[256];
    DataBindValue *item = NULL;
    if (name == NULL || variant_type == NULL) {
      data_bind_value_free(result);
      return NULL;
    }
    if (!xml_field_path(doc, path, name, item_path, sizeof(item_path))) continue;
    if (find_data_record(schema_root, variant_type) != NULL ||
        find_union_record(schema_root, variant_type) != NULL)
      item = bind_xml_typed_value(schema_root, variant_type, doc, item_path);
    else item = bind_xml_scalar_at_path(schema_root, variant_type, scalar_kind, doc, item_path);
    if (item == NULL || !dbv_object_set(result, name, item)) {
      data_bind_value_free(item);
      data_bind_value_free(result);
      return NULL;
    }
    matches++;
  }
  if (matches != 1) {
    data_bind_value_free(result);
    return NULL;
  }
  return result;
}

static DataBindValue *bind_xml_record_at_path(Node *schema_root, Node *record, turbo_xml_doc_t *doc,
                                              const char *path) {
  Node *fields = fields_node_for_record(record);
  DataBindValue *result;
  size_t i;
  if (fields == NULL || doc == NULL || path == NULL || !xml_path_exists(doc, path)) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *field = fields->data.list.items[i];
    const char *name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    char field_path[256];
    DataBindValue *bound = NULL;
    if (name == NULL) continue;
    if (field_flag(field, "is_group_field")) {
      if (xml_join_path(field_path, sizeof(field_path), path, name))
        bound = bind_xml_list_at_path(schema_root, field, doc, field_path, DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_map")) {
      if (xml_join_path(field_path, sizeof(field_path), path, name))
        bound = bind_xml_map_at_path(schema_root, field, doc, field_path);
    } else if (field_flag(field, "is_collection")) {
      if (xml_join_path(field_path, sizeof(field_path), path, name))
        bound = bind_xml_list_at_path(schema_root, field, doc, field_path,
                                      field_flag(field, "is_set") ? DATA_BIND_VALUE_SET
                                                                  : DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_composite_ref") ||
               find_union_record(schema_root, field_type)) {
      if (xml_join_path(field_path, sizeof(field_path), path, name))
        bound = bind_xml_typed_value(schema_root, field_type, doc, field_path);
    } else {
      if (xml_field_path(doc, path, name, field_path, sizeof(field_path)))
        bound = bind_xml_scalar_at_path(schema_root, field_type,
                                        bind_field_kind(schema_root, field), doc, field_path);
    }
    if (bound == NULL) bound = bind_field_default(schema_root, field);
    if (bound == NULL) {
      if (bind_field_missing_allowed(field)) continue;
      data_bind_value_free(result);
      return NULL;
    }
    if (!db_value_matches_field_format(field, bound) || !dbv_object_set(result, name, bound)) {
      data_bind_value_free(bound);
      data_bind_value_free(result);
      return NULL;
    }
  }
  return result;
}

static DataBindValue *bind_xml_typed_value(Node *schema_root, const char *type_name,
                                           turbo_xml_doc_t *doc, const char *path) {
  Node *record = find_data_record(schema_root, type_name);
  Node *union_node = find_union_record(schema_root, type_name);
  data_bind_text_kind_t kind = bind_type_kind(schema_root, type_name);
  if (record != NULL) return bind_xml_record_at_path(schema_root, record, doc, path);
  if (union_node != NULL) return bind_xml_union_at_path(schema_root, union_node, doc, path);
  return bind_xml_scalar_at_path(schema_root, type_name, kind, doc, path);
}

static void csv_headers_free(data_bind_csv_headers_t *headers) {
  size_t i;
  if (headers == NULL) return;
  for (i = 0; i < headers->count; i++)
    free(headers->names[i]);
  free(headers->names);
  memset(headers, 0, sizeof(*headers));
}

static int csv_headers_push(data_bind_csv_headers_t *headers, const char *text, size_t len) {
  char **items;
  char *copy;
  size_t capacity;
  if (headers == NULL || text == NULL) return 0;
  if (headers->count == headers->capacity) {
    capacity = headers->capacity == 0 ? 8 : headers->capacity * 2;
    items = (char **)realloc(headers->names, capacity * sizeof(*items));
    if (items == NULL) return 0;
    headers->names = items;
    headers->capacity = capacity;
  }
  copy = (char *)malloc(len + 1);
  if (copy == NULL) return 0;
  memcpy(copy, text, len);
  copy[len] = '\0';
  headers->names[headers->count++] = copy;
  return 1;
}

static int csv_parse_header_names(const char *csv, size_t len, data_bind_csv_headers_t *headers) {
  size_t i = 0;
  char *cell = NULL;
  size_t cell_len = 0, cell_cap = 0;
  int in_quotes = 0;
  if (csv == NULL || headers == NULL) return 0;
  memset(headers, 0, sizeof(*headers));
  while (i < len) {
    char ch = csv[i++];
    if (in_quotes) {
      if (ch == '"') {
        if (i < len && csv[i] == '"') ch = csv[i++];
        else {
          in_quotes = 0;
          continue;
        }
      }
    } else if (ch == '"') {
      in_quotes = 1;
      continue;
    } else if (ch == ',' || ch == '\n' || ch == '\r') {
      if (!csv_headers_push(headers, cell != NULL ? cell : "", cell_len)) goto fail;
      cell_len = 0;
      if (ch == '\n' || ch == '\r') {
        free(cell);
        return headers->count > 0;
      }
      continue;
    }
    if (cell_len + 1 >= cell_cap) {
      size_t next_cap = cell_cap == 0 ? 32 : cell_cap * 2;
      char *next = (char *)realloc(cell, next_cap);
      if (next == NULL) goto fail;
      cell = next;
      cell_cap = next_cap;
    }
    cell[cell_len++] = ch;
  }
  if (!csv_headers_push(headers, cell != NULL ? cell : "", cell_len)) goto fail;
  free(cell);
  return headers->count > 0;
fail:
  free(cell);
  csv_headers_free(headers);
  return 0;
}

static void csv_sanitize_path(const char *path, char *out, size_t out_size) {
  size_t w = 0;
  int last_underscore = 0;
  size_t i;
  if (out == NULL || out_size == 0) return;
  if (path == NULL) {
    out[0] = '\0';
    return;
  }
  for (i = 0; path[i] != '\0' && w + 1 < out_size; i++) {
    char ch = path[i];
    if (ch == ']' || ch == ')') continue;
    if (ch == '.' || ch == '[' || ch == '(') {
      if (w > 0 && !last_underscore) {
        out[w++] = '_';
        last_underscore = 1;
      }
      continue;
    }
    out[w++] = ch;
    last_underscore = 0;
  }
  if (w > 0 && out[w - 1] == '_') w--;
  out[w] = '\0';
}

static int csv_find_named_column(turbo_csv_doc_t *doc, const char *name, size_t *out_col) {
  char typed_name[256];
  size_t col;
  if (doc == NULL || name == NULL || out_col == NULL) return 0;
  col = turbo_csv_find_column(doc, name);
  if (col < turbo_csv_column_count(doc)) {
    *out_col = col;
    return 1;
  }
  if (snprintf(typed_name, sizeof(typed_name), "%s_n", name) < (int)sizeof(typed_name)) {
    col = turbo_csv_find_column(doc, typed_name);
    if (col < turbo_csv_column_count(doc)) {
      *out_col = col;
      return 1;
    }
  }
  if (snprintf(typed_name, sizeof(typed_name), "%s_s", name) < (int)sizeof(typed_name)) {
    col = turbo_csv_find_column(doc, typed_name);
    if (col < turbo_csv_column_count(doc)) {
      *out_col = col;
      return 1;
    }
  }
  return 0;
}

static int csv_find_path_column(turbo_csv_doc_t *doc, const char *path, size_t *out_col) {
  char sanitized[256];
  if (csv_find_named_column(doc, path, out_col)) return 1;
  csv_sanitize_path(path, sanitized, sizeof(sanitized));
  if (sanitized[0] != '\0' && strcmp(sanitized, path) != 0)
    return csv_find_named_column(doc, sanitized, out_col);
  return 0;
}

static int csv_join_path(char *out, size_t out_size, const char *prefix, const char *name) {
  int written;
  if (out == NULL || out_size == 0 || name == NULL) return 0;
  if (prefix != NULL && prefix[0] != '\0') written = snprintf(out, out_size, "%s.%s", prefix, name);
  else written = snprintf(out, out_size, "%s", name);
  return written > 0 && (size_t)written < out_size;
}

static int csv_index_path(char *out, size_t out_size, const char *prefix, size_t index) {
  int written;
  if (out == NULL || out_size == 0 || prefix == NULL) return 0;
  written = snprintf(out, out_size, "%s[%zu]", prefix, index);
  return written > 0 && (size_t)written < out_size;
}

static int index_list_push(data_bind_index_list_t *indexes, size_t value) {
  size_t *items;
  size_t i, capacity;
  if (indexes == NULL) return 0;
  for (i = 0; i < indexes->count; i++)
    if (indexes->values[i] == value) return 1;
  if (indexes->count == indexes->capacity) {
    capacity = indexes->capacity == 0 ? 8 : indexes->capacity * 2;
    items = (size_t *)realloc(indexes->values, capacity * sizeof(*items));
    if (items == NULL) return 0;
    indexes->values = items;
    indexes->capacity = capacity;
  }
  indexes->values[indexes->count++] = value;
  return 1;
}

static int index_compare(const void *a, const void *b) {
  size_t lhs = *(const size_t *)a;
  size_t rhs = *(const size_t *)b;
  return (lhs > rhs) - (lhs < rhs);
}

static int csv_header_index(const char *header, const char *path, size_t *out_index) {
  char sanitized[256];
  const char *start = NULL;
  char *end = NULL;
  unsigned long value;
  size_t path_len;
  if (header == NULL || path == NULL || out_index == NULL) return 0;
  path_len = strlen(path);
  if (strncmp(header, path, path_len) == 0 && header[path_len] == '[') {
    start = header + path_len + 1;
  } else {
    csv_sanitize_path(path, sanitized, sizeof(sanitized));
    path_len = strlen(sanitized);
    if (path_len > 0 && strncmp(header, sanitized, path_len) == 0 && header[path_len] == '_')
      start = header + path_len + 1;
  }
  if (start == NULL || !isdigit((unsigned char)start[0])) return 0;
  errno = 0;
  value = strtoul(start, &end, 10);
  if (errno != 0 || end == NULL || end == start) return 0;
  if (*end != ']' && *end != '_' && *end != '.' && *end != '\0') return 0;
  *out_index = (size_t)value;
  return 1;
}

static int csv_collect_indexes(const data_bind_csv_headers_t *headers, const char *path,
                               data_bind_index_list_t *indexes) {
  size_t i;
  if (headers == NULL || path == NULL || indexes == NULL) return 0;
  for (i = 0; i < headers->count; i++) {
    size_t index = 0;
    if (csv_header_index(headers->names[i], path, &index) && !index_list_push(indexes, index))
      return 0;
  }
  if (indexes->count > 1) qsort(indexes->values, indexes->count, sizeof(size_t), index_compare);
  return indexes->count > 0;
}

static int csv_headers_have_path(const data_bind_csv_headers_t *headers, const char *path) {
  char sanitized[256];
  size_t path_len, sanitized_len, i;
  if (headers == NULL || path == NULL) return 0;
  path_len = strlen(path);
  csv_sanitize_path(path, sanitized, sizeof(sanitized));
  sanitized_len = strlen(sanitized);
  for (i = 0; i < headers->count; i++) {
    const char *header = headers->names[i];
    if (header == NULL) continue;
    if (strcmp(header, path) == 0) return 1;
    if (path_len > 0 && strncmp(header, path, path_len) == 0 &&
        (header[path_len] == '.' || header[path_len] == '['))
      return 1;
    if (sanitized_len > 0 && strcmp(header, sanitized) == 0) return 1;
    if (sanitized_len > 0 && strncmp(header, sanitized, sanitized_len) == 0 &&
        header[sanitized_len] == '_')
      return 1;
  }
  return 0;
}

static int csv_header_has_typed_suffix(const char *suffix) {
  return suffix != NULL && suffix[0] == '_' && (suffix[1] == 'n' || suffix[1] == 's') &&
         suffix[2] == '\0';
}

static int csv_header_matches_path(const char *header, const char *path) {
  char sanitized[256];
  size_t path_len, sanitized_len;
  if (header == NULL || path == NULL) return 0;
  path_len = strlen(path);
  csv_sanitize_path(path, sanitized, sizeof(sanitized));
  sanitized_len = strlen(sanitized);
  if (strcmp(header, path) == 0) return 1;
  if (path_len > 0 && strncmp(header, path, path_len) == 0 &&
      csv_header_has_typed_suffix(header + path_len))
    return 1;
  if (path_len > 0 && strncmp(header, path, path_len) == 0 &&
      (header[path_len] == '.' || header[path_len] == '['))
    return 1;
  if (sanitized_len > 0 && strcmp(header, sanitized) == 0) return 1;
  if (sanitized_len > 0 && strncmp(header, sanitized, sanitized_len) == 0 &&
      csv_header_has_typed_suffix(header + sanitized_len))
    return 1;
  if (sanitized_len > 0 && strncmp(header, sanitized, sanitized_len) == 0 &&
      header[sanitized_len] == '_')
    return 1;
  return 0;
}

static int csv_row_has_nonempty_path(turbo_csv_doc_t *doc, size_t row,
                                     const data_bind_csv_headers_t *headers, const char *path) {
  size_t i;
  if (doc == NULL || headers == NULL || path == NULL || row >= turbo_csv_row_count(doc)) return 0;
  for (i = 0; i < headers->count; i++) {
    const char *text;
    if (!csv_header_matches_path(headers->names[i], path)) continue;
    text = turbo_csv_get(doc, row, i);
    if (!db_text_is_empty(text)) return 1;
  }
  return 0;
}

static int csv_header_map_key(const char *header, const char *path, char *key, size_t key_size) {
  size_t path_len, len;
  const char *start = NULL;
  const char *end;
  int sanitized_path = 0;
  if (header == NULL || path == NULL || key == NULL || key_size == 0) return 0;
  path_len = strlen(path);
  if (strncmp(header, path, path_len) == 0 && header[path_len] == '.')
    start = header + path_len + 1;
  else if (strncmp(header, path, path_len) == 0 && header[path_len] == '_') {
    start = header + path_len + 1;
    sanitized_path = 1;
  }
  if (start == NULL || start[0] == '\0') return 0;
  end = start;
  while (*end != '\0' && *end != '.' && *end != '[' && (!sanitized_path || *end != '_'))
    end++;
  len = (size_t)(end - start);
  if (len == 0 || len >= key_size) return 0;
  memcpy(key, start, len);
  key[len] = '\0';
  return 1;
}

static DataBindValue *bind_csv_typed_value(Node *schema_root, const char *type_name,
                                           turbo_csv_doc_t *doc, size_t row,
                                           const data_bind_csv_headers_t *headers,
                                           const char *path);

static DataBindValue *bind_csv_scalar_at_path(Node *schema_root, const char *type_name,
                                              data_bind_text_kind_t kind, turbo_csv_doc_t *doc,
                                              size_t row, const char *path) {
  size_t col = 0;
  const char *text;
  if (doc == NULL || path == NULL || kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (!csv_find_path_column(doc, path, &col)) return NULL;
  text = turbo_csv_get(doc, row, col);
  if (text == NULL) return NULL;
  return bind_text_scalar(schema_root, type_name, kind, text);
}

static DataBindValue *bind_csv_scalar_value(Node *schema_root, const char *type_name,
                                            data_bind_text_kind_t kind, turbo_csv_doc_t *doc,
                                            size_t row) {
  size_t col = 0;
  const char *text;
  if (doc == NULL || row >= turbo_csv_row_count(doc) || kind == DB_TEXT_UNSUPPORTED) return NULL;
  if (!csv_find_path_column(doc, "value", &col)) col = 0;
  text = turbo_csv_get(doc, row, col);
  if (text == NULL) return NULL;
  return bind_text_scalar(schema_root, type_name, kind, text);
}

static DataBindValue *bind_csv_map_at_path(Node *schema_root, Node *field, turbo_csv_doc_t *doc,
                                           size_t row, const data_bind_csv_headers_t *headers,
                                           const char *path) {
  const char *value_type = get_string_val(find_child(field, "value_type"));
  data_bind_text_kind_t value_kind = bind_type_kind(schema_root, value_type);
  DataBindValue *map;
  size_t i;
  if (value_type == NULL || headers == NULL || path == NULL) return NULL;
  map = dbv_new(DATA_BIND_VALUE_MAP);
  if (map == NULL) return NULL;
  for (i = 0; i < headers->count; i++) {
    char key[128], item_path[256];
    DataBindValue *item = NULL;
    if (!csv_header_map_key(headers->names[i], path, key, sizeof(key))) continue;
    if (dbv_map_has_key(map, key)) continue;
    if (!csv_join_path(item_path, sizeof(item_path), path, key)) {
      data_bind_value_free(map);
      return NULL;
    }
    if (find_data_record(schema_root, value_type) || find_union_record(schema_root, value_type))
      item = bind_csv_typed_value(schema_root, value_type, doc, row, headers, item_path);
    else item = bind_csv_scalar_at_path(schema_root, value_type, value_kind, doc, row, item_path);
    if (item != NULL) {
      if (!dbv_map_set(map, key, item)) {
        data_bind_value_free(item);
        data_bind_value_free(map);
        return NULL;
      }
    } else if (csv_row_has_nonempty_path(doc, row, headers, item_path)) {
      data_bind_value_free(map);
      return NULL;
    }
  }
  if (data_bind_value_count(map) == 0) {
    data_bind_value_free(map);
    return NULL;
  }
  return map;
}

static DataBindValue *bind_csv_list_at_path(Node *schema_root, Node *field, turbo_csv_doc_t *doc,
                                            size_t row, const data_bind_csv_headers_t *headers,
                                            const char *path, DataBindValueKind list_kind) {
  const char *inner_type = get_string_val(find_child(field, "inner_type"));
  data_bind_text_kind_t scalar_kind = bind_type_kind(schema_root, inner_type);
  data_bind_index_list_t indexes = {0};
  DataBindValue *list;
  size_t count = 0, i;
  int fixed_count;
  if (inner_type == NULL || path == NULL) return NULL;
  fixed_count = parse_size_value(get_string_val(find_child(field, "length_field")), &count);
  if (fixed_count) {
    for (i = 0; i < count; i++)
      if (!index_list_push(&indexes, i)) goto fail_indexes;
  } else if (!csv_collect_indexes(headers, path, &indexes)) {
    goto fail_indexes;
  }
  list = dbv_new(list_kind);
  if (list == NULL) goto fail_indexes;
  for (i = 0; i < indexes.count; i++) {
    char item_path[256];
    DataBindValue *item = NULL;
    if (!csv_index_path(item_path, sizeof(item_path), path, indexes.values[i])) {
      data_bind_value_free(list);
      goto fail_indexes;
    }
    if (field_flag(field, "collection_element_is_composite") ||
        find_data_record(schema_root, inner_type) || find_union_record(schema_root, inner_type))
      item = bind_csv_typed_value(schema_root, inner_type, doc, row, headers, item_path);
    else item = bind_csv_scalar_at_path(schema_root, inner_type, scalar_kind, doc, row, item_path);
    if (item != NULL) {
      if (!dbv_array_push(&list->data.array_val, item)) {
        data_bind_value_free(item);
        data_bind_value_free(list);
        goto fail_indexes;
      }
    } else if (fixed_count || csv_row_has_nonempty_path(doc, row, headers, item_path)) {
      data_bind_value_free(list);
      goto fail_indexes;
    }
  }
  free(indexes.values);
  if (data_bind_value_count(list) == 0) {
    data_bind_value_free(list);
    return NULL;
  }
  return list;
fail_indexes:
  free(indexes.values);
  return NULL;
}

static DataBindValue *bind_csv_union_at_path(Node *schema_root, Node *union_node,
                                             turbo_csv_doc_t *doc, size_t row,
                                             const data_bind_csv_headers_t *headers,
                                             const char *path) {
  Node *fields = fields_node_for_record(union_node);
  DataBindValue *result;
  int matches = 0;
  size_t i;
  if (fields == NULL || path == NULL) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *variant = fields->data.list.items[i];
    const char *name = get_string_val(find_child(variant, "name"));
    const char *variant_type = get_string_val(find_child(variant, "type"));
    char item_path[256];
    DataBindValue *item;
    if (name == NULL || variant_type == NULL ||
        !csv_join_path(item_path, sizeof(item_path), path, name)) {
      data_bind_value_free(result);
      return NULL;
    }
    if (!csv_row_has_nonempty_path(doc, row, headers, item_path)) continue;
    item = bind_csv_typed_value(schema_root, variant_type, doc, row, headers, item_path);
    if (item == NULL || !dbv_object_set(result, name, item)) {
      data_bind_value_free(item);
      data_bind_value_free(result);
      return NULL;
    }
    matches++;
  }
  if (matches != 1) {
    data_bind_value_free(result);
    return NULL;
  }
  return result;
}

static DataBindValue *bind_csv_record_at_path(Node *schema_root, Node *record, turbo_csv_doc_t *doc,
                                              size_t row, const data_bind_csv_headers_t *headers,
                                              const char *prefix) {
  Node *fields = fields_node_for_record(record);
  DataBindValue *result;
  size_t i;
  if (fields == NULL || doc == NULL || row >= turbo_csv_row_count(doc)) return NULL;
  if (prefix != NULL && prefix[0] != '\0' && !csv_headers_have_path(headers, prefix)) return NULL;
  result = dbv_new(DATA_BIND_VALUE_OBJECT);
  if (result == NULL) return NULL;
  for (i = 0; i < fields->data.list.count; i++) {
    Node *field = fields->data.list.items[i];
    const char *name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    char path[256];
    DataBindValue *bound = NULL;
    if (name == NULL || !csv_join_path(path, sizeof(path), prefix, name)) continue;
    if (field_flag(field, "is_group_field")) {
      bound =
          bind_csv_list_at_path(schema_root, field, doc, row, headers, path, DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_map")) {
      bound = bind_csv_map_at_path(schema_root, field, doc, row, headers, path);
    } else if (field_flag(field, "is_collection")) {
      bound = bind_csv_list_at_path(schema_root, field, doc, row, headers, path,
                                    field_flag(field, "is_set") ? DATA_BIND_VALUE_SET
                                                                : DATA_BIND_VALUE_LIST);
    } else if (field_flag(field, "is_composite_ref") ||
               find_union_record(schema_root, field_type)) {
      bound = bind_csv_typed_value(schema_root, field_type, doc, row, headers, path);
    } else {
      bound = bind_csv_scalar_at_path(schema_root, field_type, bind_field_kind(schema_root, field),
                                      doc, row, path);
    }
    if (bound == NULL) bound = bind_field_default(schema_root, field);
    if (bound == NULL) {
      if (bind_field_missing_allowed(field) && !csv_row_has_nonempty_path(doc, row, headers, path))
        continue;
      data_bind_value_free(result);
      return NULL;
    }
    if (!db_value_matches_field_format(field, bound) || !dbv_object_set(result, name, bound)) {
      data_bind_value_free(bound);
      data_bind_value_free(result);
      return NULL;
    }
  }
  return result;
}

static DataBindValue *bind_csv_typed_value(Node *schema_root, const char *type_name,
                                           turbo_csv_doc_t *doc, size_t row,
                                           const data_bind_csv_headers_t *headers,
                                           const char *path) {
  Node *record = find_data_record(schema_root, type_name);
  Node *union_node = find_union_record(schema_root, type_name);
  data_bind_text_kind_t kind = bind_type_kind(schema_root, type_name);
  if (record != NULL) return bind_csv_record_at_path(schema_root, record, doc, row, headers, path);
  if (union_node != NULL)
    return bind_csv_union_at_path(schema_root, union_node, doc, row, headers, path);
  if (path != NULL && path[0] != '\0')
    return bind_csv_scalar_at_path(schema_root, type_name, kind, doc, row, path);
  return bind_csv_scalar_value(schema_root, type_name, kind, doc, row);
}

static DataBindSchemaKind schema_record_kind(const char *list_name, Node *record) {
  if (record != NULL && field_flag(record, "is_flags")) return DATA_BIND_SCHEMA_FLAGS;
  if (list_name != NULL) {
    if (strcmp(list_name, "messages") == 0) return DATA_BIND_SCHEMA_MESSAGE;
    if (strcmp(list_name, "composites") == 0) return DATA_BIND_SCHEMA_COMPOSITE;
    if (strcmp(list_name, "groups") == 0) return DATA_BIND_SCHEMA_GROUP;
    if (strcmp(list_name, "enums") == 0) return DATA_BIND_SCHEMA_ENUM;
    if (strcmp(list_name, "unions") == 0) return DATA_BIND_SCHEMA_UNION;
  }
  if (field_flag(record, "is_message_decl")) return DATA_BIND_SCHEMA_MESSAGE;
  if (field_flag(record, "is_composite_decl")) return DATA_BIND_SCHEMA_COMPOSITE;
  if (field_flag(record, "is_group_decl")) return DATA_BIND_SCHEMA_GROUP;
  if (field_flag(record, "is_union_decl")) return DATA_BIND_SCHEMA_UNION;
  return DATA_BIND_SCHEMA_UNKNOWN;
}

static Node *fields_node_for_record(Node *record) {
  Node *fields = find_child(record, "fields");
  return fields != NULL && fields->type == NODE_LIST ? fields : NULL;
}

static Node *items_node_for_enum(Node *record) {
  Node *items = find_child(record, "items");
  return items != NULL && items->type == NODE_LIST ? items : NULL;
}

static size_t db_reflect_out_size(size_t requested, size_t full_size) {
  return requested != 0 && requested < full_size ? requested : full_size;
}

static int db_reflect_has_field(size_t out_size, size_t offset, size_t field_size) {
  return offset <= out_size && field_size <= out_size - offset;
}

static void db_reflect_clear(void *out, size_t requested, size_t full_size) {
  size_t out_size;
  if (out == NULL) return;
  out_size = db_reflect_out_size(requested, full_size);
  memset(out, 0, out_size);
  if (out_size >= sizeof(size_t)) *(size_t *)out = out_size;
}

#define DB_REFLECT_SET(type, out, out_size, field, value)                                          \
  do {                                                                                             \
    if (db_reflect_has_field((out_size), offsetof(type, field), sizeof((out)->field)))             \
      (out)->field = (value);                                                                      \
  } while (0)

static int fill_schema_type(Node *record, const char *list_name, DataBindSchemaType *out) {
  Node *fields;
  Node *items;
  size_t out_size;
  const char *name;
  if (record == NULL || out == NULL) return 0;
  out_size = db_reflect_out_size(out->size, sizeof(*out));
  memset(out, 0, out_size);
  name = get_string_val(find_child(record, "name"));
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, size, out_size);
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, name, name);
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, kind, schema_record_kind(list_name, record));
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, underlying_type,
                 get_string_val(find_child(record, "underlying_type")));
  fields = fields_node_for_record(record);
  items = items_node_for_enum(record);
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, field_count,
                 fields != NULL ? fields->data.list.count : 0);
  DB_REFLECT_SET(DataBindSchemaType, out, out_size, item_count,
                 items != NULL ? items->data.list.count : 0);
  if (db_reflect_has_field(out_size, offsetof(DataBindSchemaType, fixed_block_size),
                           sizeof(out->fixed_block_size)) &&
      db_reflect_has_field(out_size, offsetof(DataBindSchemaType, has_fixed_block_size),
                           sizeof(out->has_fixed_block_size))) {
    out->has_fixed_block_size = parse_size_value(
        get_string_val(find_child(record, "fixed_block_size")), &out->fixed_block_size);
  }
  return name != NULL;
}

static const char *schema_field_kind(Node *schema_root, Node *field) {
  const char *field_type = get_string_val(find_child(field, "type"));
  if (field_flag(field, "is_group_field")) return "group";
  if (field_flag(field, "is_map")) return "map";
  if (field_flag(field, "is_set")) return "set";
  if (field_flag(field, "is_list")) return "list";
  if (field_flag(field, "is_collection")) return "array";
  if (field_flag(field, "is_enum_ref") ||
      (field_type != NULL && find_named_record(schema_root, "enums", field_type) != NULL))
    return "enum";
  if (field_type != NULL && find_named_record(schema_root, "unions", field_type) != NULL)
    return "union";
  if (field_flag(field, "is_composite_ref")) return "composite";
  if (field_flag(field, "is_string")) return "string";
  if (field_flag(field, "is_bytes")) return "bytes";
  if (field_flag(field, "is_numeric") || find_type_meta(field_type) != NULL) return "scalar";
  return field_type != NULL ? "custom" : "unknown";
}

static int fill_schema_field(Node *schema_root, Node *field, DataBindSchemaField *out) {
  size_t out_size;
  const char *name;
  if (schema_root == NULL || field == NULL || out == NULL) return 0;
  out_size = db_reflect_out_size(out->size, sizeof(*out));
  memset(out, 0, out_size);
  name = get_string_val(find_child(field, "name"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, size, out_size);
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, name, name);
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, type,
                 get_string_val(find_child(field, "type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, kind, schema_field_kind(schema_root, field));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, inner_type,
                 get_string_val(find_child(field, "inner_type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, group_type,
                 get_string_val(find_child(field, "group_type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, key_type,
                 get_string_val(find_child(field, "key_type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, value_type,
                 get_string_val(find_child(field, "value_type")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, collection_kind,
                 get_string_val(find_child(field, "collection_kind")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, length,
                 get_string_val(find_child(field, "length_field")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_optional, field_flag(field, "is_optional"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, has_default, field_flag(field, "has_default"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, default_value,
                 get_string_val(find_child(field, "default_value")));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_collection,
                 field_flag(field, "is_collection"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_composite,
                 field_flag(field, "is_composite_ref"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_group, field_flag(field, "is_group_field"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_map, field_flag(field, "is_map"));
  if (db_reflect_has_field(out_size, offsetof(DataBindSchemaField, is_enum),
                           sizeof(out->is_enum))) {
    const char *type = get_string_val(find_child(field, "type"));
    out->is_enum = field_flag(field, "is_enum_ref") ||
                   (type != NULL && find_named_record(schema_root, "enums", type) != NULL);
  }
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_variable_size,
                 field_flag(field, "is_variable_size"));
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, is_fixed_size,
                 field_flag(field, "is_fixed_size"));
  if (db_reflect_has_field(out_size, offsetof(DataBindSchemaField, offset), sizeof(out->offset)) &&
      db_reflect_has_field(out_size, offsetof(DataBindSchemaField, has_offset),
                           sizeof(out->has_offset))) {
    out->has_offset = parse_size_value(get_string_val(find_child(field, "offset")), &out->offset);
  }
  if (db_reflect_has_field(out_size, offsetof(DataBindSchemaField, size_bytes),
                           sizeof(out->size_bytes)) &&
      db_reflect_has_field(out_size, offsetof(DataBindSchemaField, has_size_bytes),
                           sizeof(out->has_size_bytes))) {
    out->has_size_bytes =
        parse_size_value(get_string_val(find_child(field, "size_bytes")), &out->size_bytes);
  }
  if (db_reflect_has_field(out_size, offsetof(DataBindSchemaField, field_size_bytes),
                           sizeof(out->field_size_bytes)) &&
      db_reflect_has_field(out_size, offsetof(DataBindSchemaField, has_field_size_bytes),
                           sizeof(out->has_field_size_bytes))) {
    out->has_field_size_bytes = parse_size_value(
        get_string_val(find_child(field, "field_size_bytes")), &out->field_size_bytes);
  }
  DB_REFLECT_SET(DataBindSchemaField, out, out_size, format, field_format(field));
  return name != NULL;
}

static int emit_field_array_push(emit_field_array_t *fields, emit_field_t field) {
  emit_field_t *new_items;
  size_t new_capacity;
  if (fields->count == fields->capacity) {
    new_capacity = fields->capacity == 0 ? 8 : fields->capacity * 2;
    new_items = (emit_field_t *)realloc(fields->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) return 0;
    fields->items = new_items;
    fields->capacity = new_capacity;
  }
  fields->items[fields->count++] = field;
  return 1;
}

static void emit_field_array_free(emit_field_array_t *fields) {
  size_t i;
  if (fields == NULL) return;
  for (i = 0; i < fields->count; i++) {
    free(fields->items[i].name);
    emit_field_array_free(&fields->items[i].children);
  }
  free(fields->items);
  fields->items = NULL;
  fields->count = 0;
  fields->capacity = 0;
}

static int append_emit_field(emit_field_array_t *fields, const char *name, emit_kind_t kind,
                             const type_meta_t *meta, int size, int has_set_bytes,
                             size_t fixed_count, int group_dim, emit_field_array_t *children) {
  emit_field_t field;
  size_t name_len = strlen(name);
  memset(&field, 0, sizeof(field));
  field.name = (char *)malloc(name_len + 1);
  if (field.name == NULL) return 0;
  memcpy(field.name, name, name_len + 1);
  field.kind = kind;
  field.size = size;
  field.mir_type = meta != NULL ? meta->mir_type : MIR_T_UNDEF;
  field.is_float = meta != NULL ? meta->is_float : 0;
  field.is_64 = meta != NULL ? meta->is_64 : 0;
  field.has_set_bytes = (unsigned char)has_set_bytes;
  field.fixed_count = fixed_count;
  field.group_dim = group_dim;
  if (children != NULL) {
    field.children = *children;
    memset(children, 0, sizeof(*children));
  }
  if (!emit_field_array_push(fields, field)) {
    free(field.name);
    emit_field_array_free(&field.children);
    return 0;
  }
  return 1;
}

static int build_fields(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                         const char *prefix, int has_set_bytes);
static int build_record_fields_v1(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                                  const char *prefix, int has_set_bytes);

/* Schema validation limits to prevent malicious schemas */
#define MAX_FIELD_NESTING_DEPTH 32
#define MAX_FIELD_OFFSET_BYTES (1024 * 1024 * 1024) /* 1GB */
#define MAX_TOTAL_FIELDS 10000

typedef struct schema_validation_context {
  int nesting_depth;
  size_t total_fields;
  size_t max_offset;
  char visited_types[256][128]; /* Track visited types to detect cycles */
  size_t visited_count;
  char error[256];
} schema_validation_context_t;

static int schema_validation_type_visited(schema_validation_context_t *ctx, const char *type_name) {
  size_t i;
  if (type_name == NULL) return 0;
  for (i = 0; i < ctx->visited_count; i++) {
    if (strcmp(ctx->visited_types[i], type_name) == 0) return 1;
  }
  return 0;
}

static int schema_validation_mark_visited(schema_validation_context_t *ctx, const char *type_name) {
  if (type_name == NULL) return 0;
  if (ctx->visited_count >= 256) {
    snprintf(ctx->error, sizeof(ctx->error), "Too many nested types (max 256)");
    return 0;
  }
  snprintf(ctx->visited_types[ctx->visited_count], 128, "%s", type_name);
  ctx->visited_count++;
  return 1;
}

static void schema_validation_unmark_visited(schema_validation_context_t *ctx) {
  if (ctx->visited_count > 0) ctx->visited_count--;
}

static int validate_field_offset_safe(schema_validation_context_t *ctx, size_t offset,
                                      size_t size) {
  if (offset > MAX_FIELD_OFFSET_BYTES) {
    snprintf(ctx->error, sizeof(ctx->error), "Field offset %zu exceeds maximum %d bytes", offset,
             MAX_FIELD_OFFSET_BYTES);
    return 0;
  }
  if (size > 0 && offset + size > MAX_FIELD_OFFSET_BYTES) {
    snprintf(ctx->error, sizeof(ctx->error), "Field range [%zu, %zu) exceeds maximum", offset,
             offset + size);
    return 0;
  }
  if (offset + size > ctx->max_offset) {
    ctx->max_offset = offset + size;
  }
  return 1;
}

static int validate_schema_fields(schema_validation_context_t *ctx, Node *src_fields,
                                  Node *schema_root, const char *parent_type);

static int validate_composite_or_group_type(schema_validation_context_t *ctx, Node *schema_root,
                                            const char *type_name, const char *list_name) {
  Node *record;
  int saved_depth;
  int result;

  if (type_name == NULL) return 1;

  /* Check for circular reference */
  if (schema_validation_type_visited(ctx, type_name)) {
    snprintf(ctx->error, sizeof(ctx->error), "Circular type reference detected: %s", type_name);
    return 0;
  }

  record = find_named_record(schema_root, list_name, type_name);
  if (record == NULL) return 1; /* Not found is not a validation error */

  /* Check nesting depth */
  if (ctx->nesting_depth >= MAX_FIELD_NESTING_DEPTH) {
    snprintf(ctx->error, sizeof(ctx->error), "Field nesting depth exceeds maximum %d (in type %s)",
             MAX_FIELD_NESTING_DEPTH, type_name);
    return 0;
  }

  /* Mark as visited and recurse */
  if (!schema_validation_mark_visited(ctx, type_name)) return 0;

  saved_depth = ctx->nesting_depth;
  ctx->nesting_depth++;
  result = validate_schema_fields(ctx, find_child(record, "fields"), schema_root, type_name);
  ctx->nesting_depth = saved_depth;

  schema_validation_unmark_visited(ctx);
  return result;
}

static int validate_schema_fields(schema_validation_context_t *ctx, Node *src_fields,
                                  Node *schema_root, const char *parent_type) {
  size_t i;
  if (src_fields == NULL || src_fields->type != NODE_LIST) return 1;

  for (i = 0; i < src_fields->data.list.count; i++) {
    Node *field = src_fields->data.list.items[i];
    const char *field_name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    Node *offset_node = find_child(field, "offset");
    Node *size_node = find_child(field, "size");

    if (field_name == NULL) continue;

    /* Count total fields */
    ctx->total_fields++;
    if (ctx->total_fields > MAX_TOTAL_FIELDS) {
      snprintf(ctx->error, sizeof(ctx->error), "Total field count exceeds maximum %d",
               MAX_TOTAL_FIELDS);
      return 0;
    }

    /* Validate offset if present */
    if (offset_node != NULL && offset_node->type == NODE_STRING) {
      const char *offset_str = get_string_val(offset_node);
      const char *size_str = size_node != NULL ? get_string_val(size_node) : NULL;
      if (offset_str != NULL) {
        int offset_int = parse_positive_int(offset_str);
        int size_int = size_str != NULL ? parse_positive_int(size_str) : 0;
        if (offset_int >= 0 && size_int >= 0) {
          if (!validate_field_offset_safe(ctx, (size_t)offset_int, (size_t)size_int)) return 0;
        }
      }
    }

    /* Validate composite references */
    if (field_flag(field, "is_composite_ref")) {
      if (!validate_composite_or_group_type(ctx, schema_root, field_type, "composites")) return 0;
      continue;
    }

    /* Validate group references */
    if (field_flag(field, "is_group_field")) {
      const char *group_type = get_string_val(find_child(field, "group_type"));
      if (!validate_composite_or_group_type(ctx, schema_root, group_type, "groups")) return 0;
      continue;
    }

    /* Validate collection inner types */
    if (field_flag(field, "is_collection")) {
      const char *inner_type = get_string_val(find_child(field, "inner_type"));
      if (inner_type != NULL) {
        if (!validate_composite_or_group_type(ctx, schema_root, inner_type, "composites")) return 0;
      }
    }
  }
  return 1;
}

static void build_full_field_name(const char *prefix, const char *field_name, char *out,
                                  size_t out_size) {
  if (prefix != NULL && prefix[0] != '\0') snprintf(out, out_size, "%s.%s", prefix, field_name);
  else snprintf(out, out_size, "%s", field_name);
}

static int build_composite_emit_fields(emit_field_array_t *fields, Node *field, Node *schema_root,
                                        const char *full_name, int has_set_bytes,
                                        int record_mode) {
  const char *field_type = get_string_val(find_child(field, "type"));
  Node *composite = find_named_record(schema_root, "composites", field_type);
  if (composite == NULL) return 1;
  if (record_mode) {
    emit_field_array_t child_fields = {0};
    if (!build_record_fields_v1(&child_fields, find_child(composite, "fields"), schema_root, NULL,
                                has_set_bytes)) {
      emit_field_array_free(&child_fields);
      return 0;
    }
    if (!append_emit_field(fields, full_name, EF_OBJECT, NULL, 0, 0, 0, 0, &child_fields)) {
      emit_field_array_free(&child_fields);
      return 0;
    }
    return 1;
  }
  return build_fields(fields, find_child(composite, "fields"), schema_root, full_name,
                      has_set_bytes);
}

static int build_group_emit_field(emit_field_array_t *fields, Node *field, Node *schema_root,
                                   const char *full_name, int has_set_bytes, int record_mode) {
  emit_field_array_t child_fields = {0};
  Node *group =
      find_named_record(schema_root, "groups", get_string_val(find_child(field, "group_type")));
  int entry_size =
      group != NULL ? parse_positive_int(get_string_val(find_child(group, "fixed_block_size"))) : 0;
  int group_dim = parse_positive_int(get_string_val(find_child(field, "group_dimension_size")));

  if (group == NULL || entry_size <= 0) return 1;
  if (group_dim <= 0) group_dim = 4;

  if (!(record_mode ? build_record_fields_v1(&child_fields, find_child(group, "fields"),
                                             schema_root, NULL, has_set_bytes)
                    : build_fields(&child_fields, find_child(group, "fields"), schema_root, NULL,
                                   has_set_bytes))) {
    emit_field_array_free(&child_fields);
    return 0;
  }
  if (!append_emit_field(fields, full_name, EF_GROUP, NULL, entry_size, 0, 0, group_dim,
                         &child_fields)) {
    emit_field_array_free(&child_fields);
    return 0;
  }
  return 1;
}

static int build_map_collection_emit_field(emit_field_array_t *fields, Node *field,
                                           Node *schema_root, const char *full_name) {
  const char *key_type = get_string_val(find_child(field, "key_type"));
  const char *value_type = get_string_val(find_child(field, "value_type"));
  const type_meta_t *meta = NULL;

  if (key_type == NULL || value_type == NULL) return 1;
  if (strcmp(key_type, "string") != 0) return 1;
  if (strcmp(value_type, "string") == 0)
    return append_emit_field(fields, full_name, EF_MAP_STR_STR, NULL, 0, 0, 0, 0, NULL);
  if (strcmp(value_type, "bool") == 0)
    return append_emit_field(fields, full_name, EF_MAP_STR_BOOL, find_type_meta("bool"), 1, 0, 0, 0,
                             NULL);

  meta = find_scalar_meta(schema_root, value_type);
  if (meta == NULL) return 1;
  if (meta->is_float)
    return append_emit_field(fields, full_name, EF_MAP_STR_DBL, meta, meta->size, 0, 0, 0, NULL);
  return append_emit_field(
      fields, full_name,
      meta->mir_type == MIR_T_U64
          ? EF_MAP_STR_U64
          : (meta->mir_type == MIR_T_U32 ? EF_MAP_STR_U32
                                         : (meta->is_64 ? EF_MAP_STR_I64 : EF_MAP_STR_INT)),
      meta, meta->size, 0, 0, 0, NULL);
}

static int build_list_or_set_collection_emit_field(emit_field_array_t *fields, Node *field,
                                                    Node *schema_root, const char *full_name,
                                                    const char *collection_kind,
                                                    const char *inner_type, int count,
                                                    int record_mode) {
  const type_meta_t *meta = NULL;

  if (strcmp(collection_kind, "list") != 0 && strcmp(collection_kind, "set") != 0 &&
      strcmp(collection_kind, "array") != 0)
    return 1;
  if (inner_type == NULL) return 1;
  if (field_flag(field, "is_fixed_size") && count <= 0) return 1;

  if (strcmp(collection_kind, "set") != 0) {
    Node *composite = find_named_record(schema_root, "composites", inner_type);
    if (composite != NULL) {
      emit_field_array_t child_fields = {0};
      int element_size =
          parse_positive_int(get_string_val(find_child(composite, "fixed_block_size")));
      if (element_size <= 0) return 1;
      if (!(record_mode ? build_record_fields_v1(&child_fields, find_child(composite, "fields"),
                                                 schema_root, NULL, 0)
                        : build_fields(&child_fields, find_child(composite, "fields"), schema_root,
                                       NULL, 0))) {
        emit_field_array_free(&child_fields);
        return 0;
      }
      if (!append_emit_field(fields, full_name, EF_LIST_OBJ, NULL, element_size, 0,
                             field_flag(field, "is_fixed_size") ? (size_t)count : 0, 0,
                             &child_fields)) {
        emit_field_array_free(&child_fields);
        return 0;
      }
      return 1;
    }
  }

  if (strcmp(inner_type, "string") == 0) {
    emit_kind_t kind = strcmp(collection_kind, "set") == 0 ? EF_SET_STR : EF_LIST_STR;
    return append_emit_field(fields, full_name, kind, NULL, 0, 0,
                             field_flag(field, "is_fixed_size") ? (size_t)count : 0, 0, NULL);
  }

  meta = find_scalar_meta(schema_root, inner_type);
  if (meta == NULL) return 1;
  if (strcmp(collection_kind, "set") == 0) {
    emit_kind_t kind;
    if (meta->is_float) kind = EF_SET_DBL;
    else if (strcmp(inner_type, "bool") == 0) kind = EF_SET_BOOL;
    else if (meta->mir_type == MIR_T_U64) kind = EF_SET_U64;
    else if (meta->mir_type == MIR_T_U32) kind = EF_SET_U32;
    else if (meta->is_64) kind = EF_SET_I64;
    else kind = EF_SET_INT;
    return append_emit_field(fields, full_name, kind, meta, meta->size, 0, 0, 0, NULL);
  }

  return append_emit_field(
      fields, full_name,
      strcmp(inner_type, "bool") == 0
          ? EF_LIST_BOOL
          : (meta->is_float
                 ? EF_LIST_DBL
                 : (meta->mir_type == MIR_T_U64 ? EF_LIST_U64
                                                : (meta->mir_type == MIR_T_U32
                                                       ? EF_LIST_U32
                                                       : (meta->is_64 ? EF_LIST_I64
                                                                      : EF_LIST_INT)))),
      meta, meta->size, 0, field_flag(field, "is_fixed_size") ? (size_t)count : 0, 0, NULL);
}

static int build_collection_emit_field(emit_field_array_t *fields, Node *field, Node *schema_root,
                                        const char *full_name, int record_mode) {
  const char *collection_kind = get_string_val(find_child(field, "collection_kind"));
  const char *field_type = get_string_val(find_child(field, "type"));
  const char *inner_type = get_string_val(find_child(field, "inner_type"));
  int count = parse_positive_int(get_string_val(find_child(field, "length_field")));

  if (collection_kind == NULL) collection_kind = field_type;
  if (strcmp(collection_kind, "map") == 0)
    return build_map_collection_emit_field(fields, field, schema_root, full_name);

  return build_list_or_set_collection_emit_field(fields, field, schema_root, full_name,
                                                  collection_kind, inner_type, count, record_mode);
}

static int build_var_or_scalar_emit_field(emit_field_array_t *fields, Node *field,
                                          Node *schema_root, const char *full_name,
                                          const char *field_type, int has_set_bytes) {
  if (field_flag(field, "is_var_data")) {
    emit_kind_t kind = field_flag(field, "is_bytes") ? EF_VAR_BYTES : EF_STR;
    return append_emit_field(fields, full_name, kind, NULL, 0, has_set_bytes, 0, 0, NULL);
  }

  if (field_flag(field, "is_bytes")) {
    int size = parse_positive_int(get_string_val(find_child(field, "size_bytes")));
    if (size <= 0) return 1;
    return append_emit_field(fields, full_name, EF_FIX_BYTES, NULL, size, has_set_bytes, 0, 0,
                             NULL);
  }

  if (field_flag(field, "is_uuid") || strcmp(field_type, "uuid") == 0) {
    return append_emit_field(fields, full_name, EF_UUID, NULL, 16, 0, 0, 0, NULL);
  }

  if (field_flag(field, "is_enum_ref")) {
    const type_meta_t *meta = find_enum_meta(schema_root, field_type);
    if (meta == NULL) return 1;
    return append_emit_field(fields, full_name,
                             meta->mir_type == MIR_T_U64 ? EF_U64
                                                        : (meta->mir_type == MIR_T_U32
                                                               ? EF_U32
                                                               : (meta->is_64 ? EF_I64 : EF_INT)),
                             meta, meta->size, 0, 0, 0, NULL);
  }

  {
    const type_meta_t *meta = find_type_meta(field_type);
    if (meta == NULL) return 1;
    return append_emit_field(fields, full_name,
                             strcmp(field_type, "bool") == 0
                                 ? EF_BOOL
                                 : (meta->is_float
                                        ? EF_DBL
                                        : (meta->mir_type == MIR_T_U64
                                               ? EF_U64
                                               : (meta->mir_type == MIR_T_U32
                                                      ? EF_U32
                                                      : (meta->is_64 ? EF_I64 : EF_INT)))),
                             meta, meta->size, 0, 0, 0, NULL);
  }
}

static int build_fields_mode_v1(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                                const char *prefix, int has_set_bytes, int record_mode) {
  size_t i;
  if (src_fields == NULL || src_fields->type != NODE_LIST) return 1;
  for (i = 0; i < src_fields->data.list.count; i++) {
    Node *field = src_fields->data.list.items[i];
    const char *field_name = get_string_val(find_child(field, "name"));
    const char *field_type = get_string_val(find_child(field, "type"));
    char full_name[512];
    if (field_name == NULL || field_type == NULL) continue;
    build_full_field_name(prefix, field_name, full_name, sizeof(full_name));

    if (field_flag(field, "is_composite_ref")) {
      if (!build_composite_emit_fields(fields, field, schema_root, full_name, has_set_bytes,
                                       record_mode))
        return 0;
      continue;
    }

    if (field_flag(field, "is_group_field")) {
      if (!build_group_emit_field(fields, field, schema_root, full_name, has_set_bytes, record_mode))
        return 0;
      continue;
    }

    if (field_flag(field, "is_collection")) {
      if (!build_collection_emit_field(fields, field, schema_root, full_name, record_mode)) return 0;
      continue;
    }

    if (!build_var_or_scalar_emit_field(fields, field, schema_root, full_name, field_type,
                                        has_set_bytes))
      return 0;
  }
  return 1;
}

static int build_fields(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                        const char *prefix, int has_set_bytes) {
  return build_fields_mode_v1(fields, src_fields, schema_root, prefix, has_set_bytes, 0);
}

static int build_record_fields_v1(emit_field_array_t *fields, Node *src_fields, Node *schema_root,
                                  const char *prefix, int has_set_bytes) {
  return build_fields_mode_v1(fields, src_fields, schema_root, prefix, has_set_bytes, 1);
}

static void record_layout_free_v1(data_bind_record_layout_t *layout) {
  size_t i;
  if (layout == NULL) return;
  for (i = 0; i < layout->count; ++i) {
    free(layout->fields[i].name);
    record_layout_free_v1(layout->fields[i].child);
  }
  free(layout->fields);
  free(layout);
}

static data_bind_record_layout_t *record_layout_from_emit_fields_v1(
    const emit_field_array_t *fields) {
  data_bind_record_layout_t *layout;
  size_t i;
  if (fields == NULL) return NULL;
  layout = (data_bind_record_layout_t *)calloc(1, sizeof(*layout));
  if (layout == NULL) return NULL;
  layout->count = fields->count;
  if (layout->count == 0) return layout;
  layout->fields = (data_bind_record_layout_field_t *)calloc(layout->count,
                                                              sizeof(*layout->fields));
  if (layout->fields == NULL) {
    free(layout);
    return NULL;
  }
  for (i = 0; i < layout->count; ++i) {
    const emit_field_t *source = &fields->items[i];
    size_t name_len = strlen(source->name);
    layout->fields[i].name = (char *)malloc(name_len + 1);
    if (layout->fields[i].name == NULL) {
      record_layout_free_v1(layout);
      return NULL;
    }
    memcpy(layout->fields[i].name, source->name, name_len + 1);
    if (source->children.count > 0) {
      layout->fields[i].child = record_layout_from_emit_fields_v1(&source->children);
      if (layout->fields[i].child == NULL) {
        record_layout_free_v1(layout);
        return NULL;
      }
    }
  }
  return layout;
}

static void data_bind_record_plans_free_v1(DataBind *codec) {
  data_bind_record_plan_t *plan;
  if (codec == NULL) return;
  plan = codec->record_plans;
  while (plan != NULL) {
    data_bind_record_plan_t *next = plan->next;
    free(plan->type_name);
    record_layout_free_v1(plan->layout);
    free(plan);
    plan = next;
  }
  codec->record_plans = NULL;
}

static data_bind_record_plan_t *data_bind_record_plan_find_v1(const DataBind *codec,
                                                               const char *type_name) {
  data_bind_record_plan_t *plan;
  if (codec == NULL || type_name == NULL) return NULL;
  for (plan = codec->record_plans; plan != NULL; plan = plan->next)
    if (strcmp(plan->type_name, type_name) == 0) return plan;
  return NULL;
}

static int data_bind_record_plans_build_v1(DataBind *codec) {
  Node *messages_node;
  size_t i;
  if (codec == NULL) return 0;
  messages_node = find_child(codec->schema_root, "messages");
  if (messages_node == NULL || messages_node->type != NODE_LIST) return 1;
  for (i = 0; i < messages_node->data.list.count; ++i) {
    Node *message = messages_node->data.list.items[i];
    const char *name = get_string_val(find_child(message, "name"));
    emit_field_array_t fields = {0};
    data_bind_record_plan_t *plan;
    if (name == NULL) continue;
    if (!build_record_fields_v1(&fields, find_child(message, "fields"), codec->schema_root, NULL,
                                codec->api.set_field_bytes != NULL)) {
      emit_field_array_free(&fields);
      return 0;
    }
    plan = (data_bind_record_plan_t *)calloc(1, sizeof(*plan));
    if (plan != NULL) {
      plan->type_name = dbv_strdup(name);
      plan->layout = record_layout_from_emit_fields_v1(&fields);
    }
    emit_field_array_free(&fields);
    if (plan == NULL || plan->type_name == NULL || plan->layout == NULL) {
      if (plan != NULL) {
        free(plan->type_name);
        record_layout_free_v1(plan->layout);
        free(plan);
      }
      return 0;
    }
    plan->next = codec->record_plans;
    codec->record_plans = plan;
  }
  return 1;
}

typedef struct data_bind_binary_writer {
  uint8_t *data;
  size_t capacity;
  size_t offset;
  DataBindError *error;
} data_bind_binary_writer_t;

static const DataBindValue *db_binary_object_get_n(const DataBindValue *object, const char *name,
                                                   size_t name_len) {
  size_t i;
  if (object == NULL || object->kind != DATA_BIND_VALUE_OBJECT || name == NULL) return NULL;
  for (i = 0; i < object->data.object_val.count; ++i) {
    const data_bind_value_field_t *field = &object->data.object_val.items[i];
    if (strlen(field->name) == name_len && memcmp(field->name, name, name_len) == 0)
      return field->value;
  }
  return NULL;
}

static const DataBindValue *db_binary_value_at_path(const DataBindValue *object, const char *path) {
  const DataBindValue *value;
  const char *segment;
  const char *dot;
  if (object == NULL || path == NULL) return NULL;
  value = db_binary_object_get_n(object, path, strlen(path));
  if (value != NULL) return value;
  value = object;
  segment = path;
  while (segment[0] != '\0') {
    dot = strchr(segment, '.');
    value = db_binary_object_get_n(value, segment,
                                   dot != NULL ? (size_t)(dot - segment) : strlen(segment));
    if (value == NULL || dot == NULL) return value;
    segment = dot + 1;
  }
  return NULL;
}

static DataBindStatus db_binary_writer_reserve(data_bind_binary_writer_t *writer, size_t size,
                                               const char *path, uint8_t **out) {
  size_t start;
  if (writer == NULL || size > SIZE_MAX - writer->offset)
    return db_error_set(writer != NULL ? writer->error : NULL, DATA_BIND_ERR_RUNTIME, path, -1, -1,
                        "Binary output size overflow");
  start = writer->offset;
  writer->offset += size;
  if (out != NULL) *out = writer->data != NULL ? writer->data + start : NULL;
  if (writer->data != NULL && writer->offset > writer->capacity)
    return db_error_set(writer->error, DATA_BIND_ERR_INVALID_ARG, path, -1, -1,
                        "Binary output buffer is too small");
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_bytes(data_bind_binary_writer_t *writer, const void *data,
                                            size_t size, const char *path) {
  uint8_t *dst = NULL;
  DataBindStatus status;
  if (size != 0 && data == NULL)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, path, -1, -1,
                        "Binary value has no data");
  status = db_binary_writer_reserve(writer, size, path, &dst);
  if (status == DATA_BIND_OK && dst != NULL && size != 0) memcpy(dst, data, size);
  return status;
}

static DataBindStatus db_binary_write_zeros(data_bind_binary_writer_t *writer, size_t size,
                                            const char *path) {
  uint8_t *dst = NULL;
  DataBindStatus status = db_binary_writer_reserve(writer, size, path, &dst);
  if (status == DATA_BIND_OK && dst != NULL && size != 0) memset(dst, 0, size);
  return status;
}

static DataBindStatus db_binary_write_u16(data_bind_binary_writer_t *writer, uint16_t value,
                                          const char *path) {
  uint8_t *dst = NULL;
  DataBindStatus status = db_binary_writer_reserve(writer, sizeof(value), path, &dst);
  if (status == DATA_BIND_OK && dst != NULL) tbe_wire_write_u16(dst, 0, value);
  return status;
}

static DataBindStatus db_binary_write_u32(data_bind_binary_writer_t *writer, uint32_t value,
                                          const char *path) {
  uint8_t *dst = NULL;
  DataBindStatus status = db_binary_writer_reserve(writer, sizeof(value), path, &dst);
  if (status == DATA_BIND_OK && dst != NULL) tbe_wire_write_u32(dst, 0, value);
  return status;
}

static int db_binary_integer_value(const DataBindValue *value, int64_t *out) {
  if (value == NULL || out == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_INT) {
    *out = value->data.int_val;
    return 1;
  }
  if (value->kind == DATA_BIND_VALUE_INT64) {
    *out = value->data.int64_val;
    return 1;
  }
  return 0;
}

static int db_binary_integer_fits(MIR_type_t type, int64_t value) {
  switch (type) {
  case MIR_T_U8:
    return value >= 0 && (uint64_t)value <= UINT8_MAX;
  case MIR_T_I8:
    return value >= INT8_MIN && value <= INT8_MAX;
  case MIR_T_U16:
    return value >= 0 && (uint64_t)value <= UINT16_MAX;
  case MIR_T_I16:
    return value >= INT16_MIN && value <= INT16_MAX;
  case MIR_T_U32:
    return value >= 0 && (uint64_t)value <= UINT32_MAX;
  case MIR_T_I32:
    return value >= INT32_MIN && value <= INT32_MAX;
  case MIR_T_U64:
    return value >= 0;
  case MIR_T_I64:
    return 1;
  default:
    return 0;
  }
}

static DataBindStatus db_binary_write_integer(data_bind_binary_writer_t *writer, MIR_type_t type,
                                              int size, const DataBindValue *value,
                                              const char *path) {
  uint8_t *dst = NULL;
  int64_t integer;
  DataBindStatus status;
  if (type == MIR_T_U64 && value != NULL && value->kind == DATA_BIND_VALUE_UINT64) {
    status = db_binary_writer_reserve(writer, (size_t)size, path, &dst);
    if (status == DATA_BIND_OK && dst != NULL)
      tbe_wire_write_u64(dst, 0, value->data.uint64_val);
    return status;
  }
  if (!db_binary_integer_value(value, &integer) || !db_binary_integer_fits(type, integer))
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, path, -1, -1,
                        "Integer value does not fit the schema wire type");
  status = db_binary_writer_reserve(writer, (size_t)size, path, &dst);
  if (status != DATA_BIND_OK || dst == NULL) return status;
  switch (type) {
  case MIR_T_U8:
    tbe_wire_write_u8(dst, 0, (uint8_t)integer);
    break;
  case MIR_T_I8:
    tbe_wire_write_i8(dst, 0, (int8_t)integer);
    break;
  case MIR_T_U16:
    tbe_wire_write_u16(dst, 0, (uint16_t)integer);
    break;
  case MIR_T_I16:
    tbe_wire_write_i16(dst, 0, (int16_t)integer);
    break;
  case MIR_T_U32:
    tbe_wire_write_u32(dst, 0, (uint32_t)integer);
    break;
  case MIR_T_I32:
    tbe_wire_write_i32(dst, 0, (int32_t)integer);
    break;
  case MIR_T_U64:
    tbe_wire_write_u64(dst, 0, (uint64_t)integer);
    break;
  case MIR_T_I64:
    tbe_wire_write_i64(dst, 0, integer);
    break;
  default:
    return db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, path, -1, -1,
                        "Unsupported integer wire type");
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_number(data_bind_binary_writer_t *writer,
                                             const emit_field_t *field,
                                             const DataBindValue *value) {
  uint8_t *dst = NULL;
  double number;
  DataBindStatus status;
  if (value == NULL || value->kind != DATA_BIND_VALUE_DOUBLE || !isfinite(value->data.double_val))
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Floating-point field requires a finite number");
  number = value->data.double_val;
  if (field->mir_type == MIR_T_F && (number < -FLT_MAX || number > FLT_MAX))
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Floating-point value does not fit float32");
  status = db_binary_writer_reserve(writer, (size_t)field->size, field->name, &dst);
  if (status != DATA_BIND_OK || dst == NULL) return status;
  if (field->mir_type == MIR_T_F) tbe_wire_write_f32(dst, 0, (float)number);
  else tbe_wire_write_f64(dst, 0, number);
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_var_data(data_bind_binary_writer_t *writer, const void *data,
                                               size_t size, const char *path) {
  DataBindStatus status;
  if (size > UINT32_MAX)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, path, -1, -1,
                        "Variable binary value exceeds uint32 length");
  status = db_binary_write_u32(writer, (uint32_t)size, path);
  return status == DATA_BIND_OK ? db_binary_write_bytes(writer, data, size, path) : status;
}

static DataBindStatus db_binary_write_fields(data_bind_binary_writer_t *writer,
                                             const emit_field_array_t *fields,
                                             const DataBindValue *object);

static DataBindStatus db_binary_write_scalar(data_bind_binary_writer_t *writer,
                                             const emit_field_t *field,
                                             const DataBindValue *value) {
  if (value == NULL)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Required binary field is missing");
  switch (field->kind) {
  case EF_INT:
  case EF_U32:
  case EF_I64:
  case EF_U64:
    return db_binary_write_integer(writer, field->mir_type, field->size, value, field->name);
  case EF_BOOL: {
    uint8_t boolean;
    if (value->kind != DATA_BIND_VALUE_BOOL)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Boolean field has the wrong value type");
    boolean = value->data.bool_val != 0;
    return db_binary_write_bytes(writer, &boolean, sizeof(boolean), field->name);
  }
  case EF_DBL:
    return db_binary_write_number(writer, field, value);
  case EF_UUID:
    if (value->kind != DATA_BIND_VALUE_UUID)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "UUID field has the wrong value type");
    return db_binary_write_bytes(writer, value->data.uuid_val.bytes, TURBO_UUID_SIZE, field->name);
  case EF_FIX_BYTES:
    if (value->kind != DATA_BIND_VALUE_BYTES || value->data.bytes_val.len != (size_t)field->size)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Fixed bytes field length does not match the schema");
    return db_binary_write_bytes(writer, value->data.bytes_val.ptr, value->data.bytes_val.len,
                                 field->name);
  case EF_STR:
    if (value->kind != DATA_BIND_VALUE_STRING)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "String field has the wrong value type");
    return db_binary_write_var_data(writer, value->data.string_val.ptr,
                                    strlen(value->data.string_val.ptr), field->name);
  case EF_VAR_BYTES:
    if (value->kind != DATA_BIND_VALUE_BYTES)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Bytes field has the wrong value type");
    return db_binary_write_var_data(writer, value->data.bytes_val.ptr, value->data.bytes_val.len,
                                    field->name);
  default:
    return db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                        "Unsupported scalar binary field");
  }
}

static int db_binary_is_list_kind(emit_kind_t kind) {
  return kind >= EF_LIST_INT && kind <= EF_LIST_OBJ;
}

static int db_binary_is_set_kind(emit_kind_t kind) {
  return kind >= EF_SET_INT && kind <= EF_SET_STR;
}

static DataBindStatus db_binary_write_collection_item(data_bind_binary_writer_t *writer,
                                                      const emit_field_t *field,
                                                      const DataBindValue *item) {
  emit_field_t scalar = *field;
  switch (field->kind) {
  case EF_LIST_INT:
  case EF_SET_INT:
    scalar.kind = EF_INT;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_U32:
  case EF_SET_U32:
    scalar.kind = EF_U32;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_I64:
  case EF_SET_I64:
    scalar.kind = EF_I64;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_U64:
  case EF_SET_U64:
    scalar.kind = EF_U64;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_DBL:
  case EF_SET_DBL:
    scalar.kind = EF_DBL;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_BOOL:
  case EF_SET_BOOL:
    scalar.kind = EF_BOOL;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_STR:
  case EF_SET_STR:
    scalar.kind = EF_STR;
    return db_binary_write_scalar(writer, &scalar, item);
  case EF_LIST_OBJ:
    if (item == NULL || item->kind != DATA_BIND_VALUE_OBJECT)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Composite collection item has the wrong value type");
    return db_binary_write_fields(writer, &field->children, item);
  default:
    return db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                        "Unsupported binary collection item");
  }
}

static DataBindStatus db_binary_write_collection(data_bind_binary_writer_t *writer,
                                                 const emit_field_t *field,
                                                 const DataBindValue *value) {
  size_t i;
  size_t count;
  DataBindStatus status;
  DataBindValueKind expected =
      db_binary_is_set_kind(field->kind) ? DATA_BIND_VALUE_SET : DATA_BIND_VALUE_LIST;
  if (value == NULL || value->kind != expected)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Collection field has the wrong value type");
  count = value->data.array_val.count;
  if (field->fixed_count != 0) {
    if (count != field->fixed_count)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Fixed collection length does not match the schema");
  } else {
    if (count > UINT32_MAX)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Collection exceeds uint32 item count");
    status = db_binary_write_u32(writer, (uint32_t)count, field->name);
    if (status != DATA_BIND_OK) return status;
  }
  for (i = 0; i < count; ++i) {
    status = db_binary_write_collection_item(writer, field, value->data.array_val.items[i]);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_map(data_bind_binary_writer_t *writer,
                                          const emit_field_t *field, const DataBindValue *value) {
  size_t i;
  DataBindStatus status;
  emit_field_t scalar = *field;
  if (value == NULL || value->kind != DATA_BIND_VALUE_MAP || value->data.map_val.count > UINT32_MAX)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Map field has the wrong value type or too many entries");
  status = db_binary_write_u32(writer, (uint32_t)value->data.map_val.count, field->name);
  if (status != DATA_BIND_OK) return status;
  for (i = 0; i < value->data.map_val.count; ++i) {
    const data_bind_value_map_entry_t *entry = &value->data.map_val.items[i];
    status = db_binary_write_var_data(writer, entry->key, strlen(entry->key), field->name);
    if (status != DATA_BIND_OK) return status;
    if (field->kind == EF_MAP_STR_STR) scalar.kind = EF_STR;
    else if (field->kind == EF_MAP_STR_INT) scalar.kind = EF_INT;
    else if (field->kind == EF_MAP_STR_U32) scalar.kind = EF_U32;
    else if (field->kind == EF_MAP_STR_I64) scalar.kind = EF_I64;
    else if (field->kind == EF_MAP_STR_U64) scalar.kind = EF_U64;
    else if (field->kind == EF_MAP_STR_DBL) scalar.kind = EF_DBL;
    else scalar.kind = EF_BOOL;
    status = db_binary_write_scalar(writer, &scalar, entry->value);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_group(data_bind_binary_writer_t *writer,
                                            const emit_field_t *field, const DataBindValue *value) {
  size_t i;
  DataBindStatus status;
  if (value == NULL || value->kind != DATA_BIND_VALUE_LIST || field->group_dim < 4 ||
      field->size <= 0 || field->size > UINT16_MAX || value->data.array_val.count > UINT16_MAX)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                        "Group value does not fit the schema dimensions");
  status = db_binary_write_u16(writer, (uint16_t)field->size, field->name);
  if (status == DATA_BIND_OK)
    status = db_binary_write_u16(writer, (uint16_t)value->data.array_val.count, field->name);
  if (status == DATA_BIND_OK && field->group_dim > 4)
    status = db_binary_write_zeros(writer, (size_t)field->group_dim - 4u, field->name);
  if (status != DATA_BIND_OK) return status;
  for (i = 0; i < value->data.array_val.count; ++i) {
    const DataBindValue *entry = value->data.array_val.items[i];
    size_t start = writer->offset;
    if (entry == NULL || entry->kind != DATA_BIND_VALUE_OBJECT)
      return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, field->name, -1, -1,
                          "Group entry has the wrong value type");
    status = db_binary_write_fields(writer, &field->children, entry);
    if (status != DATA_BIND_OK) return status;
    if (writer->offset - start > (size_t)field->size)
      return db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                          "Group fields exceed the declared block length");
    status =
        db_binary_write_zeros(writer, (size_t)field->size - (writer->offset - start), field->name);
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_write_fields(data_bind_binary_writer_t *writer,
                                             const emit_field_array_t *fields,
                                             const DataBindValue *object) {
  size_t i;
  if (object == NULL || object->kind != DATA_BIND_VALUE_OBJECT)
    return db_error_set(writer->error, DATA_BIND_ERR_TYPE_MISMATCH, "binary", -1, -1,
                        "Binary root value must be an object");
  for (i = 0; i < fields->count; ++i) {
    const emit_field_t *field = &fields->items[i];
    const DataBindValue *value = db_binary_value_at_path(object, field->name);
    DataBindStatus status;
    if (field->kind <= EF_VAR_BYTES) status = db_binary_write_scalar(writer, field, value);
    else if (db_binary_is_list_kind(field->kind) || db_binary_is_set_kind(field->kind))
      status = db_binary_write_collection(writer, field, value);
    else if (field->kind >= EF_MAP_STR_STR && field->kind <= EF_MAP_STR_BOOL)
      status = db_binary_write_map(writer, field, value);
    else if (field->kind == EF_GROUP) status = db_binary_write_group(writer, field, value);
    else
      status = db_error_set(writer->error, DATA_BIND_ERR_SCHEMA, field->name, -1, -1,
                            "Unsupported binary field kind");
    if (status != DATA_BIND_OK) return status;
  }
  return DATA_BIND_OK;
}

static int db_binary_field_supported(Node *schema_root, Node *field) {
  const char *type = get_string_val(find_child(field, "type"));
  if (type == NULL || field_flag(field, "is_optional")) return 0;
  if (field_flag(field, "is_composite_ref")) {
    Node *record = find_named_record(schema_root, "composites", type);
    Node *children = record != NULL ? find_child(record, "fields") : NULL;
    size_t i;
    if (children == NULL || children->type != NODE_LIST) return 0;
    for (i = 0; i < children->data.list.count; ++i)
      if (!db_binary_field_supported(schema_root, children->data.list.items[i])) return 0;
    return 1;
  }
  if (field_flag(field, "is_group_field")) {
    Node *record =
        find_named_record(schema_root, "groups", get_string_val(find_child(field, "group_type")));
    Node *children = record != NULL ? find_child(record, "fields") : NULL;
    size_t i;
    if (children == NULL || children->type != NODE_LIST ||
        parse_positive_int(get_string_val(find_child(record, "fixed_block_size"))) <= 0)
      return 0;
    for (i = 0; i < children->data.list.count; ++i)
      if (!db_binary_field_supported(schema_root, children->data.list.items[i])) return 0;
    return 1;
  }
  if (field_flag(field, "is_collection")) {
    const char *kind = get_string_val(find_child(field, "collection_kind"));
    const char *inner = get_string_val(find_child(field, "inner_type"));
    const char *key = get_string_val(find_child(field, "key_type"));
    const char *mapped = get_string_val(find_child(field, "value_type"));
    const type_meta_t *meta;
    if (kind == NULL) kind = type;
    if (strcmp(kind, "map") == 0) {
      if (key == NULL || mapped == NULL || strcmp(key, "string") != 0) return 0;
      if (strcmp(mapped, "string") == 0 || strcmp(mapped, "bool") == 0) return 1;
      meta = find_scalar_meta(schema_root, mapped);
      return meta != NULL;
    }
    if (inner == NULL ||
        (strcmp(kind, "list") != 0 && strcmp(kind, "set") != 0 && strcmp(kind, "array") != 0))
      return 0;
    if (strcmp(kind, "set") != 0) {
      Node *record = find_named_record(schema_root, "composites", inner);
      if (record != NULL) {
        Node *children = find_child(record, "fields");
        size_t i;
        if (children == NULL || children->type != NODE_LIST) return 0;
        for (i = 0; i < children->data.list.count; ++i)
          if (!db_binary_field_supported(schema_root, children->data.list.items[i])) return 0;
        return 1;
      }
    }
    if (strcmp(inner, "string") == 0) return 1;
    meta = find_scalar_meta(schema_root, inner);
    return meta != NULL;
  }
  if (field_flag(field, "is_var_data"))
    return field_flag(field, "is_string") || field_flag(field, "is_bytes");
  if (field_flag(field, "is_bytes"))
    return parse_positive_int(get_string_val(find_child(field, "size_bytes"))) > 0;
  if (field_flag(field, "is_uuid") || strcmp(type, "uuid") == 0) return 1;
  if (field_flag(field, "is_enum_ref")) return find_enum_meta(schema_root, type) != NULL;
  return find_type_meta(type) != NULL;
}

static DataBindStatus db_binary_plan(DataBind *codec, const DataBindObject *object,
                                     emit_field_array_t *fields, DataBindError *error) {
  Node *message;
  Node *schema_fields;
  const char *byte_order;
  size_t i;
  if (codec == NULL || object == NULL || object->type_name == NULL || object->value == NULL ||
      fields == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "binary", -1, -1,
                        "Invalid binary serialize arguments");
  message = find_named_record(codec->schema_root, "messages", object->type_name);
  if (message == NULL)
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, object->type_name, -1, -1,
                        "Binary schema message was not found");
  byte_order = get_string_val(find_child(codec->schema_root, "wire_byte_order"));
  if (byte_order != NULL && strcmp(byte_order, "little") != 0)
    return db_error_set(error, DATA_BIND_ERR_SCHEMA, object->type_name, -1, -1,
                        "Dynamic binary codec currently requires little-endian schema order");
  schema_fields = find_child(message, "fields");
  if (schema_fields == NULL || schema_fields->type != NODE_LIST)
    return db_error_set(error, DATA_BIND_ERR_SCHEMA, object->type_name, -1, -1,
                        "Binary schema message has no field list");
  for (i = 0; i < schema_fields->data.list.count; ++i) {
    Node *field = schema_fields->data.list.items[i];
    if (!db_binary_field_supported(codec->schema_root, field))
      return db_error_set(error, DATA_BIND_ERR_SCHEMA, get_string_val(find_child(field, "name")),
                          -1, -1, "Schema field has no supported dynamic binary representation");
  }
  if (!build_fields(fields, schema_fields, codec->schema_root, NULL,
                    codec->api.set_field_bytes != NULL))
    return db_error_set(error, DATA_BIND_ERR_OOM, object->type_name, -1, -1,
                        "Out of memory building binary serialization plan");
  return DATA_BIND_OK;
}

static DataBindStatus db_binary_measure(const emit_field_array_t *fields,
                                        const DataBindValue *value, size_t *out_len,
                                        DataBindError *error) {
  data_bind_binary_writer_t writer = {NULL, 0, 0, error};
  DataBindStatus status = db_binary_write_fields(&writer, fields, value);
  if (out_len != NULL) *out_len = status == DATA_BIND_OK ? writer.offset : 0;
  return status;
}

static int validate_fields_api(DataBind *codec, const char *message_name,
                               const emit_field_array_t *fields) {
  size_t i;
  for (i = 0; i < fields->count; i++) {
    const emit_field_t *field = &fields->items[i];
    if ((field->kind == EF_LIST_INT &&
         (codec->api.create_list == NULL || codec->api.set_field_list == NULL ||
          codec->api.add_list_item_int == NULL)) ||
        (field->kind == EF_LIST_U32 &&
         (codec->api.create_list == NULL || codec->api.set_field_list == NULL ||
          codec->api.add_list_item_uint32 == NULL)) ||
        (field->kind == EF_LIST_I64 &&
         (codec->api.create_list == NULL || codec->api.set_field_list == NULL ||
          codec->api.add_list_item_int64 == NULL)) ||
        (field->kind == EF_LIST_U64 &&
         (codec->api.create_list == NULL || codec->api.set_field_list == NULL ||
          codec->api.add_list_item_uint64 == NULL)) ||
        (field->kind == EF_LIST_DBL &&
         (codec->api.create_list == NULL || codec->api.set_field_list == NULL ||
          codec->api.add_list_item_double == NULL)) ||
        (field->kind == EF_LIST_BOOL &&
         (codec->api.create_list == NULL || codec->api.set_field_list == NULL ||
          codec->api.add_list_item_bool == NULL)) ||
        (field->kind == EF_LIST_STR &&
         (codec->api.create_list == NULL || codec->api.set_field_list == NULL ||
          codec->api.add_list_item_string == NULL)) ||
        ((field->kind == EF_LIST_OBJ || field->kind == EF_GROUP) &&
         (codec->api.create_list == NULL || codec->api.set_field_list == NULL ||
          codec->api.add_list_item_object == NULL)) ||
        (field->kind == EF_SET_INT &&
         (codec->api.create_set == NULL || codec->api.set_field_set == NULL ||
          codec->api.add_set_item_int == NULL)) ||
        (field->kind == EF_SET_U32 &&
         (codec->api.create_set == NULL || codec->api.set_field_set == NULL ||
          codec->api.add_set_item_uint32 == NULL)) ||
        (field->kind == EF_SET_I64 &&
         (codec->api.create_set == NULL || codec->api.set_field_set == NULL ||
          codec->api.add_set_item_int64 == NULL)) ||
        (field->kind == EF_SET_U64 &&
         (codec->api.create_set == NULL || codec->api.set_field_set == NULL ||
          codec->api.add_set_item_uint64 == NULL)) ||
        (field->kind == EF_SET_DBL &&
         (codec->api.create_set == NULL || codec->api.set_field_set == NULL ||
          codec->api.add_set_item_double == NULL)) ||
        (field->kind == EF_SET_BOOL &&
         (codec->api.create_set == NULL || codec->api.set_field_set == NULL ||
          codec->api.add_set_item_bool == NULL)) ||
        (field->kind == EF_SET_STR &&
         (codec->api.create_set == NULL || codec->api.set_field_set == NULL ||
          codec->api.add_set_item_string == NULL)) ||
        (field->kind == EF_MAP_STR_STR &&
         (codec->api.create_map == NULL || codec->api.set_field_map == NULL ||
          codec->api.add_map_entry_string_string == NULL)) ||
        (field->kind == EF_MAP_STR_INT &&
         (codec->api.create_map == NULL || codec->api.set_field_map == NULL ||
          codec->api.add_map_entry_string_int == NULL)) ||
        (field->kind == EF_MAP_STR_U32 &&
         (codec->api.create_map == NULL || codec->api.set_field_map == NULL ||
          codec->api.add_map_entry_string_uint32 == NULL)) ||
        (field->kind == EF_MAP_STR_I64 &&
         (codec->api.create_map == NULL || codec->api.set_field_map == NULL ||
          codec->api.add_map_entry_string_int64 == NULL)) ||
        (field->kind == EF_MAP_STR_U64 &&
         (codec->api.create_map == NULL || codec->api.set_field_map == NULL ||
          codec->api.add_map_entry_string_uint64 == NULL)) ||
        (field->kind == EF_MAP_STR_DBL &&
         (codec->api.create_map == NULL || codec->api.set_field_map == NULL ||
          codec->api.add_map_entry_string_double == NULL)) ||
        (field->kind == EF_MAP_STR_BOOL &&
         (codec->api.create_map == NULL || codec->api.set_field_map == NULL ||
          codec->api.add_map_entry_string_bool == NULL))) {
      return set_codec_error(codec, "Schema field '%s.%s' requires container API callbacks",
                             message_name, field->name);
    }
    if (field->kind == EF_UUID && codec->api.set_field_uuid == NULL) {
      return set_codec_error(codec, "Schema field '%s.%s' requires uuid API callback", message_name,
                             field->name);
    }
    if (field->kind == EF_U32 && codec->api.set_field_uint32 == NULL) {
      return set_codec_error(codec, "Schema field '%s.%s' requires uint32 API callback",
                             message_name, field->name);
    }
    if (field->kind == EF_U64 && codec->api.set_field_uint64 == NULL) {
      return set_codec_error(codec, "Schema field '%s.%s' requires uint64 API callback",
                             message_name, field->name);
    }
    if (field->children.count > 0 && !validate_fields_api(codec, message_name, &field->children))
      return 0;
  }
  return 1;
}

static char *builder_make_name(mir_builder_t *builder, const char *prefix, size_t id) {
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%s_%zu", prefix, id);
  return codec_strdup(builder->codec, buffer);
}

static MIR_item_t builder_make_string_data(mir_builder_t *builder, const char *value) {
  char *item_name = builder_make_name(builder, "__db_str", builder->data_name_id++);
  char *bytes = NULL;
  size_t len;
  if (item_name == NULL) return NULL;
  len = strlen(value);
  bytes = codec_strdup_n(builder->codec, value, len);
  if (bytes == NULL) return NULL;
  return MIR_new_data(builder->ctx, item_name, MIR_T_U8, len + 1, bytes);
}

static void declare_external(mir_builder_t *builder, external_ref_t *ref, const char *name,
                             size_t nres, MIR_type_t *res_types, size_t nargs, MIR_var_t *args) {
  char proto_name[128];
  ref->import_item = MIR_new_import(builder->ctx, name);
  snprintf(proto_name, sizeof(proto_name), "p_%s", name);
  ref->proto_item = MIR_new_proto_arr(builder->ctx, codec_strdup(builder->codec, proto_name), nres,
                                      res_types, nargs, args);
}

static void init_externals(mir_builder_t *builder) {
  MIR_type_t ptr_result = MIR_T_P;
  MIR_type_t status_result = MIR_T_I32;
  MIR_var_t set_int_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_I32, "value", 0}};
  MIR_var_t set_u32_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_U32, "value", 0}};
  MIR_var_t set_i64_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_I64, "value", 0}};
  MIR_var_t set_u64_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_U64, "value", 0}};
  MIR_var_t set_dbl_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_D, "value", 0}};
  MIR_var_t set_bool_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_I32, "value", 0}};
  MIR_var_t set_str_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_P, "value", 0}};
  MIR_var_t set_bytes_args[] = {
      {MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_P, "data", 0}, {MIR_T_I64, "len", 0}};
  MIR_var_t set_uuid_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_P, "data", 0}};
  MIR_var_t list_i32_args[] = {{MIR_T_P, "list", 0}, {MIR_T_I32, "value", 0}};
  MIR_var_t list_u32_args[] = {{MIR_T_P, "list", 0}, {MIR_T_U32, "value", 0}};
  MIR_var_t list_i64_args[] = {{MIR_T_P, "list", 0}, {MIR_T_I64, "value", 0}};
  MIR_var_t list_u64_args[] = {{MIR_T_P, "list", 0}, {MIR_T_U64, "value", 0}};
  MIR_var_t list_dbl_args[] = {{MIR_T_P, "list", 0}, {MIR_T_D, "value", 0}};
  MIR_var_t list_bool_args[] = {{MIR_T_P, "list", 0}, {MIR_T_I32, "value", 0}};
  MIR_var_t list_str_args[] = {{MIR_T_P, "list", 0}, {MIR_T_P, "value", 0}};
  MIR_var_t list_obj_args[] = {{MIR_T_P, "list", 0}, {MIR_T_P, "value", 0}};
  MIR_var_t set_list_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_P, "list", 0}};
  MIR_var_t map_str_str_args[] = {{MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_P, "value", 0}};
  MIR_var_t map_str_int_args[] = {
      {MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_I32, "value", 0}};
  MIR_var_t map_str_u32_args[] = {
      {MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_U32, "value", 0}};
  MIR_var_t map_str_i64_args[] = {
      {MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_I64, "value", 0}};
  MIR_var_t map_str_u64_args[] = {
      {MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_U64, "value", 0}};
  MIR_var_t map_str_dbl_args[] = {{MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_D, "value", 0}};
  MIR_var_t map_str_bool_args[] = {
      {MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_I32, "value", 0}};
  MIR_var_t read_varstr_args[] = {
      {MIR_T_P, "buf", 0}, {MIR_T_I64, "offset", 0}, {MIR_T_I64, "remaining", 0}};
  MIR_var_t free_args[] = {{MIR_T_P, "ptr", 0}};
  MIR_var_t create_record_child_args[] = {{MIR_T_P, "container", 0}};
  MIR_var_t create_record_field_args[] = {
      {MIR_T_P, "obj", 0}, {MIR_T_U32, "slot", 0}};
  MIR_var_t set_slot_i32_args[] = {
      {MIR_T_P, "obj", 0}, {MIR_T_U32, "slot", 0}, {MIR_T_I32, "value", 0}};
  MIR_var_t set_slot_u32_args[] = {
      {MIR_T_P, "obj", 0}, {MIR_T_U32, "slot", 0}, {MIR_T_U32, "value", 0}};
  MIR_var_t set_slot_i64_args[] = {
      {MIR_T_P, "obj", 0}, {MIR_T_U32, "slot", 0}, {MIR_T_I64, "value", 0}};
  MIR_var_t set_slot_u64_args[] = {
      {MIR_T_P, "obj", 0}, {MIR_T_U32, "slot", 0}, {MIR_T_U64, "value", 0}};
  MIR_var_t set_slot_dbl_args[] = {
      {MIR_T_P, "obj", 0}, {MIR_T_U32, "slot", 0}, {MIR_T_D, "value", 0}};
  MIR_var_t set_slot_str_args[] = {
      {MIR_T_P, "obj", 0}, {MIR_T_U32, "slot", 0}, {MIR_T_P, "value", 0}};
  MIR_var_t set_slot_bytes_args[] = {{MIR_T_P, "obj", 0}, {MIR_T_U32, "slot", 0},
                                     {MIR_T_P, "data", 0}, {MIR_T_I64, "len", 0}};

  declare_external(builder, &builder->ext.create_obj, "create_obj", 1, &ptr_result, 0, NULL);
  declare_external(builder, &builder->ext.free_value, "free_value", 0, NULL, 1, free_args);
  declare_external(builder, &builder->ext.set_int, "set_int", 1, &status_result, 3, set_int_args);
  declare_external(builder, &builder->ext.set_u32, "set_uint32", 1, &status_result, 3,
                   set_u32_args);
  declare_external(builder, &builder->ext.set_i64, "set_int64", 1, &status_result, 3, set_i64_args);
  declare_external(builder, &builder->ext.set_u64, "set_uint64", 1, &status_result, 3, set_u64_args);
  declare_external(builder, &builder->ext.set_dbl, "set_dbl", 1, &status_result, 3, set_dbl_args);
  declare_external(builder, &builder->ext.set_bool, "set_bool", 1, &status_result, 3, set_bool_args);
  declare_external(builder, &builder->ext.set_str, "set_str", 1, &status_result, 3, set_str_args);
  declare_external(builder, &builder->ext.set_bytes, "set_bytes", 1, &status_result, 4, set_bytes_args);
  declare_external(builder, &builder->ext.set_uuid, "set_uuid", 1, &status_result, 3, set_uuid_args);
  declare_external(builder, &builder->ext.create_list, "create_list", 1, &ptr_result, 0, NULL);
  declare_external(builder, &builder->ext.add_list_int, "add_list_int", 1, &status_result, 2, list_i32_args);
  declare_external(builder, &builder->ext.add_list_u32, "add_list_uint32", 1, &status_result, 2,
                   list_u32_args);
  declare_external(builder, &builder->ext.add_list_i64, "add_list_int64", 1, &status_result, 2,
                   list_i64_args);
  declare_external(builder, &builder->ext.add_list_u64, "add_list_uint64", 1, &status_result, 2,
                   list_u64_args);
  declare_external(builder, &builder->ext.add_list_dbl, "add_list_dbl", 1, &status_result, 2, list_dbl_args);
  declare_external(builder, &builder->ext.add_list_bool, "add_list_bool", 1, &status_result, 2,
                   list_bool_args);
  declare_external(builder, &builder->ext.add_list_str, "add_list_str", 1, &status_result, 2, list_str_args);
  declare_external(builder, &builder->ext.add_list_obj, "add_list_obj", 1, &status_result, 2, list_obj_args);
  declare_external(builder, &builder->ext.set_list, "set_list", 1, &status_result, 3, set_list_args);
  declare_external(builder, &builder->ext.create_set, "create_set", 1, &ptr_result, 0, NULL);
  declare_external(builder, &builder->ext.add_set_int, "add_set_int", 1, &status_result, 2, list_i32_args);
  declare_external(builder, &builder->ext.add_set_u32, "add_set_uint32", 1, &status_result, 2,
                   list_u32_args);
  declare_external(builder, &builder->ext.add_set_i64, "add_set_int64", 1, &status_result, 2,
                   list_i64_args);
  declare_external(builder, &builder->ext.add_set_u64, "add_set_uint64", 1, &status_result, 2,
                   list_u64_args);
  declare_external(builder, &builder->ext.add_set_dbl, "add_set_dbl", 1, &status_result, 2, list_dbl_args);
  declare_external(builder, &builder->ext.add_set_bool, "add_set_bool", 1, &status_result, 2, list_bool_args);
  declare_external(builder, &builder->ext.add_set_str, "add_set_str", 1, &status_result, 2, list_str_args);
  declare_external(builder, &builder->ext.set_set, "set_set", 1, &status_result, 3, set_list_args);
  declare_external(builder, &builder->ext.create_map, "create_map", 1, &ptr_result, 0, NULL);
  declare_external(builder, &builder->ext.add_map_str_str, "add_map_str_str", 1, &status_result, 3,
                   map_str_str_args);
  declare_external(builder, &builder->ext.add_map_str_int, "add_map_str_int", 1, &status_result, 3,
                   map_str_int_args);
  declare_external(builder, &builder->ext.add_map_str_u32, "add_map_str_uint32", 1,
                   &status_result, 3, map_str_u32_args);
  declare_external(builder, &builder->ext.add_map_str_i64, "add_map_str_int64", 1, &status_result,
                   3, map_str_i64_args);
  declare_external(builder, &builder->ext.add_map_str_u64, "add_map_str_uint64", 1,
                   &status_result, 3, map_str_u64_args);
  declare_external(builder, &builder->ext.add_map_str_dbl, "add_map_str_dbl", 1, &status_result, 3,
                   map_str_dbl_args);
  declare_external(builder, &builder->ext.add_map_str_bool, "add_map_str_bool", 1, &status_result, 3,
                   map_str_bool_args);
  declare_external(builder, &builder->ext.set_map, "set_map", 1, &status_result, 3, set_list_args);
  declare_external(builder, &builder->ext.read_varstr, "read_varstr", 1, &ptr_result, 3,
                   read_varstr_args);
  declare_external(builder, &builder->ext.free_fn, "free", 0, NULL, 1, free_args);
  if (builder->include_record_v1) {
    declare_external(builder, &builder->ext.create_record_child_v1, "record_create_child_v1", 1,
                     &ptr_result, 1, create_record_child_args);
    declare_external(builder, &builder->ext.create_record_field_v1, "record_create_field_v1", 1,
                     &ptr_result, 2, create_record_field_args);
    declare_external(builder, &builder->ext.set_slot_int_v1, "record_set_slot_int_v1", 1,
                     &status_result, 3, set_slot_i32_args);
    declare_external(builder, &builder->ext.set_slot_u32_v1, "record_set_slot_uint32_v1", 1,
                     &status_result, 3, set_slot_u32_args);
    declare_external(builder, &builder->ext.set_slot_i64_v1, "record_set_slot_int64_v1", 1,
                     &status_result, 3, set_slot_i64_args);
    declare_external(builder, &builder->ext.set_slot_u64_v1, "record_set_slot_uint64_v1", 1,
                     &status_result, 3, set_slot_u64_args);
    declare_external(builder, &builder->ext.set_slot_dbl_v1, "record_set_slot_double_v1", 1,
                     &status_result, 3, set_slot_dbl_args);
    declare_external(builder, &builder->ext.set_slot_bool_v1, "record_set_slot_bool_v1", 1,
                     &status_result, 3, set_slot_i32_args);
    declare_external(builder, &builder->ext.set_slot_str_v1, "record_set_slot_string_v1", 1,
                     &status_result, 3, set_slot_str_args);
    declare_external(builder, &builder->ext.set_slot_bytes_v1, "record_set_slot_bytes_v1", 1,
                     &status_result, 4, set_slot_bytes_args);
    declare_external(builder, &builder->ext.set_slot_uuid_v1, "record_set_slot_uuid_v1", 1,
                     &status_result, 3, set_slot_str_args);
    declare_external(builder, &builder->ext.set_slot_list_v1, "record_set_slot_list_v1", 1,
                     &status_result, 3, set_slot_str_args);
    declare_external(builder, &builder->ext.set_slot_set_v1, "record_set_slot_set_v1", 1,
                     &status_result, 3, set_slot_str_args);
    declare_external(builder, &builder->ext.set_slot_map_v1, "record_set_slot_map_v1", 1,
                     &status_result, 3, set_slot_str_args);
  }
}

static MIR_reg_t emitter_new_reg(mir_emitter_t *e, MIR_type_t type, const char *base_name) {
  char *name = builder_make_name(e->builder, base_name, e->builder->temp_name_id++);
  return MIR_new_func_reg(e->builder->ctx, e->func, type, name);
}
static MIR_op_t emitter_reg(mir_emitter_t *e, MIR_reg_t reg) {
  return MIR_new_reg_op(e->builder->ctx, reg);
}
static MIR_op_t emitter_label(mir_emitter_t *e, MIR_label_t label) {
  return MIR_new_label_op(e->builder->ctx, label);
}
static void emitter_append(mir_emitter_t *e, MIR_insn_t insn) {
  MIR_append_insn(e->builder->ctx, e->func_item, insn);
}

static void emitter_call(mir_emitter_t *e, external_ref_t *ref, MIR_op_t *args, size_t nargs) {
  MIR_op_t ops[8];
  size_t i;
  ops[0] = MIR_new_ref_op(e->builder->ctx, ref->proto_item);
  ops[1] = MIR_new_ref_op(e->builder->ctx, ref->import_item);
  for (i = 0; i < nargs; i++)
    ops[i + 2] = args[i];
  emitter_append(e, MIR_new_insn_arr(e->builder->ctx, MIR_CALL, nargs + 2, ops));
}

static void emitter_call_result(mir_emitter_t *e, external_ref_t *ref, MIR_reg_t result_reg,
                                MIR_op_t *args, size_t nargs) {
  MIR_op_t ops[8];
  size_t i;
  ops[0] = MIR_new_ref_op(e->builder->ctx, ref->proto_item);
  ops[1] = MIR_new_ref_op(e->builder->ctx, ref->import_item);
  ops[2] = emitter_reg(e, result_reg);
  for (i = 0; i < nargs; i++)
    ops[i + 3] = args[i];
  emitter_append(e, MIR_new_insn_arr(e->builder->ctx, MIR_CALL, nargs + 3, ops));
}

static MIR_reg_t emitter_call_status(mir_emitter_t *e, external_ref_t *ref, MIR_op_t *args,
                                     size_t nargs) {
  MIR_reg_t status_reg = emitter_new_reg(e, MIR_T_I64, "__mutation_status");
  emitter_call_result(e, ref, status_reg, args, nargs);
  return status_reg;
}

static void emitter_branch_on_failure(mir_emitter_t *e, MIR_reg_t status_reg) {
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                 emitter_reg(e, status_reg), MIR_new_int_op(e->builder->ctx, 0)));
}

static void emitter_call_checked(mir_emitter_t *e, external_ref_t *ref, MIR_op_t *args,
                                 size_t nargs) {
  emitter_branch_on_failure(e, emitter_call_status(e, ref, args, nargs));
}

static MIR_op_t emitter_mem(mir_emitter_t *e, MIR_type_t type, MIR_reg_t off_reg, int disp) {
  return MIR_new_mem_op(e->builder->ctx, type, disp, e->buf_reg, off_reg, 1);
}
static void emitter_advance_const(mir_emitter_t *e, MIR_reg_t off_reg, int delta) {
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, off_reg),
                                 emitter_reg(e, off_reg), MIR_new_int_op(e->builder->ctx, delta)));
}
static void emitter_advance_reg(mir_emitter_t *e, MIR_reg_t off_reg, MIR_reg_t delta_reg) {
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, off_reg),
                                 emitter_reg(e, off_reg), emitter_reg(e, delta_reg)));
}

static void emitter_bounds_check_const(mir_emitter_t *e, MIR_reg_t off_reg, int needed,
                                       MIR_label_t fail_label) {
  MIR_reg_t end_reg = emitter_new_reg(e, MIR_T_I64, "__end");
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, end_reg),
                                 emitter_reg(e, off_reg), MIR_new_int_op(e->builder->ctx, needed)));
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UBGT, emitter_label(e, fail_label),
                                 emitter_reg(e, end_reg), emitter_reg(e, e->len_reg)));
}

static void emitter_bounds_check_reg(mir_emitter_t *e, MIR_reg_t off_reg, MIR_reg_t needed_reg,
                                     MIR_label_t fail_label) {
  MIR_reg_t end_reg = emitter_new_reg(e, MIR_T_I64, "__end");
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, end_reg),
                                 emitter_reg(e, off_reg), emitter_reg(e, needed_reg)));
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UBGT, emitter_label(e, fail_label),
                                 emitter_reg(e, end_reg), emitter_reg(e, e->len_reg)));
}

static MIR_reg_t emitter_load_u16(mir_emitter_t *e, MIR_reg_t off_reg, int disp) {
  MIR_reg_t reg = emitter_new_reg(e, MIR_T_I64, "__u16");
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UEXT16, emitter_reg(e, reg),
                                 emitter_mem(e, MIR_T_U16, off_reg, disp)));
  return reg;
}

static MIR_reg_t emitter_load_u32(mir_emitter_t *e, MIR_reg_t off_reg, int disp) {
  MIR_reg_t reg = emitter_new_reg(e, MIR_T_I64, "__u32");
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UEXT32, emitter_reg(e, reg),
                                 emitter_mem(e, MIR_T_U32, off_reg, disp)));
  return reg;
}

static MIR_reg_t emitter_ptr_from_off(mir_emitter_t *e, MIR_reg_t off_reg, int disp,
                                      const char *name) {
  MIR_reg_t reg = emitter_new_reg(e, MIR_T_I64, name);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, reg),
                                 emitter_reg(e, e->buf_reg), emitter_reg(e, off_reg)));
  if (disp != 0)
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, reg),
                                   emitter_reg(e, reg), MIR_new_int_op(e->builder->ctx, disp)));
  return reg;
}

static MIR_op_t emitter_int32_value(mir_emitter_t *e, const emit_field_t *field,
                                    MIR_reg_t off_reg) {
  MIR_reg_t reg;
  switch (field->mir_type) {
  case MIR_T_I8:
    reg = emitter_new_reg(e, MIR_T_I64, "__i8");
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_EXT8, emitter_reg(e, reg),
                                   emitter_mem(e, MIR_T_I8, off_reg, 0)));
    return emitter_reg(e, reg);
  case MIR_T_U8:
    reg = emitter_new_reg(e, MIR_T_I64, "__u8");
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UEXT8, emitter_reg(e, reg),
                                   emitter_mem(e, MIR_T_U8, off_reg, 0)));
    return emitter_reg(e, reg);
  case MIR_T_I16:
    reg = emitter_new_reg(e, MIR_T_I64, "__i16");
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_EXT16, emitter_reg(e, reg),
                                   emitter_mem(e, MIR_T_I16, off_reg, 0)));
    return emitter_reg(e, reg);
  case MIR_T_U16:
    reg = emitter_new_reg(e, MIR_T_I64, "__u16v");
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UEXT16, emitter_reg(e, reg),
                                   emitter_mem(e, MIR_T_U16, off_reg, 0)));
    return emitter_reg(e, reg);
  case MIR_T_U32:
    reg = emitter_new_reg(e, MIR_T_I64, "__u32v");
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UEXT32, emitter_reg(e, reg),
                                   emitter_mem(e, MIR_T_U32, off_reg, 0)));
    return emitter_reg(e, reg);
  case MIR_T_I32:
  default:
    return emitter_mem(e, MIR_T_I32, off_reg, 0);
  }
}

static MIR_op_t emitter_bool_value(mir_emitter_t *e, MIR_reg_t off_reg) {
  MIR_reg_t reg = emitter_new_reg(e, MIR_T_I64, "__bool");
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UEXT8, emitter_reg(e, reg),
                                 emitter_mem(e, MIR_T_U8, off_reg, 0)));
  return emitter_reg(e, reg);
}

static MIR_op_t emitter_float_to_double(mir_emitter_t *e, MIR_reg_t off_reg) {
  MIR_reg_t reg = emitter_new_reg(e, MIR_T_D, "__dbl");
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_F2D, emitter_reg(e, reg),
                                 emitter_mem(e, MIR_T_F, off_reg, 0)));
  return emitter_reg(e, reg);
}

static MIR_op_t emitter_field_key_v1(mir_emitter_t *e, MIR_item_t field_name_item,
                                     uint32_t field_slot) {
  return e->slot_mode ? MIR_new_uint_op(e->builder->ctx, field_slot)
                      : MIR_new_ref_op(e->builder->ctx, field_name_item);
}

static external_ref_t *emitter_field_external_v1(mir_emitter_t *e, external_ref_t *legacy,
                                                  external_ref_t *slot_v1) {
  return e->slot_mode ? slot_v1 : legacy;
}

static void emit_fields_into_object(mir_emitter_t *e, MIR_reg_t target_obj_reg, MIR_reg_t off_reg,
                                    const emit_field_array_t *fields);

static void emit_scalar_field(mir_emitter_t *e, MIR_reg_t target_obj_reg, MIR_reg_t off_reg,
                               const emit_field_t *field, MIR_item_t field_name_item,
                               uint32_t field_slot) {
  MIR_op_t args[4];
  if (field->kind == EF_INT || field->kind == EF_U32 || field->kind == EF_I64 ||
      field->kind == EF_U64 || field->kind == EF_DBL ||
      field->kind == EF_BOOL) {
    emitter_bounds_check_const(e, off_reg, field->size, e->fail_label);
    args[0] = emitter_reg(e, target_obj_reg);
    args[1] = emitter_field_key_v1(e, field_name_item, field_slot);
    if (field->kind == EF_INT) {
      args[2] = emitter_int32_value(e, field, off_reg);
      emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_int,
                                                        &e->builder->ext.set_slot_int_v1),
                           args, 3);
    } else if (field->kind == EF_U32) {
      args[2] = emitter_mem(e, MIR_T_U32, off_reg, 0);
      emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_u32,
                                                        &e->builder->ext.set_slot_u32_v1),
                           args, 3);
    } else if (field->kind == EF_I64) {
      args[2] = emitter_mem(e, field->mir_type, off_reg, 0);
      emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_i64,
                                                        &e->builder->ext.set_slot_i64_v1),
                           args, 3);
    } else if (field->kind == EF_U64) {
      args[2] = emitter_mem(e, MIR_T_U64, off_reg, 0);
      emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_u64,
                                                        &e->builder->ext.set_slot_u64_v1),
                           args, 3);
    } else if (field->kind == EF_BOOL) {
      args[2] = emitter_bool_value(e, off_reg);
      emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_bool,
                                                        &e->builder->ext.set_slot_bool_v1),
                           args, 3);
    } else {
      args[2] = field->mir_type == MIR_T_F ? emitter_float_to_double(e, off_reg)
                                           : emitter_mem(e, MIR_T_D, off_reg, 0);
      emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_dbl,
                                                        &e->builder->ext.set_slot_dbl_v1),
                           args, 3);
    }
    emitter_advance_const(e, off_reg, field->size);
    return;
  }
  if (field->kind == EF_FIX_BYTES) {
    emitter_bounds_check_const(e, off_reg, field->size, e->fail_label);
    if (field->has_set_bytes) {
      MIR_reg_t ptr_reg = emitter_ptr_from_off(e, off_reg, 0, "__bytes");
      args[0] = emitter_reg(e, target_obj_reg);
      args[1] = emitter_field_key_v1(e, field_name_item, field_slot);
      args[2] = emitter_reg(e, ptr_reg);
      args[3] = MIR_new_int_op(e->builder->ctx, field->size);
      emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_bytes,
                                                        &e->builder->ext.set_slot_bytes_v1),
                           args, 4);
    }
    emitter_advance_const(e, off_reg, field->size);
    return;
  }
  if (field->kind == EF_UUID) {
    MIR_reg_t ptr_reg;
    emitter_bounds_check_const(e, off_reg, 16, e->fail_label);
    ptr_reg = emitter_ptr_from_off(e, off_reg, 0, "__uuid");
    args[0] = emitter_reg(e, target_obj_reg);
    args[1] = emitter_field_key_v1(e, field_name_item, field_slot);
    args[2] = emitter_reg(e, ptr_reg);
    emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_uuid,
                                                      &e->builder->ext.set_slot_uuid_v1),
                         args, 3);
    emitter_advance_const(e, off_reg, 16);
    return;
  }
  if (field->kind == EF_STR || field->kind == EF_VAR_BYTES) {
    MIR_reg_t len_reg, total_reg;
    emitter_bounds_check_const(e, off_reg, 4, e->fail_label);
    len_reg = emitter_load_u32(e, off_reg, 0);
    total_reg = emitter_new_reg(e, MIR_T_I64, "__var_total");
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, total_reg),
                                   emitter_reg(e, len_reg), MIR_new_int_op(e->builder->ctx, 4)));
    emitter_bounds_check_reg(e, off_reg, total_reg, e->fail_label);
    if (field->kind == EF_STR) {
      MIR_reg_t str_reg = emitter_new_reg(e, MIR_T_I64, "__str");
      MIR_reg_t remaining_reg = emitter_new_reg(e, MIR_T_I64, "__str_rem");
      MIR_reg_t status_reg;
      emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_SUB, emitter_reg(e, remaining_reg),
                                     emitter_reg(e, e->len_reg), emitter_reg(e, off_reg)));
      args[0] = emitter_reg(e, e->buf_reg);
      args[1] = emitter_reg(e, off_reg);
      args[2] = emitter_reg(e, remaining_reg);
      emitter_call_result(e, &e->builder->ext.read_varstr, str_reg, args, 3);
      emitter_advance_reg(e, off_reg, total_reg);
      emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                     emitter_reg(e, str_reg), MIR_new_int_op(e->builder->ctx, 0)));
      args[0] = emitter_reg(e, target_obj_reg);
      args[1] = emitter_field_key_v1(e, field_name_item, field_slot);
      args[2] = emitter_reg(e, str_reg);
      status_reg = emitter_call_status(
          e, emitter_field_external_v1(e, &e->builder->ext.set_str,
                                       &e->builder->ext.set_slot_str_v1),
          args, 3);
      args[0] = emitter_reg(e, str_reg);
      emitter_call(e, &e->builder->ext.free_fn, args, 1);
      emitter_branch_on_failure(e, status_reg);
    } else {
      if (field->has_set_bytes) {
        MIR_reg_t ptr_reg = emitter_ptr_from_off(e, off_reg, 4, "__var_bytes");
        args[0] = emitter_reg(e, target_obj_reg);
        args[1] = emitter_field_key_v1(e, field_name_item, field_slot);
        args[2] = emitter_reg(e, ptr_reg);
        args[3] = emitter_reg(e, len_reg);
        emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_bytes,
                                                          &e->builder->ext.set_slot_bytes_v1),
                             args, 4);
      }
      emitter_advance_reg(e, off_reg, total_reg);
    }
  }
}

static void emit_list_field(mir_emitter_t *e, MIR_reg_t target_obj_reg, MIR_reg_t off_reg,
                             const emit_field_t *field, MIR_item_t field_name_item,
                             uint32_t field_slot) {
  MIR_reg_t list_reg = emitter_new_reg(e, MIR_T_I64, "__list"),
            count_reg = emitter_new_reg(e, MIR_T_I64, "__count"),
            index_reg = emitter_new_reg(e, MIR_T_I64, "__idx");
  MIR_label_t loop_label = MIR_new_label(e->builder->ctx),
              done_label = MIR_new_label(e->builder->ctx);
  MIR_op_t args[4];
  emitter_call_result(e, &e->builder->ext.create_list, list_reg, NULL, 0);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                 emitter_reg(e, list_reg), MIR_new_int_op(e->builder->ctx, 0)));
  args[0] = emitter_reg(e, target_obj_reg);
  args[1] = emitter_field_key_v1(e, field_name_item, field_slot);
  args[2] = emitter_reg(e, list_reg);
  emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_list,
                                                    &e->builder->ext.set_slot_list_v1),
                       args, 3);
  if (field->fixed_count > 0) {
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, count_reg),
                                   MIR_new_int_op(e->builder->ctx, field->fixed_count)));
  } else {
    emitter_bounds_check_const(e, off_reg, 4, e->fail_label);
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, count_reg),
                                   emitter_reg(e, emitter_load_u32(e, off_reg, 0))));
    emitter_advance_const(e, off_reg, 4);
  }
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, index_reg),
                                 MIR_new_int_op(e->builder->ctx, 0)));
  emitter_append(e, loop_label);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UBGE, emitter_label(e, done_label),
                                 emitter_reg(e, index_reg), emitter_reg(e, count_reg)));
  if (field->kind == EF_LIST_INT || field->kind == EF_LIST_U32 ||
      field->kind == EF_LIST_I64 || field->kind == EF_LIST_U64 ||
      field->kind == EF_LIST_DBL || field->kind == EF_LIST_BOOL) {
    emitter_bounds_check_const(e, off_reg, field->size, e->fail_label);
    args[0] = emitter_reg(e, list_reg);
    if (field->kind == EF_LIST_INT) {
      args[1] = emitter_int32_value(e, field, off_reg);
      emitter_call_checked(e, &e->builder->ext.add_list_int, args, 2);
    } else if (field->kind == EF_LIST_U32) {
      args[1] = emitter_mem(e, MIR_T_U32, off_reg, 0);
      emitter_call_checked(e, &e->builder->ext.add_list_u32, args, 2);
    } else if (field->kind == EF_LIST_I64) {
      args[1] = emitter_mem(e, field->mir_type, off_reg, 0);
      emitter_call_checked(e, &e->builder->ext.add_list_i64, args, 2);
    } else if (field->kind == EF_LIST_U64) {
      args[1] = emitter_mem(e, MIR_T_U64, off_reg, 0);
      emitter_call_checked(e, &e->builder->ext.add_list_u64, args, 2);
    } else if (field->kind == EF_LIST_BOOL) {
      args[1] = emitter_bool_value(e, off_reg);
      emitter_call_checked(e, &e->builder->ext.add_list_bool, args, 2);
    } else {
      args[1] = field->mir_type == MIR_T_F ? emitter_float_to_double(e, off_reg)
                                           : emitter_mem(e, MIR_T_D, off_reg, 0);
      emitter_call_checked(e, &e->builder->ext.add_list_dbl, args, 2);
    }
    emitter_advance_const(e, off_reg, field->size);
  } else if (field->kind == EF_LIST_STR) {
    MIR_reg_t len_reg, total_reg, str_reg;
    MIR_reg_t status_reg;
    emitter_bounds_check_const(e, off_reg, 4, e->fail_label);
    len_reg = emitter_load_u32(e, off_reg, 0);
    total_reg = emitter_new_reg(e, MIR_T_I64, "__list_str_total");
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, total_reg),
                                   emitter_reg(e, len_reg), MIR_new_int_op(e->builder->ctx, 4)));
    emitter_bounds_check_reg(e, off_reg, total_reg, e->fail_label);
    str_reg = emitter_new_reg(e, MIR_T_I64, "__list_str");
    {
      MIR_reg_t lrem_reg = emitter_new_reg(e, MIR_T_I64, "__list_str_rem");
      emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_SUB, emitter_reg(e, lrem_reg),
                                     emitter_reg(e, e->len_reg), emitter_reg(e, off_reg)));
      args[0] = emitter_reg(e, e->buf_reg);
      args[1] = emitter_reg(e, off_reg);
      args[2] = emitter_reg(e, lrem_reg);
      emitter_call_result(e, &e->builder->ext.read_varstr, str_reg, args, 3);
    }
    emitter_advance_reg(e, off_reg, total_reg);
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                   emitter_reg(e, str_reg), MIR_new_int_op(e->builder->ctx, 0)));
    args[0] = emitter_reg(e, list_reg);
    args[1] = emitter_reg(e, str_reg);
    status_reg = emitter_call_status(e, &e->builder->ext.add_list_str, args, 2);
    args[0] = emitter_reg(e, str_reg);
    emitter_call(e, &e->builder->ext.free_fn, args, 1);
    emitter_branch_on_failure(e, status_reg);
  } else if (field->kind == EF_LIST_OBJ) {
    MIR_reg_t child_reg = emitter_new_reg(e, MIR_T_I64, "__child"),
              child_off_reg = emitter_new_reg(e, MIR_T_I64, "__child_off");
    if (e->slot_mode) {
      args[0] = emitter_reg(e, list_reg);
      emitter_call_result(e, &e->builder->ext.create_record_child_v1, child_reg, args, 1);
    } else {
      emitter_call_result(e, &e->builder->ext.create_obj, child_reg, NULL, 0);
    }
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                   emitter_reg(e, child_reg), MIR_new_int_op(e->builder->ctx, 0)));
    args[0] = emitter_reg(e, list_reg);
    args[1] = emitter_reg(e, child_reg);
    emitter_call_checked(e, &e->builder->ext.add_list_obj, args, 2);
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, child_off_reg),
                                   emitter_reg(e, off_reg)));
    emit_fields_into_object(e, child_reg, child_off_reg, &field->children);
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, off_reg),
                                   emitter_reg(e, child_off_reg)));
  }
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, index_reg),
                                 emitter_reg(e, index_reg), MIR_new_int_op(e->builder->ctx, 1)));
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_JMP, emitter_label(e, loop_label)));
  emitter_append(e, done_label);
}

static void emit_set_field(mir_emitter_t *e, MIR_reg_t target_obj_reg, MIR_reg_t off_reg,
                           const emit_field_t *field, MIR_item_t field_name_item,
                           uint32_t field_slot) {
  MIR_reg_t set_reg = emitter_new_reg(e, MIR_T_I64, "__set"),
            count_reg = emitter_new_reg(e, MIR_T_I64, "__count"),
            index_reg = emitter_new_reg(e, MIR_T_I64, "__idx");
  MIR_label_t loop_label = MIR_new_label(e->builder->ctx),
              done_label = MIR_new_label(e->builder->ctx);
  MIR_op_t args[4];
  emitter_call_result(e, &e->builder->ext.create_set, set_reg, NULL, 0);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                 emitter_reg(e, set_reg), MIR_new_int_op(e->builder->ctx, 0)));
  args[0] = emitter_reg(e, target_obj_reg);
  args[1] = emitter_field_key_v1(e, field_name_item, field_slot);
  args[2] = emitter_reg(e, set_reg);
  emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_set,
                                                    &e->builder->ext.set_slot_set_v1),
                       args, 3);
  emitter_bounds_check_const(e, off_reg, 4, e->fail_label);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, count_reg),
                                 emitter_reg(e, emitter_load_u32(e, off_reg, 0))));
  emitter_advance_const(e, off_reg, 4);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, index_reg),
                                 MIR_new_int_op(e->builder->ctx, 0)));
  emitter_append(e, loop_label);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UBGE, emitter_label(e, done_label),
                                 emitter_reg(e, index_reg), emitter_reg(e, count_reg)));
  args[0] = emitter_reg(e, set_reg);
  if (field->kind == EF_SET_INT) {
    emitter_bounds_check_const(e, off_reg, field->size, e->fail_label);
    args[1] = emitter_int32_value(e, field, off_reg);
    emitter_call_checked(e, &e->builder->ext.add_set_int, args, 2);
    emitter_advance_const(e, off_reg, field->size);
  } else if (field->kind == EF_SET_U32) {
    emitter_bounds_check_const(e, off_reg, field->size, e->fail_label);
    args[1] = emitter_mem(e, MIR_T_U32, off_reg, 0);
    emitter_call_checked(e, &e->builder->ext.add_set_u32, args, 2);
    emitter_advance_const(e, off_reg, field->size);
  } else if (field->kind == EF_SET_I64) {
    emitter_bounds_check_const(e, off_reg, field->size, e->fail_label);
    args[1] = emitter_mem(e, field->mir_type, off_reg, 0);
    emitter_call_checked(e, &e->builder->ext.add_set_i64, args, 2);
    emitter_advance_const(e, off_reg, field->size);
  } else if (field->kind == EF_SET_U64) {
    emitter_bounds_check_const(e, off_reg, field->size, e->fail_label);
    args[1] = emitter_mem(e, MIR_T_U64, off_reg, 0);
    emitter_call_checked(e, &e->builder->ext.add_set_u64, args, 2);
    emitter_advance_const(e, off_reg, field->size);
  } else if (field->kind == EF_SET_DBL) {
    emitter_bounds_check_const(e, off_reg, field->size, e->fail_label);
    args[1] = field->mir_type == MIR_T_F ? emitter_float_to_double(e, off_reg)
                                         : emitter_mem(e, MIR_T_D, off_reg, 0);
    emitter_call_checked(e, &e->builder->ext.add_set_dbl, args, 2);
    emitter_advance_const(e, off_reg, field->size);
  } else if (field->kind == EF_SET_BOOL) {
    emitter_bounds_check_const(e, off_reg, field->size, e->fail_label);
    args[1] = emitter_bool_value(e, off_reg);
    emitter_call_checked(e, &e->builder->ext.add_set_bool, args, 2);
    emitter_advance_const(e, off_reg, field->size);
  } else if (field->kind == EF_SET_STR) {
    MIR_reg_t len_reg, total_reg, str_reg;
    MIR_reg_t status_reg;
    emitter_bounds_check_const(e, off_reg, 4, e->fail_label);
    len_reg = emitter_load_u32(e, off_reg, 0);
    total_reg = emitter_new_reg(e, MIR_T_I64, "__set_str_total");
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, total_reg),
                                   emitter_reg(e, len_reg), MIR_new_int_op(e->builder->ctx, 4)));
    emitter_bounds_check_reg(e, off_reg, total_reg, e->fail_label);
    str_reg = emitter_new_reg(e, MIR_T_I64, "__set_str");
    {
      MIR_reg_t srem_reg = emitter_new_reg(e, MIR_T_I64, "__set_str_rem");
      emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_SUB, emitter_reg(e, srem_reg),
                                     emitter_reg(e, e->len_reg), emitter_reg(e, off_reg)));
      args[0] = emitter_reg(e, e->buf_reg);
      args[1] = emitter_reg(e, off_reg);
      args[2] = emitter_reg(e, srem_reg);
      emitter_call_result(e, &e->builder->ext.read_varstr, str_reg, args, 3);
    }
    emitter_advance_reg(e, off_reg, total_reg);
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                   emitter_reg(e, str_reg), MIR_new_int_op(e->builder->ctx, 0)));
    args[0] = emitter_reg(e, set_reg);
    args[1] = emitter_reg(e, str_reg);
    status_reg = emitter_call_status(e, &e->builder->ext.add_set_str, args, 2);
    args[0] = emitter_reg(e, str_reg);
    emitter_call(e, &e->builder->ext.free_fn, args, 1);
    emitter_branch_on_failure(e, status_reg);
  }
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, index_reg),
                                 emitter_reg(e, index_reg), MIR_new_int_op(e->builder->ctx, 1)));
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_JMP, emitter_label(e, loop_label)));
  emitter_append(e, done_label);
}

static void emit_map_field(mir_emitter_t *e, MIR_reg_t target_obj_reg, MIR_reg_t off_reg,
                           const emit_field_t *field, MIR_item_t field_name_item,
                           uint32_t field_slot) {
  MIR_reg_t map_reg = emitter_new_reg(e, MIR_T_I64, "__map"),
            count_reg = emitter_new_reg(e, MIR_T_I64, "__count"),
            index_reg = emitter_new_reg(e, MIR_T_I64, "__idx");
  MIR_label_t loop_label = MIR_new_label(e->builder->ctx),
              done_label = MIR_new_label(e->builder->ctx);
  MIR_op_t args[4];
  emitter_call_result(e, &e->builder->ext.create_map, map_reg, NULL, 0);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                 emitter_reg(e, map_reg), MIR_new_int_op(e->builder->ctx, 0)));
  args[0] = emitter_reg(e, target_obj_reg);
  args[1] = emitter_field_key_v1(e, field_name_item, field_slot);
  args[2] = emitter_reg(e, map_reg);
  emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_map,
                                                    &e->builder->ext.set_slot_map_v1),
                       args, 3);
  emitter_bounds_check_const(e, off_reg, 4, e->fail_label);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, count_reg),
                                 emitter_reg(e, emitter_load_u32(e, off_reg, 0))));
  emitter_advance_const(e, off_reg, 4);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, index_reg),
                                 MIR_new_int_op(e->builder->ctx, 0)));
  emitter_append(e, loop_label);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UBGE, emitter_label(e, done_label),
                                 emitter_reg(e, index_reg), emitter_reg(e, count_reg)));
  {
    MIR_reg_t key_len_reg, key_total_reg, key_reg;
    MIR_label_t cleanup_key_label = MIR_new_label(e->builder->ctx);
    MIR_label_t item_done_label = MIR_new_label(e->builder->ctx);
    emitter_bounds_check_const(e, off_reg, 4, e->fail_label);
    key_len_reg = emitter_load_u32(e, off_reg, 0);
    key_total_reg = emitter_new_reg(e, MIR_T_I64, "__key_total");
    emitter_append(e,
                   MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, key_total_reg),
                                emitter_reg(e, key_len_reg), MIR_new_int_op(e->builder->ctx, 4)));
    emitter_bounds_check_reg(e, off_reg, key_total_reg, e->fail_label);
    key_reg = emitter_new_reg(e, MIR_T_I64, "__key");
    {
      MIR_reg_t krem_reg = emitter_new_reg(e, MIR_T_I64, "__key_rem");
      emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_SUB, emitter_reg(e, krem_reg),
                                     emitter_reg(e, e->len_reg), emitter_reg(e, off_reg)));
      args[0] = emitter_reg(e, e->buf_reg);
      args[1] = emitter_reg(e, off_reg);
      args[2] = emitter_reg(e, krem_reg);
      emitter_call_result(e, &e->builder->ext.read_varstr, key_reg, args, 3);
    }
    emitter_advance_reg(e, off_reg, key_total_reg);
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                   emitter_reg(e, key_reg), MIR_new_int_op(e->builder->ctx, 0)));
    if (field->kind == EF_MAP_STR_STR) {
      MIR_reg_t val_len_reg, val_total_reg, val_reg, status_reg;
      emitter_bounds_check_const(e, off_reg, 4, cleanup_key_label);
      val_len_reg = emitter_load_u32(e, off_reg, 0);
      val_total_reg = emitter_new_reg(e, MIR_T_I64, "__val_total");
      emitter_append(e,
                     MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, val_total_reg),
                                  emitter_reg(e, val_len_reg), MIR_new_int_op(e->builder->ctx, 4)));
      emitter_bounds_check_reg(e, off_reg, val_total_reg, cleanup_key_label);
      val_reg = emitter_new_reg(e, MIR_T_I64, "__val");
      {
        MIR_reg_t vrem_reg = emitter_new_reg(e, MIR_T_I64, "__val_rem");
        emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_SUB, emitter_reg(e, vrem_reg),
                                       emitter_reg(e, e->len_reg), emitter_reg(e, off_reg)));
        args[0] = emitter_reg(e, e->buf_reg);
        args[1] = emitter_reg(e, off_reg);
        args[2] = emitter_reg(e, vrem_reg);
        emitter_call_result(e, &e->builder->ext.read_varstr, val_reg, args, 3);
      }
      emitter_advance_reg(e, off_reg, val_total_reg);
      emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ,
                                     emitter_label(e, cleanup_key_label),
                                     emitter_reg(e, val_reg),
                                     MIR_new_int_op(e->builder->ctx, 0)));
      args[0] = emitter_reg(e, map_reg);
      args[1] = emitter_reg(e, key_reg);
      args[2] = emitter_reg(e, val_reg);
      status_reg = emitter_call_status(e, &e->builder->ext.add_map_str_str, args, 3);
      args[0] = emitter_reg(e, key_reg);
      emitter_call(e, &e->builder->ext.free_fn, args, 1);
      args[0] = emitter_reg(e, val_reg);
      emitter_call(e, &e->builder->ext.free_fn, args, 1);
      emitter_branch_on_failure(e, status_reg);
      emitter_append(e,
                     MIR_new_insn(e->builder->ctx, MIR_JMP, emitter_label(e, item_done_label)));
    } else if (field->kind == EF_MAP_STR_INT || field->kind == EF_MAP_STR_U32 ||
               field->kind == EF_MAP_STR_I64 || field->kind == EF_MAP_STR_U64 ||
               field->kind == EF_MAP_STR_DBL || field->kind == EF_MAP_STR_BOOL) {
      MIR_reg_t status_reg;
      args[0] = emitter_reg(e, map_reg);
      args[1] = emitter_reg(e, key_reg);
      if (field->kind == EF_MAP_STR_INT) {
        emitter_bounds_check_const(e, off_reg, field->size, cleanup_key_label);
        args[2] = emitter_int32_value(e, field, off_reg);
        status_reg = emitter_call_status(e, &e->builder->ext.add_map_str_int, args, 3);
        emitter_advance_const(e, off_reg, field->size);
      } else if (field->kind == EF_MAP_STR_U32) {
        emitter_bounds_check_const(e, off_reg, field->size, cleanup_key_label);
        args[2] = emitter_mem(e, MIR_T_U32, off_reg, 0);
        status_reg = emitter_call_status(e, &e->builder->ext.add_map_str_u32, args, 3);
        emitter_advance_const(e, off_reg, field->size);
      } else if (field->kind == EF_MAP_STR_I64) {
        emitter_bounds_check_const(e, off_reg, field->size, cleanup_key_label);
        args[2] = emitter_mem(e, field->mir_type, off_reg, 0);
        status_reg = emitter_call_status(e, &e->builder->ext.add_map_str_i64, args, 3);
        emitter_advance_const(e, off_reg, field->size);
      } else if (field->kind == EF_MAP_STR_U64) {
        emitter_bounds_check_const(e, off_reg, field->size, cleanup_key_label);
        args[2] = emitter_mem(e, MIR_T_U64, off_reg, 0);
        status_reg = emitter_call_status(e, &e->builder->ext.add_map_str_u64, args, 3);
        emitter_advance_const(e, off_reg, field->size);
      } else if (field->kind == EF_MAP_STR_BOOL) {
        emitter_bounds_check_const(e, off_reg, field->size, cleanup_key_label);
        args[2] = emitter_bool_value(e, off_reg);
        status_reg = emitter_call_status(e, &e->builder->ext.add_map_str_bool, args, 3);
        emitter_advance_const(e, off_reg, field->size);
      } else {
        emitter_bounds_check_const(e, off_reg, field->size, cleanup_key_label);
        args[2] = field->mir_type == MIR_T_F ? emitter_float_to_double(e, off_reg)
                                             : emitter_mem(e, MIR_T_D, off_reg, 0);
        status_reg = emitter_call_status(e, &e->builder->ext.add_map_str_dbl, args, 3);
        emitter_advance_const(e, off_reg, field->size);
      }
      args[0] = emitter_reg(e, key_reg);
      emitter_call(e, &e->builder->ext.free_fn, args, 1);
      emitter_branch_on_failure(e, status_reg);
      emitter_append(e,
                     MIR_new_insn(e->builder->ctx, MIR_JMP, emitter_label(e, item_done_label)));
    }
    emitter_append(e, cleanup_key_label);
    args[0] = emitter_reg(e, key_reg);
    emitter_call(e, &e->builder->ext.free_fn, args, 1);
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_JMP, emitter_label(e, e->fail_label)));
    emitter_append(e, item_done_label);
  }
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, index_reg),
                                 emitter_reg(e, index_reg), MIR_new_int_op(e->builder->ctx, 1)));
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_JMP, emitter_label(e, loop_label)));
  emitter_append(e, done_label);
}

static void emit_group_field(mir_emitter_t *e, MIR_reg_t target_obj_reg, MIR_reg_t off_reg,
                             const emit_field_t *field, MIR_item_t field_name_item,
                             uint32_t field_slot) {
  MIR_reg_t block_len_reg, count_reg, entries_size_reg, total_reg, list_reg, entries_off_reg,
      index_reg;
  MIR_label_t loop_label, done_label;
  MIR_op_t args[4];
  emitter_bounds_check_const(e, off_reg, field->group_dim, e->fail_label);
  block_len_reg = emitter_load_u16(e, off_reg, 0);
  count_reg = emitter_load_u16(e, off_reg, 2);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UBGT, emitter_label(e, e->fail_label),
                                 MIR_new_int_op(e->builder->ctx, field->size),
                                 emitter_reg(e, block_len_reg)));
  entries_size_reg = emitter_new_reg(e, MIR_T_I64, "__entries_size");
  total_reg = emitter_new_reg(e, MIR_T_I64, "__group_total");
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MUL, emitter_reg(e, entries_size_reg),
                                 emitter_reg(e, block_len_reg), emitter_reg(e, count_reg)));
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, total_reg),
                                 emitter_reg(e, entries_size_reg),
                                 MIR_new_int_op(e->builder->ctx, field->group_dim)));
  emitter_bounds_check_reg(e, off_reg, total_reg, e->fail_label);
  list_reg = emitter_new_reg(e, MIR_T_I64, "__group_list");
  emitter_call_result(e, &e->builder->ext.create_list, list_reg, NULL, 0);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                 emitter_reg(e, list_reg), MIR_new_int_op(e->builder->ctx, 0)));
  args[0] = emitter_reg(e, target_obj_reg);
  args[1] = emitter_field_key_v1(e, field_name_item, field_slot);
  args[2] = emitter_reg(e, list_reg);
  emitter_call_checked(e, emitter_field_external_v1(e, &e->builder->ext.set_list,
                                                    &e->builder->ext.set_slot_list_v1),
                       args, 3);
  entries_off_reg = emitter_new_reg(e, MIR_T_I64, "__entries_off");
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, entries_off_reg),
                                 emitter_reg(e, off_reg)));
  emitter_advance_const(e, entries_off_reg, field->group_dim);
  index_reg = emitter_new_reg(e, MIR_T_I64, "__group_idx");
  loop_label = MIR_new_label(e->builder->ctx);
  done_label = MIR_new_label(e->builder->ctx);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, index_reg),
                                 MIR_new_int_op(e->builder->ctx, 0)));
  emitter_append(e, loop_label);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_UBGE, emitter_label(e, done_label),
                                 emitter_reg(e, index_reg), emitter_reg(e, count_reg)));
  {
    MIR_reg_t stride_reg = emitter_new_reg(e, MIR_T_I64, "__stride"),
              entry_off_reg = emitter_new_reg(e, MIR_T_I64, "__entry_off"),
              child_reg = emitter_new_reg(e, MIR_T_I64, "__group_child"),
              child_off_reg = emitter_new_reg(e, MIR_T_I64, "__group_child_off");
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MUL, emitter_reg(e, stride_reg),
                                   emitter_reg(e, block_len_reg), emitter_reg(e, index_reg)));
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, entry_off_reg),
                                   emitter_reg(e, entries_off_reg), emitter_reg(e, stride_reg)));
    if (e->slot_mode) {
      args[0] = emitter_reg(e, list_reg);
      emitter_call_result(e, &e->builder->ext.create_record_child_v1, child_reg, args, 1);
    } else {
      emitter_call_result(e, &e->builder->ext.create_obj, child_reg, NULL, 0);
    }
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                   emitter_reg(e, child_reg), MIR_new_int_op(e->builder->ctx, 0)));
    args[0] = emitter_reg(e, list_reg);
    args[1] = emitter_reg(e, child_reg);
    emitter_call_checked(e, &e->builder->ext.add_list_obj, args, 2);
    emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_MOV, emitter_reg(e, child_off_reg),
                                   emitter_reg(e, entry_off_reg)));
    emit_fields_into_object(e, child_reg, child_off_reg, &field->children);
  }
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_ADD, emitter_reg(e, index_reg),
                                 emitter_reg(e, index_reg), MIR_new_int_op(e->builder->ctx, 1)));
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_JMP, emitter_label(e, loop_label)));
  emitter_append(e, done_label);
  emitter_advance_reg(e, off_reg, total_reg);
}

static void emit_object_field_v1(mir_emitter_t *e, MIR_reg_t target_obj_reg, MIR_reg_t off_reg,
                                 const emit_field_t *field, uint32_t field_slot) {
  MIR_reg_t child_reg = emitter_new_reg(e, MIR_T_I64, "__record_child");
  MIR_op_t args[2];
  args[0] = emitter_reg(e, target_obj_reg);
  args[1] = MIR_new_uint_op(e->builder->ctx, field_slot);
  emitter_call_result(e, &e->builder->ext.create_record_field_v1, child_reg, args, 2);
  emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_BEQ, emitter_label(e, e->fail_label),
                                 emitter_reg(e, child_reg), MIR_new_int_op(e->builder->ctx, 0)));
  emit_fields_into_object(e, child_reg, off_reg, &field->children);
}

static void emit_field_code(mir_emitter_t *e, MIR_reg_t target_obj_reg, MIR_reg_t off_reg,
                             const emit_field_t *field, uint32_t field_slot) {
  MIR_item_t field_name_item = NULL;
  if (!e->slot_mode) {
    field_name_item = builder_make_string_data(e->builder, field->name);
    if (field_name_item == NULL) {
      emitter_append(e, MIR_new_insn(e->builder->ctx, MIR_JMP, emitter_label(e, e->fail_label)));
      return;
    }
  }
  if (field->kind == EF_INT || field->kind == EF_U32 || field->kind == EF_I64 ||
      field->kind == EF_U64 || field->kind == EF_DBL ||
      field->kind == EF_BOOL || field->kind == EF_UUID || field->kind == EF_STR ||
      field->kind == EF_FIX_BYTES || field->kind == EF_VAR_BYTES)
    emit_scalar_field(e, target_obj_reg, off_reg, field, field_name_item, field_slot);
  else if (field->kind == EF_LIST_INT || field->kind == EF_LIST_U32 ||
           field->kind == EF_LIST_I64 || field->kind == EF_LIST_U64 || field->kind == EF_LIST_DBL ||
           field->kind == EF_LIST_BOOL || field->kind == EF_LIST_STR || field->kind == EF_LIST_OBJ)
    emit_list_field(e, target_obj_reg, off_reg, field, field_name_item, field_slot);
  else if (field->kind == EF_SET_INT || field->kind == EF_SET_U32 ||
           field->kind == EF_SET_I64 || field->kind == EF_SET_U64 || field->kind == EF_SET_DBL ||
           field->kind == EF_SET_BOOL || field->kind == EF_SET_STR)
    emit_set_field(e, target_obj_reg, off_reg, field, field_name_item, field_slot);
  else if (field->kind == EF_MAP_STR_STR || field->kind == EF_MAP_STR_INT ||
           field->kind == EF_MAP_STR_U32 || field->kind == EF_MAP_STR_I64 ||
           field->kind == EF_MAP_STR_U64 ||
           field->kind == EF_MAP_STR_DBL || field->kind == EF_MAP_STR_BOOL)
    emit_map_field(e, target_obj_reg, off_reg, field, field_name_item, field_slot);
  else if (field->kind == EF_GROUP)
    emit_group_field(e, target_obj_reg, off_reg, field, field_name_item, field_slot);
  else if (field->kind == EF_OBJECT && e->slot_mode)
    emit_object_field_v1(e, target_obj_reg, off_reg, field, field_slot);
}

static void emit_fields_into_object(mir_emitter_t *e, MIR_reg_t target_obj_reg, MIR_reg_t off_reg,
                                    const emit_field_array_t *fields) {
  size_t i;
  for (i = 0; i < fields->count; i++)
    emit_field_code(e, target_obj_reg, off_reg, &fields->items[i], (uint32_t)i);
}

static int generate_message_function(mir_builder_t *builder, Node *message_node, Node *schema_root,
                                     int has_set_bytes) {
  const char *message_name = get_string_val(find_child(message_node, "name"));
  Node *fields_node = find_child(message_node, "fields");
  emit_field_array_t fields = {0};
  emit_field_array_t record_fields = {0};
  schema_validation_context_t validation_ctx = {0};
  MIR_type_t result_type = MIR_T_P;
  MIR_var_t args[] = {{MIR_T_P, "buf", 0}, {MIR_T_I64, "len", 0}};
  MIR_var_t record_args[] = {
      {MIR_T_P, "buf", 0}, {MIR_T_I64, "len", 0}, {MIR_T_P, "record", 0}};
  char func_name[256];
  mir_emitter_t e;
  if (message_name == NULL) return 0;

  /* Validate schema structure before code generation */
  if (!validate_schema_fields(&validation_ctx, fields_node, schema_root, message_name)) {
    set_codec_error(builder->codec, "Schema validation failed for %s: %s", message_name,
                    validation_ctx.error);
    return 0;
  }

  if (!build_fields(&fields, fields_node, schema_root, NULL, has_set_bytes)) {
    emit_field_array_free(&fields);
    return 0;
  }
  if (!validate_fields_api(builder->codec, message_name, &fields)) {
    emit_field_array_free(&fields);
    return 0;
  }
  if (builder->include_record_v1 &&
      (!build_record_fields_v1(&record_fields, fields_node, schema_root, NULL, has_set_bytes) ||
       !validate_fields_api(builder->codec, message_name, &record_fields))) {
    emit_field_array_free(&record_fields);
    emit_field_array_free(&fields);
    return 0;
  }
  snprintf(func_name, sizeof(func_name), "parse_%s", message_name);
  memset(&e, 0, sizeof(e));
  e.builder = builder;
  e.func_item = MIR_new_func_arr(builder->ctx, codec_strdup(builder->codec, func_name), 1,
                                 &result_type, 2, args);
  e.func = e.func_item->u.func;
  e.buf_reg = MIR_reg(builder->ctx, "buf", e.func);
  e.len_reg = MIR_reg(builder->ctx, "len", e.func);
  e.off_reg = emitter_new_reg(&e, MIR_T_I64, "__off");
  e.obj_reg = emitter_new_reg(&e, MIR_T_I64, "__obj");
  e.fail_label = MIR_new_label(builder->ctx);
  e.slot_mode = 0;
  emitter_append(&e, MIR_new_insn(builder->ctx, MIR_MOV, emitter_reg(&e, e.off_reg),
                                  MIR_new_int_op(builder->ctx, 0)));
  emitter_call_result(&e, &builder->ext.create_obj, e.obj_reg, NULL, 0);
  emitter_append(&e, MIR_new_insn(builder->ctx, MIR_BEQ, emitter_label(&e, e.fail_label),
                                  emitter_reg(&e, e.obj_reg), MIR_new_int_op(builder->ctx, 0)));
  emit_fields_into_object(&e, e.obj_reg, e.off_reg, &fields);
  emitter_append(&e, MIR_new_ret_insn(builder->ctx, 1, emitter_reg(&e, e.obj_reg)));
  emitter_append(&e, e.fail_label);
  {
    MIR_op_t free_args[1] = {emitter_reg(&e, e.obj_reg)};
    emitter_call(&e, &builder->ext.free_value, free_args, 1);
  }
  emitter_append(&e, MIR_new_ret_insn(builder->ctx, 1, MIR_new_int_op(builder->ctx, 0)));
  MIR_finish_func(builder->ctx);

  if (builder->include_record_v1) {
    snprintf(func_name, sizeof(func_name), "parse_record_v1_%s", message_name);
    memset(&e, 0, sizeof(e));
    e.builder = builder;
    e.func_item = MIR_new_func_arr(builder->ctx, codec_strdup(builder->codec, func_name), 1,
                                   &result_type, 3, record_args);
    e.func = e.func_item->u.func;
    e.buf_reg = MIR_reg(builder->ctx, "buf", e.func);
    e.len_reg = MIR_reg(builder->ctx, "len", e.func);
    e.obj_reg = MIR_reg(builder->ctx, "record", e.func);
    e.off_reg = emitter_new_reg(&e, MIR_T_I64, "__record_off");
    e.fail_label = MIR_new_label(builder->ctx);
    e.slot_mode = 1;
    emitter_append(&e, MIR_new_insn(builder->ctx, MIR_MOV, emitter_reg(&e, e.off_reg),
                                    MIR_new_int_op(builder->ctx, 0)));
    emitter_append(&e, MIR_new_insn(builder->ctx, MIR_BEQ, emitter_label(&e, e.fail_label),
                                    emitter_reg(&e, e.obj_reg), MIR_new_int_op(builder->ctx, 0)));
    emit_fields_into_object(&e, e.obj_reg, e.off_reg, &record_fields);
    emitter_append(&e, MIR_new_ret_insn(builder->ctx, 1, emitter_reg(&e, e.obj_reg)));
    emitter_append(&e, e.fail_label);
    {
      MIR_op_t free_args[1] = {emitter_reg(&e, e.obj_reg)};
      emitter_call(&e, &builder->ext.free_value, free_args, 1);
    }
    emitter_append(&e, MIR_new_ret_insn(builder->ctx, 1, MIR_new_int_op(builder->ctx, 0)));
    MIR_finish_func(builder->ctx);
  }
  emit_field_array_free(&record_fields);
  emit_field_array_free(&fields);
  return 1;
}

static Node *parse_schema_text_to_root(const char *schema_text, size_t len, const char *path,
                                       char *error_buf, size_t error_size, DataBindError *error) {
  Node *root;
  tbe_error_t err = {0};
  if (schema_text == NULL) {
    if (error_buf != NULL && error_size > 0) snprintf(error_buf, error_size, "Invalid schema text");
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, path, -1, -1, "Invalid schema text");
    return NULL;
  }
  root = create_node_map(NULL);
  if (root == NULL) {
    if (error_buf != NULL && error_size > 0) snprintf(error_buf, error_size, "Out of memory");
    db_error_set(error, DATA_BIND_ERR_OOM, path, -1, -1, "Out of memory");
    return NULL;
  }
  if (parse_schema(schema_text, len, root, &err) != 0) {
    if (error_buf != NULL && error_size > 0)
      snprintf(error_buf, error_size, "Parse error: %s", err.message);
    db_error_set(error, DATA_BIND_ERR_PARSE, path, err.line, err.column, "Parse error: %s",
                 err.message);
    node_free(root);
    return NULL;
  }
  db_error_clear(error);
  return root;
}

static void data_bind_free_contents(DataBind *codec) {
  mir_func_node_t *func_node;
  owned_alloc_node_t *alloc_node;
  if (codec == NULL) return;

  /* Only free func_head if this codec owns it (not from cache) */
  if (!codec->is_cloned) {
    func_node = codec->func_head;
    while (func_node != NULL) {
      mir_func_node_t *next = func_node->next;
      free(func_node->type_name);
      free(func_node);
      func_node = next;
    }
  }
  codec->func_head = NULL;

  alloc_node = codec->owned_allocs;
  while (alloc_node != NULL) {
    owned_alloc_node_t *next = alloc_node->next;
    free(alloc_node->ptr);
    free(alloc_node);
    alloc_node = next;
  }
  codec->owned_allocs = NULL;

  data_bind_record_plans_free_v1(codec);

  /* Handle MIR context based on whether this is a cloned codec */
  if (codec->ctx != NULL) {
    if (codec->is_cloned && codec->schema_hash[0] != '\0') {
      /* Release cache reference - context will be freed by cache when ref_count reaches 0 */
      mir_cache_release(codec->schema_hash, codec->ctx);
    } else if (!codec->is_cloned) {
      /* Owned context not in cache, can be destroyed directly */
      if (codec->mir_gen_initialized) MIR_gen_finish(codec->ctx);
      MIR_finish(codec->ctx);
    }
    codec->ctx = NULL;
    codec->mir_gen_initialized = 0;
  }

  if (codec->schema_root != NULL) {
    node_free(codec->schema_root);
    codec->schema_root = NULL;
  }
}

static int data_bind_add_mir_metadata(DataBind *codec) {
  uint32_t abi_version = DATA_BIND_MIR_PARSER_ABI_VERSION;
  size_t fingerprint_len;

  if (codec->schema_hash[0] == '\0')
    return set_codec_error(codec, "Cannot generate MIR without a schema fingerprint");
  fingerprint_len = strlen(codec->schema_hash) + 1;
  if (MIR_new_data(codec->ctx, DATA_BIND_MIR_ABI_ITEM_NAME, MIR_T_U32, 1, &abi_version) == NULL ||
      MIR_new_data(codec->ctx, DATA_BIND_MIR_SCHEMA_ITEM_NAME, MIR_T_U8, fingerprint_len,
                   codec->schema_hash) == NULL)
    return set_codec_error(codec, "Failed to add MIR parser metadata");
  return 1;
}

static void data_bind_move_mir_data_before_functions(MIR_module_t module) {
  MIR_item_t first_item = DLIST_HEAD(MIR_item_t, module->items);
  MIR_item_t item = DLIST_TAIL(MIR_item_t, module->items);
  while (item != NULL) {
    MIR_item_t previous = DLIST_PREV(MIR_item_t, item);
    if (item->item_type == MIR_data_item) {
      DLIST_REMOVE(MIR_item_t, module->items, item);
      DLIST_PREPEND(MIR_item_t, module->items, item);
    }
    if (item == first_item) break;
    item = previous;
  }
}

static MIR_module_t generate_parser_module(DataBind *codec, int include_record_v1) {
  mir_builder_t builder;
  Node *messages_node;
  size_t i;
  messages_node = find_child(codec->schema_root, "messages");
  if (messages_node == NULL || messages_node->type != NODE_LIST ||
      messages_node->data.list.count == 0) {
    set_codec_error(codec, "No messages found in schema");
    return NULL;
  }
  codec->ctx = MIR_init();
  if (codec->ctx == NULL) return NULL;
  memset(&builder, 0, sizeof(builder));
  builder.codec = codec;
  builder.ctx = codec->ctx;
  builder.module = MIR_new_module(codec->ctx, DATA_BIND_MIR_MODULE_NAME);
  builder.include_record_v1 = include_record_v1 != 0;
  if (!data_bind_add_mir_metadata(codec)) {
    MIR_finish_module(codec->ctx);
    MIR_finish(codec->ctx);
    codec->ctx = NULL;
    return NULL;
  }
  init_externals(&builder);
  for (i = 0; i < messages_node->data.list.count; i++)
    if (!generate_message_function(&builder, messages_node->data.list.items[i], codec->schema_root,
                                   codec->api.set_field_bytes != NULL)) {
      if (codec->error[0] == '\0') set_codec_error(codec, "Failed to generate parser function");
      MIR_finish(codec->ctx);
      codec->ctx = NULL;
      return NULL;
    }
  data_bind_move_mir_data_before_functions(builder.module);
  MIR_finish_module(codec->ctx);
  return builder.module;
}

typedef struct data_bind_mir_external {
  const char *name;
  const void *function_pointer;
  size_t function_pointer_size;
} data_bind_mir_external_t;

static int load_mir_function_external(DataBind *codec,
                                      const data_bind_mir_external_t *external) {
  void *address = NULL;

  if (external->function_pointer_size != sizeof(address))
    return set_codec_error(codec, "Cannot link external function '%s': incompatible pointer size",
                           external->name);

  /* MIR uses void * for both function and data symbols.  Copy the representation here so callers
     do not rely on a non-standard implicit function-to-object pointer conversion. */
  memcpy(&address, external->function_pointer, sizeof(address));
  if (address == NULL)
    return set_codec_error(codec, "Cannot link external function '%s': null address",
                           external->name);

  MIR_load_external(codec->ctx, external->name, address);
  return 1;
}

static int link_module(DataBind *codec, MIR_module_t module) {
  data_bind_runtime_api_t api = codec->api;
  char *(*read_varstr_fn)(const uint8_t *, size_t, size_t) = data_bind_read_varstring;
  void (*free_fn)(void *) = free;
  DataBindValue *(*record_create_child_fn)(DataBindValue *) = record_create_child_v1;
  DataBindValue *(*record_create_field_fn)(DataBindValue *, uint32_t) = record_create_field_v1;
  int (*record_set_slot_int_fn)(DataBindValue *, uint32_t, int32_t) = record_set_slot_int_v1;
  int (*record_set_slot_u32_fn)(DataBindValue *, uint32_t, uint32_t) =
      record_set_slot_uint32_v1;
  int (*record_set_slot_i64_fn)(DataBindValue *, uint32_t, int64_t) = record_set_slot_int64_v1;
  int (*record_set_slot_u64_fn)(DataBindValue *, uint32_t, uint64_t) = record_set_slot_uint64_v1;
  int (*record_set_slot_double_fn)(DataBindValue *, uint32_t, double) = record_set_slot_double_v1;
  int (*record_set_slot_bool_fn)(DataBindValue *, uint32_t, int) = record_set_slot_bool_v1;
  int (*record_set_slot_string_fn)(DataBindValue *, uint32_t, const char *) =
      record_set_slot_string_v1;
  int (*record_set_slot_bytes_fn)(DataBindValue *, uint32_t, const uint8_t *, size_t) =
      record_set_slot_bytes_v1;
  int (*record_set_slot_uuid_fn)(DataBindValue *, uint32_t, const uint8_t *) =
      record_set_slot_uuid_v1;
  int (*record_set_slot_list_fn)(DataBindValue *, uint32_t, DataBindValue *) =
      record_set_slot_list_v1;
  int (*record_set_slot_set_fn)(DataBindValue *, uint32_t, DataBindValue *) =
      record_set_slot_set_v1;
  int (*record_set_slot_map_fn)(DataBindValue *, uint32_t, DataBindValue *) =
      record_set_slot_map_v1;
  size_t i;

  if (api.set_field_uint32 == NULL) api.set_field_uint32 = set_u32_noop;
  if (api.set_field_int64 == NULL) api.set_field_int64 = set_i64_noop;
  if (api.set_field_uint64 == NULL) api.set_field_uint64 = set_u64_noop;
  if (api.set_field_bool == NULL) api.set_field_bool = set_bool_noop;
  if (api.set_field_bytes == NULL) api.set_field_bytes = set_bytes_noop;
  if (api.set_field_uuid == NULL) api.set_field_uuid = set_uuid_noop;
  if (api.create_list == NULL) api.create_list = container_noop;
  if (api.add_list_item_int == NULL) api.add_list_item_int = add_i32_noop;
  if (api.add_list_item_uint32 == NULL) api.add_list_item_uint32 = add_u32_noop;
  if (api.add_list_item_int64 == NULL) api.add_list_item_int64 = add_i64_noop;
  if (api.add_list_item_uint64 == NULL) api.add_list_item_uint64 = add_u64_noop;
  if (api.add_list_item_double == NULL) api.add_list_item_double = add_dbl_noop;
  if (api.add_list_item_bool == NULL) api.add_list_item_bool = add_bool_noop;
  if (api.add_list_item_string == NULL) api.add_list_item_string = add_str_noop;
  if (api.add_list_item_object == NULL) api.add_list_item_object = add_obj_noop;
  if (api.set_field_list == NULL) api.set_field_list = set_container_noop;
  if (api.create_set == NULL) api.create_set = container_noop;
  if (api.add_set_item_int == NULL) api.add_set_item_int = add_i32_noop;
  if (api.add_set_item_uint32 == NULL) api.add_set_item_uint32 = add_u32_noop;
  if (api.add_set_item_int64 == NULL) api.add_set_item_int64 = add_i64_noop;
  if (api.add_set_item_uint64 == NULL) api.add_set_item_uint64 = add_u64_noop;
  if (api.add_set_item_double == NULL) api.add_set_item_double = add_dbl_noop;
  if (api.add_set_item_bool == NULL) api.add_set_item_bool = add_bool_noop;
  if (api.add_set_item_string == NULL) api.add_set_item_string = add_str_noop;
  if (api.set_field_set == NULL) api.set_field_set = set_container_noop;
  if (api.create_map == NULL) api.create_map = container_noop;
  if (api.add_map_entry_string_string == NULL)
    api.add_map_entry_string_string = add_map_str_str_noop;
  if (api.add_map_entry_string_int == NULL) api.add_map_entry_string_int = add_map_str_int_noop;
  if (api.add_map_entry_string_uint32 == NULL)
    api.add_map_entry_string_uint32 = add_map_str_u32_noop;
  if (api.add_map_entry_string_int64 == NULL)
    api.add_map_entry_string_int64 = add_map_str_i64_noop;
  if (api.add_map_entry_string_uint64 == NULL)
    api.add_map_entry_string_uint64 = add_map_str_u64_noop;
  if (api.add_map_entry_string_double == NULL)
    api.add_map_entry_string_double = add_map_str_dbl_noop;
  if (api.add_map_entry_string_bool == NULL)
    api.add_map_entry_string_bool = add_map_str_bool_noop;
  if (api.set_field_map == NULL) api.set_field_map = set_container_noop;

#define MIR_FUNCTION_EXTERNAL(external_name, function_pointer)                                      \
  { external_name, &(function_pointer), sizeof(function_pointer) }
  {
    const data_bind_mir_external_t externals[] = {
        MIR_FUNCTION_EXTERNAL("create_obj", api.create_object),
        MIR_FUNCTION_EXTERNAL("free_value", api.free_value),
        MIR_FUNCTION_EXTERNAL("set_int", api.set_field_int),
        MIR_FUNCTION_EXTERNAL("set_uint32", api.set_field_uint32),
        MIR_FUNCTION_EXTERNAL("set_int64", api.set_field_int64),
        MIR_FUNCTION_EXTERNAL("set_uint64", api.set_field_uint64),
        MIR_FUNCTION_EXTERNAL("set_dbl", api.set_field_double),
        MIR_FUNCTION_EXTERNAL("set_bool", api.set_field_bool),
        MIR_FUNCTION_EXTERNAL("set_str", api.set_field_string),
        MIR_FUNCTION_EXTERNAL("set_bytes", api.set_field_bytes),
        MIR_FUNCTION_EXTERNAL("set_uuid", api.set_field_uuid),
        MIR_FUNCTION_EXTERNAL("create_list", api.create_list),
        MIR_FUNCTION_EXTERNAL("add_list_int", api.add_list_item_int),
        MIR_FUNCTION_EXTERNAL("add_list_uint32", api.add_list_item_uint32),
        MIR_FUNCTION_EXTERNAL("add_list_int64", api.add_list_item_int64),
        MIR_FUNCTION_EXTERNAL("add_list_uint64", api.add_list_item_uint64),
        MIR_FUNCTION_EXTERNAL("add_list_dbl", api.add_list_item_double),
        MIR_FUNCTION_EXTERNAL("add_list_bool", api.add_list_item_bool),
        MIR_FUNCTION_EXTERNAL("add_list_str", api.add_list_item_string),
        MIR_FUNCTION_EXTERNAL("add_list_obj", api.add_list_item_object),
        MIR_FUNCTION_EXTERNAL("set_list", api.set_field_list),
        MIR_FUNCTION_EXTERNAL("create_set", api.create_set),
        MIR_FUNCTION_EXTERNAL("add_set_int", api.add_set_item_int),
        MIR_FUNCTION_EXTERNAL("add_set_uint32", api.add_set_item_uint32),
        MIR_FUNCTION_EXTERNAL("add_set_int64", api.add_set_item_int64),
        MIR_FUNCTION_EXTERNAL("add_set_uint64", api.add_set_item_uint64),
        MIR_FUNCTION_EXTERNAL("add_set_dbl", api.add_set_item_double),
        MIR_FUNCTION_EXTERNAL("add_set_bool", api.add_set_item_bool),
        MIR_FUNCTION_EXTERNAL("add_set_str", api.add_set_item_string),
        MIR_FUNCTION_EXTERNAL("set_set", api.set_field_set),
        MIR_FUNCTION_EXTERNAL("create_map", api.create_map),
        MIR_FUNCTION_EXTERNAL("add_map_str_str", api.add_map_entry_string_string),
        MIR_FUNCTION_EXTERNAL("add_map_str_int", api.add_map_entry_string_int),
        MIR_FUNCTION_EXTERNAL("add_map_str_uint32", api.add_map_entry_string_uint32),
        MIR_FUNCTION_EXTERNAL("add_map_str_int64", api.add_map_entry_string_int64),
        MIR_FUNCTION_EXTERNAL("add_map_str_uint64", api.add_map_entry_string_uint64),
        MIR_FUNCTION_EXTERNAL("add_map_str_dbl", api.add_map_entry_string_double),
        MIR_FUNCTION_EXTERNAL("add_map_str_bool", api.add_map_entry_string_bool),
        MIR_FUNCTION_EXTERNAL("set_map", api.set_field_map),
        MIR_FUNCTION_EXTERNAL("record_create_child_v1", record_create_child_fn),
        MIR_FUNCTION_EXTERNAL("record_create_field_v1", record_create_field_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_int_v1", record_set_slot_int_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_uint32_v1", record_set_slot_u32_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_int64_v1", record_set_slot_i64_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_uint64_v1", record_set_slot_u64_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_double_v1", record_set_slot_double_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_bool_v1", record_set_slot_bool_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_string_v1", record_set_slot_string_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_bytes_v1", record_set_slot_bytes_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_uuid_v1", record_set_slot_uuid_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_list_v1", record_set_slot_list_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_set_v1", record_set_slot_set_fn),
        MIR_FUNCTION_EXTERNAL("record_set_slot_map_v1", record_set_slot_map_fn),
        MIR_FUNCTION_EXTERNAL("read_varstr", read_varstr_fn),
        MIR_FUNCTION_EXTERNAL("free", free_fn),
    };
#undef MIR_FUNCTION_EXTERNAL

    MIR_load_module(codec->ctx, module);
    for (i = 0; i < sizeof(externals) / sizeof(externals[0]); ++i)
      if (!load_mir_function_external(codec, &externals[i])) return 0;
  }
  MIR_gen_init(codec->ctx);
  codec->mir_gen_initialized = 1;
  MIR_link(codec->ctx, MIR_set_gen_interface, NULL);
  return 1;
}

static MIR_module_t data_bind_validate_loaded_mir(DataBind *codec) {
  MIR_module_t module;
  MIR_item_t item;
  int found_abi = 0;
  int found_fingerprint = 0;

  module = DLIST_HEAD(MIR_module_t, *MIR_get_module_list(codec->ctx));
  if (module == NULL || DLIST_NEXT(MIR_module_t, module) != NULL)
    return set_codec_error(codec, "MIR artifact must contain exactly one module"), NULL;
  if (module->name == NULL || strcmp(module->name, DATA_BIND_MIR_MODULE_NAME) != 0)
    return set_codec_error(codec, "Unexpected MIR module name '%s'",
                           module->name != NULL ? module->name : "<null>"),
           NULL;

  for (item = DLIST_HEAD(MIR_item_t, module->items); item != NULL;
       item = DLIST_NEXT(MIR_item_t, item)) {
    MIR_data_t data;
    if (item->item_type != MIR_data_item) continue;
    data = item->u.data;
    if (data == NULL || data->name == NULL) continue;
    if (strcmp(data->name, DATA_BIND_MIR_ABI_ITEM_NAME) == 0) {
      uint32_t abi_version = 0;
      if (found_abi || data->el_type != MIR_T_U32 || data->nel != 1)
        return set_codec_error(codec, "Invalid MIR parser ABI metadata"), NULL;
      memcpy(&abi_version, data->u.els, sizeof(abi_version));
      if (abi_version != DATA_BIND_MIR_PARSER_ABI_VERSION)
        return set_codec_error(codec, "Unsupported MIR parser ABI: %u", abi_version), NULL;
      found_abi = 1;
    } else if (strcmp(data->name, DATA_BIND_MIR_SCHEMA_ITEM_NAME) == 0) {
      size_t expected_len = strlen(codec->schema_hash) + 1;
      if (found_fingerprint || data->el_type != MIR_T_U8 || data->nel != expected_len ||
          memcmp(data->u.els, codec->schema_hash, expected_len) != 0)
        return set_codec_error(codec, "MIR artifact does not match the supplied schema"), NULL;
      found_fingerprint = 1;
    }
  }
  if (!found_abi || !found_fingerprint)
    return set_codec_error(codec, "MIR artifact is missing DataBind metadata"), NULL;
  return module;
}

static int register_parse_functions(DataBind *codec, MIR_module_t module) {
  Node *messages_node = find_child(codec->schema_root, "messages");
  size_t i;
  if (messages_node == NULL || messages_node->type != NODE_LIST)
    return set_codec_error(codec, "Schema contains no message list");
  for (i = 0; i < messages_node->data.list.count; i++) {
    Node *msg = messages_node->data.list.items[i];
    const char *msg_name = get_string_val(find_child(msg, "name"));
    char func_name[256];
    char record_func_name[256];
    MIR_item_t item;
    void *parse_fn = NULL;
    void *parse_record_fn = NULL;
    if (msg_name == NULL) return set_codec_error(codec, "Schema message has no name");
    if (snprintf(func_name, sizeof(func_name), "parse_%s", msg_name) >= (int)sizeof(func_name) ||
        snprintf(record_func_name, sizeof(record_func_name), "parse_record_v1_%s", msg_name) >=
            (int)sizeof(record_func_name))
      return set_codec_error(codec, "Parser function name is too long for message '%s'", msg_name);
    for (item = DLIST_HEAD(MIR_item_t, module->items); item != NULL;
         item = DLIST_NEXT(MIR_item_t, item)) {
      if (item->item_type != MIR_func_item) continue;
      if (strcmp(item->u.func->name, func_name) == 0) parse_fn = item->addr;
      else if (strcmp(item->u.func->name, record_func_name) == 0) parse_record_fn = item->addr;
    }
    if (parse_fn == NULL)
      return set_codec_error(codec, "MIR module is missing parser function '%s'", func_name);
    {
      mir_func_node_t *node = (mir_func_node_t *)calloc(1, sizeof(*node));
      if (node == NULL) return set_codec_error(codec, "Out of memory registering '%s'", func_name);
      /* type_name is freed individually in data_bind_free; not tracked by codec_alloc */
      node->type_name = strdup(msg_name);
      if (node->type_name == NULL) {
        free(node);
        return set_codec_error(codec, "Out of memory registering '%s'", func_name);
      }
      node->parse_fn = parse_fn;
      node->parse_record_fn = parse_record_fn;
      node->next = codec->func_head;
      codec->func_head = node;
    }
  }
  return 1;
}

static DataBindStatus data_bind_finish_codec(DataBind *codec, DataBindError *error,
                                              const char *schema_text, size_t schema_len) {
  MIR_module_t module;
  mir_cache_entry_t *cache_entry = NULL;

  if (!data_bind_record_plans_build_v1(codec))
    return db_error_set(error, DATA_BIND_ERR_OOM, NULL, -1, -1,
                        "Out of memory creating binary Record layouts");

  /* Compute schema hash for caching */
  if (schema_text != NULL && schema_len > 0) {
    compute_schema_hash(schema_text, schema_len, codec->schema_hash);
    cache_entry = mir_cache_find(codec->schema_hash);
  }

  /* Use cached MIR context if available */
  if (cache_entry != NULL && cache_entry->shared_ctx != NULL) {
    codec->ctx = cache_entry->shared_ctx;
    codec->func_head = cache_entry->func_head;
    codec->is_cloned = 1;
    cache_entry->ref_count++;
    db_error_clear(error);
    codec->error[0] = '\0';
    codec->binary_error[0] = '\0';
    return DATA_BIND_OK;
  }

  /* Generate new MIR module */
  module = generate_parser_module(codec, 1);
  if (module == NULL) {
    snprintf(codec->binary_error, sizeof(codec->binary_error), "%s",
             codec->error[0] != '\0' ? codec->error : "Failed to generate parser module");
    codec->error[0] = '\0';
    db_error_clear(error);
    return DATA_BIND_OK;
  }
  if (!link_module(codec, module)) {
    snprintf(codec->binary_error, sizeof(codec->binary_error), "%s",
             codec->error[0] != '\0' ? codec->error : "Failed to link parser module");
    codec->error[0] = '\0';
    db_error_clear(error);
    return DATA_BIND_OK;
  }
  if (!register_parse_functions(codec, module)) {
    snprintf(codec->binary_error, sizeof(codec->binary_error), "%s",
             codec->error[0] != '\0' ? codec->error : "Failed to register parser functions");
    codec->error[0] = '\0';
    db_error_clear(error);
    return DATA_BIND_OK;
  }

  /* Cache the MIR context if hashing was successful */
  if (codec->schema_hash[0] != '\0') {
    cache_entry = mir_cache_insert(codec->schema_hash, codec->ctx, codec->func_head);
    if (cache_entry != NULL) {
      codec->is_cloned = 1; /* Mark as using cached context */
    } else {
      codec->is_cloned = 0; /* Cache failed, codec owns the context */
    }
  } else {
    codec->is_cloned = 0; /* No hash, codec owns the context */
  }

  db_error_clear(error);
  codec->error[0] = '\0';
  codec->binary_error[0] = '\0';
  return DATA_BIND_OK;
}

static DataBindStatus
data_bind_create_with_api_from_root(Node *schema_root, const data_bind_runtime_api_t *api,
                                    DataBind **out_codec, DataBindError *error,
                                    const char *schema_text, size_t schema_len) {
  DataBind *codec;
  DataBindStatus status;
  if (out_codec != NULL) *out_codec = NULL;
  if (api == NULL || api->create_object == NULL || api->free_value == NULL ||
      api->set_field_int == NULL || api->set_field_double == NULL || api->set_field_string == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1, "Invalid runtime API");
  if (schema_root == NULL || out_codec == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid codec create arguments");
  codec = (DataBind *)calloc(1, sizeof(*codec));
  if (codec == NULL) {
    node_free(schema_root);
    return db_error_set(error, DATA_BIND_ERR_OOM, NULL, -1, -1, "Out of memory");
  }
  codec->api = *api;
  codec->schema_root = schema_root;
  codec->is_cloned = 0;
  codec->schema_hash[0] = '\0';
  status = data_bind_finish_codec(codec, error, schema_text, schema_len);
  if (status != DATA_BIND_OK) {
    data_bind_free(codec);
    return status;
  }
  *out_codec = codec;
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_create_with_api(const char *schema_path,
                                                const data_bind_runtime_api_t *api,
                                                DataBind **out_codec, DataBindError *error) {
  Node *schema_root;
  turbo_fs_buf_t schema_buf = {NULL, 0};
  DataBindStatus status;

  if (out_codec != NULL) *out_codec = NULL;

  /* Load schema file to get both parsed AST and raw text for hashing */
  if (turbo_fs_read_file(schema_path, &schema_buf) != 0) {
    return db_error_set(error, DATA_BIND_ERR_IO, schema_path, -1, -1, "Cannot read schema: %s",
                        schema_path);
  }

  schema_root =
      parse_schema_text_to_root(schema_buf.base, schema_buf.len, schema_path, NULL, 0, error);
  if (schema_root == NULL) {
    turbo_fs_buf_free(&schema_buf);
    return error != NULL && error->code != DATA_BIND_OK ? error->code : DATA_BIND_ERR_SCHEMA;
  }

  status = data_bind_create_with_api_from_root(schema_root, api, out_codec, error, schema_buf.base,
                                               schema_buf.len);
  turbo_fs_buf_free(&schema_buf);
  return status;
}

DataBindStatus data_bind_create(const char *schema_path, DataBind **out_codec,
                                DataBindError *error) {
  return data_bind_create_with_api(schema_path, &DYNAMIC_VALUE_API, out_codec, error);
}

DataBindStatus data_bind_create_from_text(const char *schema_text, size_t len, DataBind **out_codec,
                                          DataBindError *error) {
  Node *schema_root;
  if (out_codec != NULL) *out_codec = NULL;
  schema_root = parse_schema_text_to_root(schema_text, len, NULL, NULL, 0, error);
  if (schema_root == NULL)
    return error != NULL && error->code != DATA_BIND_OK ? error->code : DATA_BIND_ERR_SCHEMA;
  return data_bind_create_with_api_from_root(schema_root, &DYNAMIC_VALUE_API, out_codec, error,
                                             schema_text, len);
}

static DataBindStatus data_bind_create_from_mir_artifact(
    const char *schema_text, size_t schema_len, const void *artifact, size_t artifact_len,
    int binary_input, DataBind **out_codec, DataBindError *error) {
  Node *schema_root;
  DataBind *codec;
  MIR_module_t module;
  char *mir_text = NULL;

  if (out_codec != NULL) *out_codec = NULL;
  if (schema_text == NULL || schema_len == 0 || artifact == NULL || artifact_len == 0 ||
      out_codec == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid MIR codec arguments");
  if (!binary_input && memchr(artifact, '\0', artifact_len) != NULL)
    return db_error_set(error, DATA_BIND_ERR_PARSE, NULL, -1, -1,
                        "Textual MIR contains an embedded NUL byte");

  schema_root = parse_schema_text_to_root(schema_text, schema_len, NULL, NULL, 0, error);
  if (schema_root == NULL)
    return error != NULL && error->code != DATA_BIND_OK ? error->code : DATA_BIND_ERR_SCHEMA;
  codec = (DataBind *)calloc(1, sizeof(*codec));
  if (codec == NULL) {
    node_free(schema_root);
    return db_error_set(error, DATA_BIND_ERR_OOM, NULL, -1, -1, "Out of memory");
  }
  codec->api = DYNAMIC_VALUE_API;
  codec->schema_root = schema_root;
  compute_schema_hash(schema_text, schema_len, codec->schema_hash);
  codec->ctx = MIR_init();
  if (codec->ctx == NULL) {
    data_bind_free(codec);
    return db_error_set(error, DATA_BIND_ERR_OOM, NULL, -1, -1,
                        "Failed to initialize MIR context");
  }

  if (binary_input) {
    data_bind_bmir_input_t input = {(const uint8_t *)artifact, artifact_len, 0};
    if (g_data_bind_bmir_input != NULL) {
      data_bind_free(codec);
      return db_error_set(error, DATA_BIND_ERR_RUNTIME, NULL, -1, -1,
                          "Nested BMIR loading is not supported");
    }
    g_data_bind_bmir_input = &input;
    MIR_read_with_func(codec->ctx, data_bind_bmir_read_byte);
    g_data_bind_bmir_input = NULL;
  } else {
    if (artifact_len == SIZE_MAX) {
      data_bind_free(codec);
      return db_error_set(error, DATA_BIND_ERR_OOM, NULL, -1, -1,
                          "Textual MIR is too large");
    }
    mir_text = (char *)malloc(artifact_len + 1);
    if (mir_text == NULL) {
      data_bind_free(codec);
      return db_error_set(error, DATA_BIND_ERR_OOM, NULL, -1, -1,
                          "Out of memory loading textual MIR");
    }
    memcpy(mir_text, artifact, artifact_len);
    mir_text[artifact_len] = '\0';
    MIR_scan_string(codec->ctx, mir_text);
    free(mir_text);
  }

  module = data_bind_validate_loaded_mir(codec);
  if (module == NULL) {
    DataBindStatus status = db_error_set(error, DATA_BIND_ERR_SCHEMA, NULL, -1, -1, "%s",
                                         codec->error[0] != '\0' ? codec->error
                                                                  : "Invalid MIR artifact");
    data_bind_free(codec);
    return status;
  }
  if (!link_module(codec, module) || !register_parse_functions(codec, module)) {
    DataBindStatus status = db_error_set(error, DATA_BIND_ERR_RUNTIME, NULL, -1, -1, "%s",
                                         codec->error[0] != '\0' ? codec->error
                                                                  : "Failed to link MIR artifact");
    data_bind_free(codec);
    return status;
  }

  db_error_clear(error);
  *out_codec = codec;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_create_from_mir(const char *schema_text, size_t schema_len,
                                         const char *mir_text, size_t mir_len,
                                         DataBind **out_codec, DataBindError *error) {
  return data_bind_create_from_mir_artifact(schema_text, schema_len, mir_text, mir_len, 0,
                                            out_codec, error);
}

DataBindStatus data_bind_create_from_bmir(const char *schema_text, size_t schema_len,
                                          const void *bmir_data, size_t bmir_len,
                                          DataBind **out_codec, DataBindError *error) {
  return data_bind_create_from_mir_artifact(schema_text, schema_len, bmir_data, bmir_len, 1,
                                            out_codec, error);
}

void data_bind_free(DataBind *codec) {
  if (codec == NULL) return;
  data_bind_free_contents(codec);
  free(codec);
}

void data_bind_set_cache_enabled(int enabled) { g_mir_cache_enabled = enabled != 0; }

void data_bind_clear_cache(void) {
  mir_cache_entry_t **entry_ptr = &g_mir_cache_head;
  while (*entry_ptr != NULL) {
    mir_cache_entry_t *entry = *entry_ptr;
    entry->evicted = 1;
    if (entry->ref_count <= 0) {
      *entry_ptr = entry->next;
      mir_cache_entry_destroy(entry);
    } else {
      entry_ptr = &entry->next;
    }
  }
}

void data_bind_set_value_pool_enabled(int enabled) {
  DataBindValue *nodes[VALUE_POOL_SIZE];
  size_t node_count = 0;
  size_t i;
  turbo_once(&g_value_pool_once, value_pool_init_once);
  turbo_mutex_lock(&g_value_pool_control_mutex);

  if (enabled) {
    int state = atomic_load_explicit(&g_value_pool_state, memory_order_relaxed);
    if (state != VALUE_POOL_ENABLED) {
      if (state == VALUE_POOL_DISABLED) {
        atomic_store_explicit(&g_value_pool_ready_mask, 0, memory_order_relaxed);
        for (i = 0; i < VALUE_POOL_SIZE; ++i) {
          DataBindValue *expected = VALUE_POOL_CLOSED_SLOT;
          atomic_compare_exchange_strong_explicit(&g_value_pool_slots[i], &expected, NULL,
                                                  memory_order_release, memory_order_relaxed);
        }
      }
      atomic_store_explicit(&g_value_pool_state, VALUE_POOL_ENABLED, memory_order_release);
    }
  } else {
    atomic_store_explicit(&g_value_pool_state, VALUE_POOL_DISABLED, memory_order_release);
    for (i = 0; i < VALUE_POOL_SIZE; ++i) {
      DataBindValue *node = atomic_exchange_explicit(&g_value_pool_slots[i], VALUE_POOL_CLOSED_SLOT,
                                                     memory_order_acquire);
      if (node != NULL && node != VALUE_POOL_CLOSED_SLOT) nodes[node_count++] = node;
    }
    atomic_store_explicit(&g_value_pool_ready_mask, 0, memory_order_relaxed);
  }
  turbo_mutex_unlock(&g_value_pool_control_mutex);

  for (i = 0; i < node_count; ++i)
    free(nodes[i]);
}

void data_bind_get_value_pool_stats(size_t *allocated, size_t *reused) {
  if (allocated != NULL)
    *allocated = atomic_load_explicit(&g_value_pool_allocated_count, memory_order_relaxed);
  if (reused != NULL)
    *reused = atomic_load_explicit(&g_value_pool_reused_count, memory_order_relaxed);
}

static DataBindStatus data_bind_emit_file_to_writer(FILE *file, DataBindWriteFn write, void *user,
                                                    DataBindError *error) {
  unsigned char buf[4096];
  size_t n;
  if (fflush(file) != 0 || fseek(file, 0, SEEK_SET) != 0)
    return db_error_set(error, DATA_BIND_ERR_IO, NULL, -1, -1, "Failed to rewind MIR output");
  while ((n = fread(buf, 1, sizeof(buf), file)) > 0) {
    if (write(buf, n, user) != 0)
      return db_error_set(error, DATA_BIND_ERR_IO, NULL, -1, -1, "MIR write callback failed");
  }
  if (ferror(file))
    return db_error_set(error, DATA_BIND_ERR_IO, NULL, -1, -1, "Failed to read MIR output");
  return DATA_BIND_OK;
}

DataBindStatus data_bind_generate_mir(const char *schema_path, DataBindWriteFn write, void *user,
                                      int binary_output, DataBindError *error) {
  DataBind codec;
  MIR_module_t module = NULL;
  FILE *tmp = NULL;
  turbo_fs_buf_t schema_buf = {NULL, 0};
  DataBindStatus status = DATA_BIND_ERR_RUNTIME;

  db_error_clear(error);
  if (schema_path == NULL || write == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, schema_path, -1, -1,
                        "Invalid MIR output arguments");

  memset(&codec, 0, sizeof(codec));
  codec.api = MIR_OUTPUT_API;
  if (turbo_fs_read_file(schema_path, &schema_buf) != 0) {
    status = db_error_set(error, DATA_BIND_ERR_IO, schema_path, -1, -1,
                          "Cannot read schema: %s", schema_path);
    goto cleanup;
  }
  codec.schema_root = parse_schema_text_to_root(schema_buf.base, schema_buf.len, schema_path,
                                                codec.error, sizeof(codec.error), error);
  if (codec.schema_root == NULL) goto cleanup;
  compute_schema_hash(schema_buf.base, schema_buf.len, codec.schema_hash);

  module = generate_parser_module(&codec, 0);
  if (module == NULL) {
    status = db_error_set(error, DATA_BIND_ERR_SCHEMA, schema_path, -1, -1, "%s",
                          codec.error[0] != '\0' ? codec.error : "MIR generation failed");
    goto cleanup;
  }

  tmp = tmpfile();
  if (tmp == NULL) {
    status = db_error_set(error, DATA_BIND_ERR_IO, schema_path, -1, -1,
                          "Failed to create temporary MIR output");
    goto cleanup;
  }
  if (binary_output) MIR_write_module(codec.ctx, tmp, module);
  else MIR_output_module(codec.ctx, tmp, module);
  if (ferror(tmp)) {
    status = db_error_set(error, DATA_BIND_ERR_IO, schema_path, -1, -1, "MIR output failed");
    goto cleanup;
  }
  status = data_bind_emit_file_to_writer(tmp, write, user, error);

cleanup:
  if (tmp != NULL) fclose(tmp);
  data_bind_free_contents(&codec);
  turbo_fs_buf_free(&schema_buf);
  return status;
}

DataBindStatus data_bind_parse(DataBind *codec, const char *type_name, const uint8_t *buf,
                               size_t len, DataBindValue **out_value, DataBindError *error) {
  mir_func_node_t *node;
  DataBindValue *(*parse_fn)(const uint8_t *, int64_t);
  DataBindValue *result;
  char error_path[64];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || type_name == NULL || buf == NULL || out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid binary bind arguments");
  if (codec->func_head == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "binary", NULL);
    return db_error_set(error, DATA_BIND_ERR_RUNTIME, error_path, -1, -1, "%s",
                        codec->binary_error[0] != '\0'
                            ? codec->binary_error
                            : "Binary parser is unavailable for this schema");
  }
  for (node = codec->func_head; node != NULL; node = node->next) {
    if (strcmp(node->type_name, type_name) == 0) {
      parse_fn = (DataBindValue * (*)(const uint8_t *, int64_t)) node->parse_fn;
      codec->error[0] = '\0';
      g_dynamic_runtime_oom = 0;
      result = parse_fn(buf, (int64_t)len);
      if (result == NULL) {
        db_error_format_path(error_path, sizeof(error_path), "binary", "parse failed");
        if (g_dynamic_runtime_oom)
          return db_error_set(error, DATA_BIND_ERR_OOM, error_path, -1, -1,
                              "Out of memory binding binary type: %s", type_name);
        return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1,
                            "Binary bind failed for type: %s", type_name);
      }
      *out_value = result;
      db_error_clear(error);
      return DATA_BIND_OK;
    }
  }
  return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_NOT_FOUND, "Type not found: %s",
                        type_name);
}

static DataBindStatus data_bind_parse_record_v1(DataBind *codec, const char *type_name,
                                                 const uint8_t *buf, size_t len,
                                                 DataBindValue **out_value,
                                                 DataBindError *error) {
  data_bind_record_plan_t *plan;
  mir_func_node_t *node;
  DataBindValue *record;
  DataBindValue *result;
  DataBindValue *(*parse_fn)(const uint8_t *, int64_t, DataBindValue *);
  char error_path[64];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || type_name == NULL || buf == NULL || out_value == NULL || len > INT64_MAX)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid binary Record bind arguments");
  plan = data_bind_record_plan_find_v1(codec, type_name);
  if (plan == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_NOT_FOUND, "Type not found: %s",
                          type_name);
  for (node = codec->func_head; node != NULL; node = node->next)
    if (strcmp(node->type_name, type_name) == 0) break;
  if (node == NULL || node->parse_record_fn == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "binary", NULL);
    return db_error_set(error, DATA_BIND_ERR_RUNTIME, error_path, -1, -1,
                        "Binary Record parser v1 is unavailable for type: %s", type_name);
  }
  g_dynamic_runtime_oom = 0;
  record = record_create_from_layout_v1(plan->layout);
  if (record == NULL)
    return db_error_set(error, DATA_BIND_ERR_OOM, "binary", -1, -1,
                        "Out of memory creating binary Record: %s", type_name);
  parse_fn = (DataBindValue * (*)(const uint8_t *, int64_t, DataBindValue *))node->parse_record_fn;
  result = parse_fn(buf, (int64_t)len, record);
  if (result == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "binary", "record parse failed");
    if (g_dynamic_runtime_oom)
      return db_error_set(error, DATA_BIND_ERR_OOM, error_path, -1, -1,
                          "Out of memory binding binary Record type: %s", type_name);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1,
                        "Binary Record bind failed for type: %s", type_name);
  }
  record_clear_layout_v1(result);
  *out_value = result;
  db_error_clear(error);
  return DATA_BIND_OK;
}

static int data_bind_stream_json_path_is_root_array(const char *path) {
  return path == NULL || path[0] == '\0' || strcmp(path, "$[*]") == 0;
}

static int data_bind_stream_xml_name_char(char ch) {
  return isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == ':' || ch == '.';
}

static int data_bind_stream_xml_path_is_simple_descendant(const char *path) {
  const char *name;
  size_t len;
  size_t i;
  if (path == NULL || path[0] != '/' || path[1] != '/' || path[2] == '\0') return 0;
  name = path + 2;
  len = strlen(name);
  for (i = 0; i < len; ++i) {
    if (!data_bind_stream_xml_name_char(name[i])) return 0;
  }
  return 1;
}

static char *data_bind_stream_xml_target_from_path(const char *path) {
  const char *name;
  size_t len;
  char *target;
  if (!data_bind_stream_xml_path_is_simple_descendant(path)) return NULL;
  name = path + 2;
  len = strlen(name);
  target = (char *)malloc(len + 1);
  if (target == NULL) return NULL;
  memcpy(target, name, len + 1);
  return target;
}

static int data_bind_stream_xml_can_bind_incrementally(const char *path) {
  return data_bind_stream_xml_path_is_simple_descendant(path);
}

static void data_bind_stream_error_msg(data_bind_stream_t *parser, const char *message) {
  if (parser == NULL || message == NULL) return;
  snprintf(parser->stream_error, sizeof(parser->stream_error), "%s", message);
}

static int data_bind_stream_emit_record(data_bind_stream_t *parser, const DataBindValue *record) {
  DataBindRecordAction action;
  if (parser == NULL || record == NULL || parser->record_callback == NULL ||
      parser->record_callback_stopped) {
    return 0;
  }
  action = parser->record_callback(parser->record_callback_user, record,
                                   parser->record_callback_index++);
  if (action == DATA_BIND_RECORD_CONTINUE) return 0;
  if (action == DATA_BIND_RECORD_STOP) {
    parser->record_callback_stopped = 1;
    return 0;
  }
  parser->record_callback_failed = 1;
  data_bind_stream_error_msg(parser, "Record callback failed");
  return -1;
}

static DataBindStatus data_bind_stream_emit_result(data_bind_stream_t *parser,
                                                   const DataBindValue *value,
                                                   DataBindError *error) {
  size_t i;
  if (parser == NULL || value == NULL || parser->record_callback == NULL ||
      parser->record_callback_stopped) {
    return DATA_BIND_OK;
  }
  if (data_bind_value_kind(value) == DATA_BIND_VALUE_LIST) {
    for (i = 0; i < data_bind_value_count(value); ++i) {
      if (data_bind_stream_emit_record(parser, data_bind_value_at(value, i)) != 0) {
        return db_error_set(error, DATA_BIND_ERR_RUNTIME, "record_callback", -1, -1,
                            "Record callback failed at index %llu",
                            (unsigned long long)(parser->record_callback_index - 1));
      }
      if (parser->record_callback_stopped) break;
    }
  } else if (data_bind_stream_emit_record(parser, value) != 0) {
    return db_error_set(error, DATA_BIND_ERR_RUNTIME, "record_callback", -1, -1,
                        "Record callback failed at index %llu",
                        (unsigned long long)(parser->record_callback_index - 1));
  }
  return DATA_BIND_OK;
}

static char *data_bind_stream_copy_slice(const char *text, size_t len) {
  char *copy = (char *)malloc(len + 1);
  if (copy == NULL) return NULL;
  if (len > 0) memcpy(copy, text, len);
  copy[len] = '\0';
  return copy;
}

static int data_bind_stream_values_push(data_bind_stream_t *parser, DataBindValue *item,
                                        const char *message) {
  if (parser == NULL || parser->stream_values == NULL || item == NULL) {
    data_bind_value_free(item);
    data_bind_stream_error_msg(parser, message);
    return -1;
  }
  if (data_bind_stream_emit_record(parser, item) != 0) {
    data_bind_value_free(item);
    return -1;
  }
  if (!dbv_array_push(&parser->stream_values->data.array_val, item)) {
    data_bind_value_free(item);
    data_bind_stream_error_msg(parser, "Out of memory appending streamed bind result");
    return -1;
  }
  return 0;
}

static int data_bind_stream_json_bind_value(data_bind_stream_t *parser, json_value_t *value) {
  DataBindValue *item;
  if (parser == NULL || value == NULL) return -1;
  item = bind_json_typed_value(parser->codec->schema_root, parser->type_name, value);
  turbo_free_json(&value);
  if (item == NULL) {
    data_bind_stream_error_msg(parser, "JSON stream item bind failed");
    return -1;
  }
  return data_bind_stream_values_push(parser, item, "JSON stream item append failed");
}

static int data_bind_stream_json_frame_reserve(data_bind_stream_t *parser) {
  data_bind_json_stream_frame_t *grown;
  size_t next_capacity;
  if (parser->json_frame_count < parser->json_frame_capacity) return 0;
  next_capacity = parser->json_frame_capacity == 0 ? 8 : parser->json_frame_capacity * 2;
  if (next_capacity <= parser->json_frame_capacity) {
    data_bind_stream_error_msg(parser, "JSON stream nesting too deep");
    return -1;
  }
  grown =
      (data_bind_json_stream_frame_t *)realloc(parser->json_frames, next_capacity * sizeof(*grown));
  if (grown == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory growing JSON stream stack");
    return -1;
  }
  parser->json_frames = grown;
  parser->json_frame_capacity = next_capacity;
  return 0;
}

static int data_bind_stream_json_attach_value(data_bind_stream_t *parser, json_value_t *value) {
  data_bind_json_stream_frame_t *parent;
  if (parser->json_frame_count == 0) return 0;
  parent = &parser->json_frames[parser->json_frame_count - 1];
  if (parent->is_object) {
    if (parent->pending_key == NULL) {
      data_bind_stream_error_msg(parser, "JSON stream object value without key");
      return -1;
    }
    turbo_json_object_add(parent->value, parent->pending_key, value);
    free(parent->pending_key);
    parent->pending_key = NULL;
  } else {
    turbo_json_array_add(parent->value, value);
  }
  return 0;
}

static int data_bind_stream_json_scalar(data_bind_stream_t *parser, json_value_t *value) {
  if (value == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory creating JSON stream value");
    return -1;
  }
  if (!parser->json_stream_active || parser->json_sax_depth == 0) {
    turbo_free_json(&value);
    return 0;
  }
  if (parser->json_sax_depth == 1 || parser->json_frame_count > 0) {
    if (data_bind_stream_json_attach_value(parser, value) != 0) {
      turbo_free_json(&value);
      return -1;
    }
    if (parser->json_frame_count == 0) {
      return data_bind_stream_json_bind_value(parser, value);
    }
  } else {
    turbo_free_json(&value);
  }
  return 0;
}

static int data_bind_stream_json_container_start(data_bind_stream_t *parser, json_value_t *value,
                                                 int is_object) {
  data_bind_json_stream_frame_t *frame;
  if (value == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory creating JSON stream value");
    return -1;
  }
  if (!parser->json_stream_active || parser->json_sax_depth == 0) {
    turbo_free_json(&value);
    return 0;
  }
  if (parser->json_sax_depth != 1 && parser->json_frame_count == 0) {
    turbo_free_json(&value);
    return 0;
  }
  if (data_bind_stream_json_attach_value(parser, value) != 0 ||
      data_bind_stream_json_frame_reserve(parser) != 0) {
    turbo_free_json(&value);
    return -1;
  }
  frame = &parser->json_frames[parser->json_frame_count++];
  frame->value = value;
  frame->pending_key = NULL;
  frame->is_object = is_object;
  return 0;
}

static int data_bind_stream_json_container_end(data_bind_stream_t *parser, int is_object) {
  data_bind_json_stream_frame_t frame;
  if (parser == NULL || !parser->json_stream_active || parser->json_frame_count == 0) return 0;
  frame = parser->json_frames[parser->json_frame_count - 1];
  if (frame.is_object != is_object) {
    data_bind_stream_error_msg(parser, "JSON stream container mismatch");
    return -1;
  }
  free(frame.pending_key);
  parser->json_frames[--parser->json_frame_count].pending_key = NULL;
  if (parser->json_frame_count == 0) {
    return data_bind_stream_json_bind_value(parser, frame.value);
  }
  return 0;
}

static int data_bind_stream_json_on_null(void *ctx) {
  return data_bind_stream_json_scalar((data_bind_stream_t *)ctx, turbo_json_create_null());
}

static int data_bind_stream_json_on_bool(void *ctx, bool val) {
  return data_bind_stream_json_scalar((data_bind_stream_t *)ctx, turbo_json_create_bool(val));
}

static int data_bind_stream_json_on_number_raw(void *ctx, const char *val, size_t len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  turbo_json_doc_t *value = NULL;
  if (val == NULL || len == 0 ||
      turbo_parse_json((const uint8_t *)val, len, &value) != 0 ||
      turbo_json_type(value) != TURBO_JSON_NUMBER) {
    turbo_free_json(&value);
    data_bind_stream_error_msg(parser, "Failed to preserve exact JSON stream number");
    return -1;
  }
  return data_bind_stream_json_scalar(parser, value);
}

static int data_bind_stream_json_on_string(void *ctx, const char *val, size_t len) {
  char *copy = data_bind_stream_copy_slice(val, len);
  json_value_t *value;
  if (copy == NULL) {
    data_bind_stream_error_msg((data_bind_stream_t *)ctx, "Out of memory copying JSON string");
    return -1;
  }
  value = turbo_json_create_string(copy);
  free(copy);
  return data_bind_stream_json_scalar((data_bind_stream_t *)ctx, value);
}

static int data_bind_stream_json_on_object_key(void *ctx, const char *key, size_t len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  data_bind_json_stream_frame_t *frame;
  if (parser == NULL || !parser->json_stream_active || parser->json_frame_count == 0) return 0;
  frame = &parser->json_frames[parser->json_frame_count - 1];
  if (!frame->is_object) return 0;
  free(frame->pending_key);
  frame->pending_key = data_bind_stream_copy_slice(key, len);
  if (frame->pending_key == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory copying JSON object key");
    return -1;
  }
  return 0;
}

static int data_bind_stream_json_on_object_start(void *ctx) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc = 0;
  if (parser == NULL) return -1;
  if (parser->json_sax_depth == 0) {
    parser->json_root_seen = 1;
  } else {
    rc = data_bind_stream_json_container_start(parser, turbo_json_create_object(), 1);
  }
  parser->json_sax_depth++;
  return rc;
}

static int data_bind_stream_json_on_object_end(void *ctx) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc;
  if (parser == NULL || parser->json_sax_depth == 0) return -1;
  rc = data_bind_stream_json_container_end(parser, 1);
  parser->json_sax_depth--;
  return rc;
}

static int data_bind_stream_json_on_array_start(void *ctx) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc = 0;
  if (parser == NULL) return -1;
  if (parser->json_sax_depth == 0) {
    parser->json_root_seen = 1;
    if (parser->json_stream_candidate) {
      parser->json_stream_active = 1;
      free(parser->buffer);
      parser->buffer = NULL;
      parser->size = 0;
      parser->capacity = 0;
    }
  } else {
    rc = data_bind_stream_json_container_start(parser, turbo_json_create_array(), 0);
  }
  parser->json_sax_depth++;
  return rc;
}

static int data_bind_stream_json_on_array_end(void *ctx) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc = 0;
  if (parser == NULL || parser->json_sax_depth == 0) return -1;
  if (parser->json_stream_active && parser->json_sax_depth == 1) {
    parser->json_stream_done = 1;
  } else {
    rc = data_bind_stream_json_container_end(parser, 0);
  }
  parser->json_sax_depth--;
  return rc;
}

static int data_bind_stream_xml_append(data_bind_stream_t *parser, const char *text, size_t len) {
  tstr_t next;
  if (parser == NULL || !parser->xml_capture_active || len == 0) return 0;
  next = tstr_cat_len(parser->xml_capture, text, len);
  if (next == NULL) {
    data_bind_stream_error_msg(parser, "Out of memory extending XML stream item");
    return -1;
  }
  parser->xml_capture = next;
  return 0;
}

static int data_bind_stream_xml_append_char(data_bind_stream_t *parser, char ch) {
  return data_bind_stream_xml_append(parser, &ch, 1);
}

static int data_bind_stream_xml_close_start(data_bind_stream_t *parser) {
  if (parser != NULL && parser->xml_capture_active && parser->xml_open_start) {
    if (data_bind_stream_xml_append_char(parser, '>') != 0) return -1;
    parser->xml_open_start = 0;
  }
  return 0;
}

static int data_bind_stream_xml_bind_capture(data_bind_stream_t *parser) {
  turbo_xml_doc_t *doc = NULL;
  DataBindValue *item;
  if (parser == NULL || parser->xml_capture == NULL) return -1;
  if (turbo_parse_xml((const uint8_t *)parser->xml_capture, tstr_len(parser->xml_capture), &doc) !=
          0 ||
      doc == NULL) {
    data_bind_stream_error_msg(parser, "XML stream item parse failed");
    return -1;
  }
  item = bind_xml_typed_value(parser->codec->schema_root, parser->type_name, doc, "/*");
  turbo_free_xml(&doc);
  if (item == NULL) {
    data_bind_stream_error_msg(parser, "XML stream item bind failed");
    return -1;
  }
  return data_bind_stream_values_push(parser, item, "XML stream item append failed");
}

static int data_bind_stream_xml_name_eq(const char *left, size_t left_len, const char *right) {
  return right != NULL && strlen(right) == left_len && memcmp(left, right, left_len) == 0;
}

static int data_bind_stream_xml_on_element_start(void *ctx, const char *name, size_t name_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->xml_stream_candidate) return 0;
  if (!parser->xml_capture_active &&
      !data_bind_stream_xml_name_eq(name, name_len, parser->xml_stream_target)) {
    return 0;
  }
  if (!parser->xml_capture_active) {
    tstr_clear(parser->xml_capture);
    parser->xml_capture_active = 1;
    parser->xml_capture_depth = 0;
  } else if (data_bind_stream_xml_close_start(parser) != 0) {
    return -1;
  }
  if (data_bind_stream_xml_append_char(parser, '<') != 0 ||
      data_bind_stream_xml_append(parser, name, name_len) != 0) {
    return -1;
  }
  parser->xml_open_start = 1;
  parser->xml_capture_depth++;
  return 0;
}

static int data_bind_stream_xml_on_attribute(void *ctx, const char *name, size_t name_len,
                                             const char *value, size_t value_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->xml_capture_active || !parser->xml_open_start) return 0;
  if (data_bind_stream_xml_append_char(parser, ' ') != 0 ||
      data_bind_stream_xml_append(parser, name, name_len) != 0 ||
      data_bind_stream_xml_append(parser, "=\"", 2) != 0 ||
      data_bind_stream_xml_append(parser, value, value_len) != 0 ||
      data_bind_stream_xml_append_char(parser, '"') != 0) {
    return -1;
  }
  return 0;
}

static int data_bind_stream_xml_on_element_end(void *ctx, const char *name, size_t name_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  int rc;
  if (parser == NULL || !parser->xml_capture_active) return 0;
  if (data_bind_stream_xml_close_start(parser) != 0 ||
      data_bind_stream_xml_append(parser, "</", 2) != 0 ||
      data_bind_stream_xml_append(parser, name, name_len) != 0 ||
      data_bind_stream_xml_append_char(parser, '>') != 0) {
    return -1;
  }
  if (parser->xml_capture_depth > 0) parser->xml_capture_depth--;
  if (parser->xml_capture_depth == 0) {
    rc = data_bind_stream_xml_bind_capture(parser);
    parser->xml_capture_active = 0;
    parser->xml_open_start = 0;
    tstr_clear(parser->xml_capture);
    return rc;
  }
  return 0;
}

static int data_bind_stream_xml_on_text(void *ctx, const char *text, size_t text_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->xml_capture_active) return 0;
  if (data_bind_stream_xml_close_start(parser) != 0) return -1;
  return data_bind_stream_xml_append(parser, text, text_len);
}

static int data_bind_stream_xml_on_comment(void *ctx, const char *text, size_t text_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->xml_capture_active) return 0;
  if (data_bind_stream_xml_close_start(parser) != 0 ||
      data_bind_stream_xml_append(parser, "<!--", 4) != 0 ||
      data_bind_stream_xml_append(parser, text, text_len) != 0 ||
      data_bind_stream_xml_append(parser, "-->", 3) != 0) {
    return -1;
  }
  return 0;
}

static int data_bind_stream_xml_on_cdata(void *ctx, const char *text, size_t text_len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)ctx;
  if (parser == NULL || !parser->xml_capture_active) return 0;
  if (data_bind_stream_xml_close_start(parser) != 0 ||
      data_bind_stream_xml_append(parser, "<![CDATA[", 9) != 0 ||
      data_bind_stream_xml_append(parser, text, text_len) != 0 ||
      data_bind_stream_xml_append(parser, "]]>", 3) != 0) {
    return -1;
  }
  return 0;
}

static const turbo_json_sax_handler_raw_t DATA_BIND_JSON_STREAM_HANDLER = {
    data_bind_stream_json_on_null,         data_bind_stream_json_on_bool,
    data_bind_stream_json_on_number_raw,   data_bind_stream_json_on_string,
    data_bind_stream_json_on_object_start, data_bind_stream_json_on_object_key,
    data_bind_stream_json_on_object_end,   data_bind_stream_json_on_array_start,
    data_bind_stream_json_on_array_end};

static const turbo_xml_sax_handler_t DATA_BIND_XML_STREAM_HANDLER = {
    NULL,
    NULL,
    data_bind_stream_xml_on_element_start,
    data_bind_stream_xml_on_attribute,
    data_bind_stream_xml_on_element_end,
    data_bind_stream_xml_on_text,
    data_bind_stream_xml_on_comment,
    data_bind_stream_xml_on_cdata,
    NULL,
    NULL};

static const turbo_json_sax_handler_t DATA_BIND_JSON_SAX_VALIDATE_HANDLER = {0};
static const turbo_yaml_sax_handler_t DATA_BIND_YAML_SAX_VALIDATE_HANDLER = {0};
static const turbo_xml_sax_handler_t DATA_BIND_XML_SAX_VALIDATE_HANDLER = {0};

static DataBindStatus data_bind_stream_sax_error(data_bind_stream_t *parser, DataBindError *error,
                                                 const char *operation) {
  const char *message = "Stream parse failed";
  const char *path = "stream";
  if (parser != NULL) {
    if (parser->stream_error[0] != '\0') {
      message = parser->stream_error;
    } else if (parser->json_sax != NULL) {
      message = turbo_json_sax_parser_error(parser->json_sax);
      path = "json";
    } else if (parser->yaml_sax != NULL) {
      message = turbo_yaml_sax_parser_error(parser->yaml_sax);
      path = "yaml";
    } else if (parser->xml_sax != NULL) {
      message = turbo_xml_sax_parser_error(parser->xml_sax);
      path = "xml";
    }
    parser->sax_failed = 1;
  }
  if (message == NULL || message[0] == '\0') message = "Stream parse failed";
  if (parser != NULL && parser->record_callback_failed) {
    return db_error_set(error, DATA_BIND_ERR_RUNTIME, "record_callback", -1, -1, "%s", message);
  }
  return db_error_set(error, DATA_BIND_ERR_PARSE, path, -1, -1, "%s: %s", operation, message);
}

static DataBindStatus data_bind_stream_sax_feed(data_bind_stream_t *parser, const char *data,
                                                size_t len, DataBindError *error) {
  if (parser == NULL || parser->sax_failed) {
    return data_bind_stream_sax_error(parser, error, "stream feed");
  }
  if (parser->json_sax != NULL) {
    if (turbo_json_sax_parser_feed(parser->json_sax, data, len) != 0) {
      return data_bind_stream_sax_error(parser, error, "JSON stream feed");
    }
  } else if (parser->yaml_sax != NULL) {
    if (turbo_yaml_sax_parser_feed(parser->yaml_sax, data, len) != 0) {
      return data_bind_stream_sax_error(parser, error, "YAML stream feed");
    }
  } else if (parser->xml_sax != NULL) {
    if (turbo_xml_sax_parser_feed(parser->xml_sax, data, len) != 0) {
      return data_bind_stream_sax_error(parser, error, "XML stream feed");
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_sax_finish(data_bind_stream_t *parser,
                                                  DataBindError *error) {
  if (parser == NULL || parser->sax_failed) {
    return data_bind_stream_sax_error(parser, error, "stream finish");
  }
  if (parser->json_sax != NULL) {
    if (turbo_json_sax_parser_finish(parser->json_sax) != 0) {
      return data_bind_stream_sax_error(parser, error, "JSON stream finish");
    }
  } else if (parser->yaml_sax != NULL) {
    if (turbo_yaml_sax_parser_finish(parser->yaml_sax) != 0) {
      return data_bind_stream_sax_error(parser, error, "YAML stream finish");
    }
  } else if (parser->xml_sax != NULL) {
    if (turbo_xml_sax_parser_finish(parser->xml_sax) != 0) {
      return data_bind_stream_sax_error(parser, error, "XML stream finish");
    }
  }
  return DATA_BIND_OK;
}

static int data_bind_stream_csv_record_append(data_bind_stream_t *parser, char ch) {
  char *grown;
  size_t next_capacity;
  if (parser == NULL) return 0;
  if (parser->csv_record_len + 1 >= parser->csv_record_capacity) {
    next_capacity = parser->csv_record_capacity == 0 ? 256 : parser->csv_record_capacity * 2;
    if (next_capacity <= parser->csv_record_capacity) return 0;
    grown = (char *)realloc(parser->csv_record, next_capacity);
    if (grown == NULL) return 0;
    parser->csv_record = grown;
    parser->csv_record_capacity = next_capacity;
  }
  parser->csv_record[parser->csv_record_len++] = ch;
  parser->csv_record[parser->csv_record_len] = '\0';
  return 1;
}

static int data_bind_stream_csv_field_append(data_bind_stream_t *parser, char ch) {
  char *grown;
  size_t next_capacity;
  if (parser == NULL) return 0;
  if (parser->csv_field_len + 1 >= parser->csv_field_capacity) {
    next_capacity = parser->csv_field_capacity == 0 ? 128 : parser->csv_field_capacity * 2;
    if (next_capacity <= parser->csv_field_capacity) return 0;
    grown = (char *)realloc(parser->csv_field, next_capacity);
    if (grown == NULL) return 0;
    parser->csv_field = grown;
    parser->csv_field_capacity = next_capacity;
  }
  parser->csv_field[parser->csv_field_len++] = ch;
  parser->csv_field[parser->csv_field_len] = '\0';
  return 1;
}

static void data_bind_stream_csv_clear_fields(data_bind_stream_t *parser) {
  size_t i;
  if (parser == NULL) return;
  for (i = 0; i < parser->csv_field_count; i++) {
    free(parser->csv_field_storage[i]);
    parser->csv_field_storage[i] = NULL;
  }
  parser->csv_field_count = 0;
  parser->csv_field_len = 0;
  if (parser->csv_field != NULL) parser->csv_field[0] = '\0';
}

static int data_bind_stream_csv_finish_field(data_bind_stream_t *parser) {
  char **grown_storage;
  tstr_v *grown_fields;
  char *field_copy;
  size_t next_capacity;

  if (parser == NULL) return 0;
  if (parser->csv_field_count >= parser->csv_fields_capacity) {
    next_capacity = parser->csv_fields_capacity == 0 ? 8 : parser->csv_fields_capacity * 2;
    if (next_capacity <= parser->csv_fields_capacity) return 0;
    grown_fields = (tstr_v *)realloc(parser->csv_fields, next_capacity * sizeof(*grown_fields));
    if (grown_fields == NULL) return 0;
    parser->csv_fields = grown_fields;
    grown_storage =
        (char **)realloc(parser->csv_field_storage, next_capacity * sizeof(*grown_storage));
    if (grown_storage == NULL) return 0;
    parser->csv_field_storage = grown_storage;
    parser->csv_fields_capacity = next_capacity;
  }

  field_copy = (char *)malloc(parser->csv_field_len + 1);
  if (field_copy == NULL) return 0;
  if (parser->csv_field_len > 0) memcpy(field_copy, parser->csv_field, parser->csv_field_len);
  field_copy[parser->csv_field_len] = '\0';
  parser->csv_field_storage[parser->csv_field_count] = field_copy;
  parser->csv_fields[parser->csv_field_count] = tstr_v_from_buf(field_copy, parser->csv_field_len);
  parser->csv_field_count++;
  parser->csv_field_len = 0;
  if (parser->csv_field != NULL) parser->csv_field[0] = '\0';
  return 1;
}

static DataBindStatus data_bind_stream_csv_compile_filter(data_bind_stream_t *parser,
                                                          DataBindError *error) {
  char *header_doc = NULL;
  size_t header_doc_len;
  turbo_csv_options_t opts = {false, ',', '"', true};
  int compiled;

  if (parser == NULL || parser->path_or_expr == NULL || parser->csv_filter != NULL)
    return DATA_BIND_OK;

  header_doc_len = parser->csv_header_len + 1;
  header_doc = (char *)malloc(header_doc_len + 1);
  if (header_doc == NULL) {
    parser->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                        "Out of memory building CSVPath stream header");
  }
  memcpy(header_doc, parser->csv_header, parser->csv_header_len);
  header_doc[parser->csv_header_len] = '\n';
  header_doc[header_doc_len] = '\0';

  if (turbo_parse_csv_opts((const uint8_t *)header_doc, header_doc_len, &opts,
                           &parser->csv_filter_doc) != 0) {
    free(header_doc);
    parser->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_PARSE, "csv", -1, -1,
                        "Failed to parse CSVPath stream header");
  }
  free(header_doc);
  if (parser->csv_filter_doc == NULL) {
    parser->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_PARSE, "csv", -1, -1,
                        "Failed to parse CSVPath stream header");
  }

  parser->csv_filter = turbo_dsv_filter_create(parser->csv_filter_doc, 0);
  compiled = parser->csv_filter != NULL &&
             turbo_dsv_filter_compile(parser->csv_filter, parser->path_or_expr);
  if (!compiled) {
    const char *filter_error = parser->csv_filter != NULL
                                   ? turbo_dsv_filter_error(parser->csv_filter)
                                   : "Failed to create CSVPath filter";
    parser->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_PARSE, "csvpath", -1, -1, "%s", filter_error);
  }
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_csv_process_record(data_bind_stream_t *parser,
                                                          DataBindError *error) {
  char *doc_text = NULL;
  size_t doc_len;
  DataBindValue *value = NULL;
  DataBindStatus status;
  int match;
  if (parser == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (parser->csv_record_len == 0 && parser->csv_header_seen) {
    data_bind_stream_csv_clear_fields(parser);
    return DATA_BIND_OK;
  }

  if (!parser->csv_header_seen) {
    parser->csv_header = (char *)malloc(parser->csv_record_len + 1);
    if (parser->csv_header == NULL) {
      parser->csv_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                          "Out of memory storing CSV stream header");
    }
    memcpy(parser->csv_header, parser->csv_record, parser->csv_record_len);
    parser->csv_header[parser->csv_record_len] = '\0';
    parser->csv_header_len = parser->csv_record_len;
    parser->csv_header_seen = 1;
    parser->csv_record_len = 0;
    if (parser->csv_record != NULL) parser->csv_record[0] = '\0';
    status = data_bind_stream_csv_compile_filter(parser, error);
    data_bind_stream_csv_clear_fields(parser);
    return status;
  }

  if (parser->path_or_expr != NULL) {
    if (parser->csv_filter == NULL) {
      status = data_bind_stream_csv_compile_filter(parser, error);
      if (status != DATA_BIND_OK) return status;
    }
    match = turbo_dsv_filter_check_values(parser->csv_filter, parser->csv_fields,
                                          parser->csv_field_count);
    if (match < 0) {
      parser->csv_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_PARSE, "csvpath", -1, -1,
                          "CSVPath stream row filter evaluation failed");
    }
    if (match == 0) {
      data_bind_stream_csv_clear_fields(parser);
      parser->csv_record_len = 0;
      if (parser->csv_record != NULL) parser->csv_record[0] = '\0';
      parser->csv_data_row++;
      return DATA_BIND_OK;
    }
  }

  doc_len = parser->csv_header_len + 1 + parser->csv_record_len + 1;
  doc_text = (char *)malloc(doc_len + 1);
  if (doc_text == NULL) {
    parser->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                        "Out of memory building CSV stream row");
  }
  memcpy(doc_text, parser->csv_header, parser->csv_header_len);
  doc_text[parser->csv_header_len] = '\n';
  memcpy(doc_text + parser->csv_header_len + 1, parser->csv_record, parser->csv_record_len);
  doc_text[doc_len - 1] = '\n';
  doc_text[doc_len] = '\0';

  status =
      data_bind_parse_csv(parser->codec, parser->type_name, doc_text, doc_len, 0, &value, error);
  if (status == DATA_BIND_OK && value != NULL) {
    if (data_bind_stream_emit_record(parser, value) != 0) {
      data_bind_value_free(value);
      free(doc_text);
      parser->csv_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_RUNTIME, "record_callback", -1, -1,
                          "Record callback failed at CSV row %llu",
                          (unsigned long long)parser->csv_data_row);
    }
    if (!dbv_array_push(&parser->csv_values->data.array_val, value)) {
      data_bind_value_free(value);
      free(doc_text);
      parser->csv_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                          "Out of memory appending CSV stream row");
    }
  }

  free(doc_text);
  data_bind_stream_csv_clear_fields(parser);
  parser->csv_record_len = 0;
  if (parser->csv_record != NULL) parser->csv_record[0] = '\0';
  parser->csv_data_row++;
  if (status != DATA_BIND_OK) parser->csv_failed = 1;
  return status;
}

static DataBindStatus data_bind_stream_csv_feed(data_bind_stream_t *parser, const char *data,
                                                size_t len, DataBindError *error) {
  size_t i;
  DataBindStatus status = DATA_BIND_OK;
  if (parser == NULL || data == NULL) return DATA_BIND_ERR_INVALID_ARG;

  for (i = 0; i < len; i++) {
    char ch = data[i];
  reprocess:
    if (parser->csv_skip_next_lf) {
      parser->csv_skip_next_lf = 0;
      if (ch == '\n') continue;
    }
    if (parser->csv_quote_pending) {
      parser->csv_quote_pending = 0;
      if (ch == '"') {
        if (!data_bind_stream_csv_record_append(parser, ch)) {
          parser->csv_failed = 1;
          return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                              "Out of memory extending CSV stream record");
        }
        if (!data_bind_stream_csv_field_append(parser, ch)) {
          parser->csv_failed = 1;
          return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                              "Out of memory extending CSV stream field");
        }
        continue;
      }
      parser->csv_in_quotes = 0;
      goto reprocess;
    }

    if (parser->csv_in_quotes) {
      if (!data_bind_stream_csv_record_append(parser, ch)) {
        parser->csv_failed = 1;
        return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                            "Out of memory extending CSV stream record");
      }
      if (ch == '"') {
        parser->csv_quote_pending = 1;
      } else if (!data_bind_stream_csv_field_append(parser, ch)) {
        parser->csv_failed = 1;
        return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                            "Out of memory extending CSV stream field");
      }
      continue;
    }

    if (ch == '"') {
      parser->csv_in_quotes = 1;
      if (!data_bind_stream_csv_record_append(parser, ch)) {
        parser->csv_failed = 1;
        return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                            "Out of memory extending CSV stream record");
      }
      continue;
    }
    if (ch == ',') {
      if (!data_bind_stream_csv_record_append(parser, ch) ||
          !data_bind_stream_csv_finish_field(parser)) {
        parser->csv_failed = 1;
        return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                            "Out of memory extending CSV stream field list");
      }
      continue;
    }
    if (ch == '\r' || ch == '\n') {
      if (!data_bind_stream_csv_finish_field(parser)) {
        parser->csv_failed = 1;
        return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                            "Out of memory extending CSV stream field list");
      }
      status = data_bind_stream_csv_process_record(parser, error);
      if (status != DATA_BIND_OK) return status;
      if (ch == '\r') parser->csv_skip_next_lf = 1;
      continue;
    }
    if (!data_bind_stream_csv_record_append(parser, ch)) {
      parser->csv_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                          "Out of memory extending CSV stream record");
    }
    if (!data_bind_stream_csv_field_append(parser, ch)) {
      parser->csv_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                          "Out of memory extending CSV stream field");
    }
  }
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_csv_finish(data_bind_stream_t *parser,
                                                  DataBindValue **out_value, DataBindError *error) {
  DataBindStatus status;
  if (parser == NULL || out_value == NULL) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_finish", -1, -1,
                        "Invalid CSV stream finish arguments");
  }
  if (parser->csv_quote_pending) {
    parser->csv_quote_pending = 0;
    parser->csv_in_quotes = 0;
  }
  if (parser->csv_in_quotes) {
    parser->csv_failed = 1;
    return db_error_set(error, DATA_BIND_ERR_PARSE, "csv", -1, -1, "Unterminated quoted CSV field");
  }
  if (parser->csv_record_len > 0 || parser->csv_field_len > 0 || parser->csv_field_count > 0 ||
      !parser->csv_header_seen) {
    if (!data_bind_stream_csv_finish_field(parser)) {
      parser->csv_failed = 1;
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory extending CSV stream field list");
    }
    status = data_bind_stream_csv_process_record(parser, error);
    if (status != DATA_BIND_OK) return status;
  }
  if (!parser->csv_header_seen || parser->csv_failed || parser->csv_values == NULL) {
    return db_error_set(error, DATA_BIND_ERR_PARSE, "csv", -1, -1, "CSV stream parse failed");
  }
  *out_value = parser->csv_values;
  parser->csv_values = NULL;
  db_error_clear(error);
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_text_feed(data_bind_stream_t *parser, const char *data,
                                                 size_t len, DataBindError *error);
static DataBindStatus data_bind_stream_json_finish(data_bind_stream_t *parser,
                                                   DataBindValue **out_value, DataBindError *error);
static DataBindStatus data_bind_stream_xml_finish(data_bind_stream_t *parser,
                                                  DataBindValue **out_value, DataBindError *error);
static DataBindStatus data_bind_stream_buffered_finish(data_bind_stream_t *parser,
                                                       DataBindValue **out_value,
                                                       DataBindError *error);

static data_bind_stream_t *data_bind_stream_create_common(
    DataBind *codec, const char *type_name, const char *path_or_expr, DataBindValue **out_value,
    DataBindError *error,
    DataBindStatus (*feed_fn)(data_bind_stream_t *, const char *, size_t, DataBindError *),
    DataBindStatus (*finish_fn)(data_bind_stream_t *, DataBindValue **, DataBindError *),
    DataBindStatus (*bind_fn)(DataBind *, const char *, const char *, size_t, const char *,
                              DataBindValue **, DataBindError *),
    int is_csv, int json_stream_candidate, int xml_stream_candidate) {
  data_bind_stream_t *parser = NULL;
  size_t type_name_len;
  size_t path_len;
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || type_name == NULL || type_name[0] == '\0' || out_value == NULL ||
      feed_fn == NULL || finish_fn == NULL || (!is_csv && bind_fn == NULL)) {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_create", -1, -1,
                 "Invalid stream constructor arguments");
    return NULL;
  }

  parser = (data_bind_stream_t *)malloc(sizeof(*parser));
  if (parser == NULL) {
    db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                 "Out of memory creating stream");
    return NULL;
  }

  type_name_len = strlen(type_name);
  parser->type_name = (char *)malloc(type_name_len + 1);
  if (parser->type_name == NULL) {
    free(parser);
    db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                 "Out of memory creating stream");
    return NULL;
  }
  memcpy(parser->type_name, type_name, type_name_len + 1);

  if (path_or_expr != NULL && path_or_expr[0] != '\0') {
    path_len = strlen(path_or_expr);
    parser->path_or_expr = (char *)malloc(path_len + 1);
    if (parser->path_or_expr == NULL) {
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating stream");
      return NULL;
    }
    memcpy(parser->path_or_expr, path_or_expr, path_len + 1);
  } else {
    parser->path_or_expr = NULL;
  }

  parser->codec = codec;
  parser->out_value = out_value;
  parser->error = error;
  parser->record_callback = NULL;
  parser->record_callback_user = NULL;
  parser->record_callback_index = 0;
  parser->feed_fn = feed_fn;
  parser->finish_fn = finish_fn;
  parser->bind_fn = bind_fn;
  parser->buffer = NULL;
  parser->size = 0;
  parser->capacity = 0;
  parser->csv_header = NULL;
  parser->csv_header_len = 0;
  parser->csv_record = NULL;
  parser->csv_record_len = 0;
  parser->csv_record_capacity = 0;
  parser->csv_field = NULL;
  parser->csv_field_len = 0;
  parser->csv_field_capacity = 0;
  parser->csv_fields = NULL;
  parser->csv_field_storage = NULL;
  parser->csv_field_count = 0;
  parser->csv_fields_capacity = 0;
  parser->csv_filter_doc = NULL;
  parser->csv_filter = NULL;
  parser->csv_values = NULL;
  parser->stream_values = NULL;
  parser->json_sax = NULL;
  parser->yaml_sax = NULL;
  parser->xml_sax = NULL;
  parser->json_frames = NULL;
  parser->json_frame_count = 0;
  parser->json_frame_capacity = 0;
  parser->json_sax_depth = 0;
  parser->xml_stream_target = NULL;
  parser->xml_capture = NULL;
  parser->xml_capture_depth = 0;
  parser->csv_data_row = 0;
  parser->csv_header_seen = 0;
  parser->csv_in_quotes = 0;
  parser->csv_quote_pending = 0;
  parser->csv_skip_next_lf = 0;
  parser->csv_failed = 0;
  parser->sax_failed = 0;
  parser->is_csv = is_csv;
  parser->json_stream_candidate = json_stream_candidate;
  parser->json_stream_active = 0;
  parser->json_stream_done = 0;
  parser->json_root_seen = 0;
  parser->xml_stream_candidate = xml_stream_candidate;
  parser->xml_capture_active = 0;
  parser->xml_open_start = 0;
  parser->stream_error[0] = '\0';
  parser->finished = 0;
  parser->started = 0;
  parser->record_callback_stopped = 0;
  parser->record_callback_failed = 0;
  if (is_csv) {
    parser->csv_values = dbv_new(DATA_BIND_VALUE_LIST);
    if (parser->csv_values == NULL) {
      free(parser->path_or_expr);
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating CSV stream output");
      return NULL;
    }
  } else if (finish_fn == data_bind_stream_json_finish) {
    if (parser->json_stream_candidate) {
      parser->stream_values = dbv_new(DATA_BIND_VALUE_LIST);
      if (parser->stream_values == NULL) {
        free(parser->path_or_expr);
        free(parser->type_name);
        free(parser);
        db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                     "Out of memory creating JSON stream output");
        return NULL;
      }
    }
    parser->json_sax = parser->json_stream_candidate
                           ? turbo_json_sax_parser_create_raw(&DATA_BIND_JSON_STREAM_HANDLER,
                                                             parser)
                           : turbo_json_sax_parser_create(&DATA_BIND_JSON_SAX_VALIDATE_HANDLER,
                                                         parser);
    if (parser->json_sax == NULL) {
      data_bind_value_free(parser->stream_values);
      free(parser->path_or_expr);
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating JSON stream validator");
      return NULL;
    }
  } else if (finish_fn == data_bind_stream_buffered_finish) {
    parser->yaml_sax =
        turbo_yaml_sax_parser_create(&DATA_BIND_YAML_SAX_VALIDATE_HANDLER, parser);
    if (parser->yaml_sax == NULL) {
      free(parser->path_or_expr);
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating YAML stream validator");
      return NULL;
    }
  } else if (finish_fn == data_bind_stream_xml_finish) {
    if (parser->xml_stream_candidate) {
      parser->xml_stream_target = data_bind_stream_xml_target_from_path(parser->path_or_expr);
      parser->xml_capture = tstr_new();
      parser->stream_values = dbv_new(DATA_BIND_VALUE_LIST);
      if (parser->xml_stream_target == NULL || parser->xml_capture == NULL ||
          parser->stream_values == NULL) {
        free(parser->xml_stream_target);
        tstr_free(parser->xml_capture);
        data_bind_value_free(parser->stream_values);
        free(parser->path_or_expr);
        free(parser->type_name);
        free(parser);
        db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                     "Out of memory creating XML stream output");
        return NULL;
      }
    }
    parser->xml_sax = turbo_xml_sax_parser_create(parser->xml_stream_candidate
                                                      ? &DATA_BIND_XML_STREAM_HANDLER
                                                      : &DATA_BIND_XML_SAX_VALIDATE_HANDLER,
                                                  parser);
    if (parser->xml_sax == NULL) {
      free(parser->xml_stream_target);
      tstr_free(parser->xml_capture);
      data_bind_value_free(parser->stream_values);
      free(parser->path_or_expr);
      free(parser->type_name);
      free(parser);
      db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_create", -1, -1,
                   "Out of memory creating XML stream validator");
      return NULL;
    }
  }
  db_error_clear(error);
  return parser;
}

static DataBindStatus data_bind_stream_bind_json(DataBind *codec, const char *type_name,
                                                 const char *text, size_t len, const char *path,
                                                 DataBindValue **out_value, DataBindError *error) {
  (void)path;
  return data_bind_parse_json(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_json_all(DataBind *codec, const char *type_name,
                                                     const char *text, size_t len, const char *path,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  (void)path;
  return data_bind_parse_json_all(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_json_path(DataBind *codec, const char *type_name,
                                                      const char *text, size_t len,
                                                      const char *path, DataBindValue **out_value,
                                                      DataBindError *error) {
  return data_bind_parse_json_path(codec, type_name, text, len, path, out_value, error);
}

static DataBindStatus data_bind_stream_bind_json_path_all(DataBind *codec, const char *type_name,
                                                          const char *text, size_t len,
                                                          const char *path,
                                                          DataBindValue **out_value,
                                                          DataBindError *error) {
  return data_bind_parse_json_path_all(codec, type_name, text, len, path, out_value, error);
}

static DataBindStatus data_bind_stream_bind_yaml(DataBind *codec, const char *type_name,
                                                 const char *text, size_t len, const char *path,
                                                 DataBindValue **out_value, DataBindError *error) {
  (void)path;
  return data_bind_parse_yaml(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_yaml_all(DataBind *codec, const char *type_name,
                                                     const char *text, size_t len, const char *path,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  (void)path;
  return data_bind_parse_yaml_all(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_yaml_path(DataBind *codec, const char *type_name,
                                                      const char *text, size_t len,
                                                      const char *path, DataBindValue **out_value,
                                                      DataBindError *error) {
  return data_bind_parse_yaml_path(codec, type_name, text, len, path, out_value, error);
}

static DataBindStatus data_bind_stream_bind_yaml_path_all(DataBind *codec, const char *type_name,
                                                          const char *text, size_t len,
                                                          const char *path,
                                                          DataBindValue **out_value,
                                                          DataBindError *error) {
  return data_bind_parse_yaml_path_all(codec, type_name, text, len, path, out_value, error);
}

static DataBindStatus data_bind_stream_bind_xml(DataBind *codec, const char *type_name,
                                                const char *text, size_t len, const char *path,
                                                DataBindValue **out_value, DataBindError *error) {
  (void)path;
  return data_bind_parse_xml(codec, type_name, text, len, out_value, error);
}

static DataBindStatus data_bind_stream_bind_xml_path_all(DataBind *codec, const char *type_name,
                                                         const char *text, size_t len,
                                                         const char *path,
                                                         DataBindValue **out_value,
                                                         DataBindError *error) {
  return data_bind_parse_xml_path_all(codec, type_name, text, len, path, out_value, error);
}

data_bind_stream_t *data_bind_stream_json_create(DataBind *codec, const char *type_name,
                                                 DataBindValue **out_value, DataBindError *error) {
  return data_bind_stream_create_common(codec, type_name, NULL, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_json_finish,
                                        data_bind_stream_bind_json, 0, 0, 0);
}

data_bind_stream_t *data_bind_stream_json_all_create(DataBind *codec, const char *type_name,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  return data_bind_stream_create_common(codec, type_name, NULL, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_json_finish,
                                        data_bind_stream_bind_json_all, 0, 1, 0);
}

data_bind_stream_t *data_bind_stream_json_path_create(DataBind *codec, const char *type_name,
                                                      const char *json_path,
                                                      DataBindValue **out_value,
                                                      DataBindError *error) {
  if (json_path == NULL || json_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_json_path_create", -1, -1,
                 "JSONPath is required");
    return NULL;
  }
  return data_bind_stream_create_common(codec, type_name, json_path, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_json_finish,
                                        data_bind_stream_bind_json_path, 0, 0, 0);
}

data_bind_stream_t *data_bind_stream_json_path_all_create(DataBind *codec, const char *type_name,
                                                          const char *json_path,
                                                          DataBindValue **out_value,
                                                          DataBindError *error) {
  if (json_path == NULL || json_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_json_path_all_create", -1, -1,
                 "JSONPath is required");
    return NULL;
  }
  return data_bind_stream_create_common(codec, type_name, json_path, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_json_finish,
                                        data_bind_stream_bind_json_path_all, 0,
                                        data_bind_stream_json_path_is_root_array(json_path), 0);
}

data_bind_stream_t *data_bind_stream_yaml_create(DataBind *codec, const char *type_name,
                                                 DataBindValue **out_value, DataBindError *error) {
  return data_bind_stream_create_common(
      codec, type_name, NULL, out_value, error, data_bind_stream_text_feed,
      data_bind_stream_buffered_finish, data_bind_stream_bind_yaml, 0, 0, 0);
}

data_bind_stream_t *data_bind_stream_yaml_all_create(DataBind *codec, const char *type_name,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  return data_bind_stream_create_common(
      codec, type_name, NULL, out_value, error, data_bind_stream_text_feed,
      data_bind_stream_buffered_finish, data_bind_stream_bind_yaml_all, 0, 0, 0);
}

data_bind_stream_t *data_bind_stream_yaml_path_create(DataBind *codec, const char *type_name,
                                                      const char *yaml_path,
                                                      DataBindValue **out_value,
                                                      DataBindError *error) {
  if (yaml_path == NULL || yaml_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_yaml_path_create", -1, -1,
                 "YPATH is required");
    return NULL;
  }
  return data_bind_stream_create_common(
      codec, type_name, yaml_path, out_value, error, data_bind_stream_text_feed,
      data_bind_stream_buffered_finish, data_bind_stream_bind_yaml_path, 0, 0, 0);
}

data_bind_stream_t *data_bind_stream_yaml_path_all_create(DataBind *codec, const char *type_name,
                                                          const char *yaml_path,
                                                          DataBindValue **out_value,
                                                          DataBindError *error) {
  if (yaml_path == NULL || yaml_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_yaml_path_all_create", -1, -1,
                 "YPATH is required");
    return NULL;
  }
  return data_bind_stream_create_common(
      codec, type_name, yaml_path, out_value, error, data_bind_stream_text_feed,
      data_bind_stream_buffered_finish, data_bind_stream_bind_yaml_path_all, 0, 0, 0);
}

data_bind_stream_t *data_bind_stream_csv_all_create(DataBind *codec, const char *type_name,
                                                    DataBindValue **out_value,
                                                    DataBindError *error) {
  return data_bind_stream_create_common(codec, type_name, NULL, out_value, error,
                                        data_bind_stream_csv_feed, data_bind_stream_csv_finish,
                                        NULL, 1, 0, 0);
}

data_bind_stream_t *data_bind_stream_csv_path_create(DataBind *codec, const char *type_name,
                                                     const char *csv_path,
                                                     DataBindValue **out_value,
                                                     DataBindError *error) {
  if (csv_path == NULL || csv_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_csv_path_create", -1, -1,
                 "CSVPath is required");
    return NULL;
  }
  return data_bind_stream_create_common(codec, type_name, csv_path, out_value, error,
                                        data_bind_stream_csv_feed, data_bind_stream_csv_finish,
                                        NULL, 1, 0, 0);
}

data_bind_stream_t *data_bind_stream_xml_create(DataBind *codec, const char *type_name,
                                                DataBindValue **out_value, DataBindError *error) {
  return data_bind_stream_create_common(codec, type_name, NULL, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_xml_finish,
                                        data_bind_stream_bind_xml, 0, 0, 0);
}

data_bind_stream_t *data_bind_stream_xml_path_all_create(DataBind *codec, const char *type_name,
                                                         const char *xml_path,
                                                         DataBindValue **out_value,
                                                         DataBindError *error) {
  if (xml_path == NULL || xml_path[0] == '\0') {
    db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_xml_path_all_create", -1, -1,
                 "XMLPath is required");
    return NULL;
  }
  return data_bind_stream_create_common(codec, type_name, xml_path, out_value, error,
                                        data_bind_stream_text_feed, data_bind_stream_xml_finish,
                                        data_bind_stream_bind_xml_path_all, 0, 0,
                                        data_bind_stream_xml_can_bind_incrementally(xml_path));
}

DataBindStatus data_bind_stream_set_record_callback(data_bind_stream_t *stream,
                                                    DataBindRecordFn callback, void *user_data) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  if (parser == NULL || callback == NULL || parser->started || parser->finished) {
    return db_error_set(parser != NULL ? parser->error : NULL, DATA_BIND_ERR_INVALID_ARG,
                        "data_bind_stream_set_record_callback", -1, -1,
                        "Record callback must be set before first feed");
  }
  parser->record_callback = callback;
  parser->record_callback_user = user_data;
  parser->record_callback_index = 0;
  parser->record_callback_stopped = 0;
  parser->record_callback_failed = 0;
  db_error_clear(parser->error);
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_stream_text_feed(data_bind_stream_t *parser, const char *data,
                                                 size_t len, DataBindError *error) {
  size_t needed;
  size_t new_cap;
  char *grown;
  DataBindStatus status;

  if (parser == NULL || parser->finished) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_feed", -1, -1,
                        "Invalid stream parser feed state");
  }
  if (len == 0) return DATA_BIND_OK;
  if (data == NULL) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_feed", -1, -1,
                        "Invalid stream parser feed data");
  }

  status = data_bind_stream_sax_feed(parser, data, len, error);
  if (status != DATA_BIND_OK) return status;

  if ((parser->json_stream_candidate && parser->json_stream_active) ||
      parser->xml_stream_candidate) {
    parser->started = 1;
    db_error_clear(error);
    return DATA_BIND_OK;
  }

  needed = parser->size + len;
  if (needed < parser->size) {
    return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                        "Stream input size overflow");
  }
  if (parser->capacity < needed + 1) {
    new_cap = parser->capacity == 0 ? 4096 : parser->capacity * 2;
    while (new_cap < needed + 1) {
      if (new_cap > (SIZE_MAX / 2)) {
        return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                            "Stream input too large");
      }
      new_cap *= 2;
    }
    grown = (char *)realloc(parser->buffer, new_cap);
    if (grown == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed", -1, -1,
                          "Out of memory while extending stream buffer");
    }
    parser->buffer = grown;
    parser->capacity = new_cap;
  }
  memcpy(parser->buffer + parser->size, data, len);
  parser->size += len;
  parser->buffer[parser->size] = '\0';
  parser->started = 1;
  db_error_clear(error);
  return DATA_BIND_OK;
}

int data_bind_stream_feed(data_bind_stream_t *stream, const void *data, size_t len) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  DataBindStatus status;
  if (parser == NULL || parser->feed_fn == NULL) return DATA_BIND_ERR_INVALID_ARG;
  status = parser->feed_fn(parser, (const char *)data, len, parser->error);
  if (status == DATA_BIND_OK) {
    parser->started = 1;
    db_error_clear(parser->error);
  }
  return status;
}

int data_bind_stream_feed_file(data_bind_stream_t *stream, const char *file_path) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  DataBindError *error = parser ? parser->error : NULL;
  char *chunk = NULL;
  turbo_file_t fd;
  DataBindStatus status;
  int close_rc;

  if (parser == NULL || file_path == NULL || file_path[0] == '\0') {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_feed_file", -1, -1,
                        "Invalid stream file feed arguments");
  }
  fd = turbo_fs_open(file_path, TURBO_FS_O_RDONLY, 0);
  if (fd == TURBO_INVALID_FILE) {
    return db_error_set(error, DATA_BIND_ERR_IO, file_path, -1, -1,
                        "Failed to open stream input file");
  }

  chunk = (char *)malloc(DATA_BIND_FILE_STREAM_CHUNK_SIZE);
  if (chunk == NULL) {
    turbo_fs_close(fd);
    return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_feed_file", -1, -1,
                        "Out of memory allocating stream file chunk");
  }

  status = DATA_BIND_OK;
  for (;;) {
    int nread = turbo_fs_read(fd, chunk, DATA_BIND_FILE_STREAM_CHUNK_SIZE);
    if (nread < 0) {
      status = db_error_set(error, DATA_BIND_ERR_IO, file_path, -1, -1,
                            "Failed to read stream input file");
      break;
    }
    if (nread == 0) break;
    status = data_bind_stream_feed(parser, chunk, (size_t)nread);
    if (status != DATA_BIND_OK) break;
  }

  free(chunk);
  close_rc = turbo_fs_close(fd);
  if (status == DATA_BIND_OK && close_rc != 0) {
    status = db_error_set(error, DATA_BIND_ERR_IO, file_path, -1, -1,
                          "Failed to close stream input file");
  }
  return status;
}

static DataBindStatus data_bind_stream_json_finish(data_bind_stream_t *parser,
                                                   DataBindValue **out_value,
                                                   DataBindError *error) {
  const char *path = parser ? parser->path_or_expr : NULL;
  DataBindStatus status = DATA_BIND_OK;

  if (parser == NULL || out_value == NULL || parser->codec == NULL || parser->type_name == NULL ||
      parser->finished) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_finish", -1, -1,
                        "Invalid stream parser finish state");
  }
  status = data_bind_stream_sax_finish(parser, error);
  if (status != DATA_BIND_OK) {
    parser->finished = 1;
    return status;
  }
  if ((parser->json_stream_candidate && parser->json_stream_active) ||
      parser->xml_stream_candidate) {
    *out_value = parser->stream_values;
    parser->stream_values = NULL;
    parser->finished = 1;
    db_error_clear(error);
    return DATA_BIND_OK;
  }
  if (!parser->started) {
    parser->buffer = (char *)realloc(parser->buffer, 1);
    if (parser->buffer == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory while finalizing stream parser");
    }
    parser->buffer[0] = '\0';
    parser->size = 0;
    parser->capacity = 1;
  }
  if (parser->buffer == NULL) {
    parser->buffer = (char *)malloc(1);
    if (parser->buffer == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory while finalizing stream parser");
    }
    parser->buffer[0] = '\0';
    parser->capacity = 1;
  }
  parser->buffer[parser->size] = '\0';

  status = parser->bind_fn(parser->codec, parser->type_name, parser->buffer, parser->size, path,
                           out_value, error);

  if (status == DATA_BIND_OK) {
    status = data_bind_stream_emit_result(parser, *out_value, error);
  }

  parser->finished = 1;
  return status;
}

static DataBindStatus data_bind_stream_xml_finish(data_bind_stream_t *parser,
                                                  DataBindValue **out_value, DataBindError *error) {
  const char *path = parser ? parser->path_or_expr : NULL;
  DataBindStatus status;
  if (parser == NULL || out_value == NULL || parser->codec == NULL || parser->type_name == NULL ||
      parser->finished) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_finish", -1, -1,
                        "Invalid stream finish state");
  }
  status = data_bind_stream_sax_finish(parser, error);
  if (status != DATA_BIND_OK) {
    parser->finished = 1;
    return status;
  }
  if (parser->xml_stream_candidate) {
    *out_value = parser->stream_values;
    parser->stream_values = NULL;
    parser->finished = 1;
    db_error_clear(error);
    return DATA_BIND_OK;
  }
  if (!parser->started) {
    parser->buffer = (char *)realloc(parser->buffer, 1);
    if (parser->buffer == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory while finalizing stream");
    }
    parser->buffer[0] = '\0';
    parser->size = 0;
    parser->capacity = 1;
  }
  if (parser->buffer == NULL) {
    parser->buffer = (char *)malloc(1);
    if (parser->buffer == NULL) {
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory while finalizing stream");
    }
    parser->buffer[0] = '\0';
    parser->capacity = 1;
  }
  parser->buffer[parser->size] = '\0';
  status = parser->bind_fn(parser->codec, parser->type_name, parser->buffer, parser->size, path,
                           out_value, error);
  if (status == DATA_BIND_OK) {
    status = data_bind_stream_emit_result(parser, *out_value, error);
  }
  parser->finished = 1;
  return status;
}

static DataBindStatus data_bind_stream_buffered_finish(data_bind_stream_t *parser,
                                                       DataBindValue **out_value,
                                                       DataBindError *error) {
  DataBindStatus status;
  if (parser == NULL || out_value == NULL || parser->codec == NULL || parser->type_name == NULL ||
      parser->bind_fn == NULL || parser->finished) {
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "data_bind_stream_finish", -1, -1,
                        "Invalid buffered stream finish state");
  }
  status = data_bind_stream_sax_finish(parser, error);
  if (status != DATA_BIND_OK) {
    parser->finished = 1;
    return status;
  }
  if (parser->buffer == NULL) {
    parser->buffer = (char *)malloc(1);
    if (parser->buffer == NULL) {
      parser->finished = 1;
      return db_error_set(error, DATA_BIND_ERR_OOM, "data_bind_stream_finish", -1, -1,
                          "Out of memory finalizing buffered stream");
    }
    parser->buffer[0] = '\0';
    parser->capacity = 1;
  }
  status = parser->bind_fn(parser->codec, parser->type_name, parser->buffer, parser->size,
                           parser->path_or_expr, out_value, error);
  if (status == DATA_BIND_OK) status = data_bind_stream_emit_result(parser, *out_value, error);
  parser->finished = 1;
  return status;
}

int data_bind_stream_finish(data_bind_stream_t *stream) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  if (parser == NULL || parser->finish_fn == NULL || parser->out_value == NULL) {
    return DATA_BIND_ERR_INVALID_ARG;
  }
  return parser->finish_fn(parser, parser->out_value, parser->error);
}

void data_bind_stream_destroy(data_bind_stream_t *stream) {
  data_bind_stream_t *parser = (data_bind_stream_t *)stream;
  size_t i;
  if (parser == NULL) return;
  free(parser->type_name);
  free(parser->path_or_expr);
  free(parser->buffer);
  free(parser->csv_header);
  free(parser->csv_record);
  data_bind_stream_csv_clear_fields(parser);
  free(parser->csv_field);
  free(parser->csv_fields);
  free(parser->csv_field_storage);
  if (parser->csv_filter != NULL) turbo_dsv_filter_destroy(parser->csv_filter);
  if (parser->json_sax != NULL) turbo_json_sax_parser_destroy(parser->json_sax);
  if (parser->yaml_sax != NULL) turbo_yaml_sax_parser_destroy(parser->yaml_sax);
  if (parser->xml_sax != NULL) turbo_xml_sax_parser_destroy(parser->xml_sax);
  for (i = 0; i < parser->json_frame_count; ++i) {
    free(parser->json_frames[i].pending_key);
  }
  free(parser->json_frames);
  free(parser->xml_stream_target);
  tstr_free(parser->xml_capture);
  data_bind_value_free(parser->stream_values);
  if (parser->csv_filter_doc != NULL) {
    turbo_csv_doc_t *doc = parser->csv_filter_doc;
    turbo_free_csv(&doc);
  }
  data_bind_value_free(parser->csv_values);
  free(parser);
}

DataBindStatus data_bind_parse_json(DataBind *codec, const char *type_name, const char *json,
                                    size_t len, DataBindValue **out_value, DataBindError *error) {
  json_value_t *root = NULL;
  DataBindValue *result;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid JSON bind arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_json((const uint8_t *)json, len, &root) != 0 || root == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "JSON parse failed");
  }
  result = bind_json_typed_value(codec->schema_root, type_name, root);
  turbo_free_json(&root);
  if (result == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", "$");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "JSON bind failed for type: %s", type_name);
  }
  *out_value = result;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_parse_json_all(DataBind *codec, const char *type_name, const char *json,
                                        size_t len, DataBindValue **out_value,
                                        DataBindError *error) {
  json_value_t *root = NULL;
  DataBindValue *list;
  size_t i;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid JSON bind_all arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_json((const uint8_t *)json, len, &root) != 0 || root == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "JSON parse failed");
  }
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list != NULL) {
    if (turbo_json_type(root) == TURBO_JSON_ARRAY) {
      for (i = 0; i < turbo_json_array_size(root); i++) {
        DataBindValue *item =
            bind_json_typed_value(codec->schema_root, type_name, turbo_json_array_get(root, i));
        if (item != NULL) {
          if (!dbv_array_push(&list->data.array_val, item)) {
            data_bind_value_free(item);
            data_bind_value_free(list);
            list = NULL;
            break;
          }
        }
      }
    } else {
      DataBindValue *item = bind_json_typed_value(codec->schema_root, type_name, root);
      if (item == NULL || !dbv_array_push(&list->data.array_val, item)) {
        data_bind_value_free(item);
        data_bind_value_free(list);
        list = NULL;
      }
    }
  }
  turbo_free_json(&root);
  if (list == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", "$[]");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "JSON bind_all failed for type: %s", type_name);
  }
  *out_value = list;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_parse_json_path(DataBind *codec, const char *type_name, const char *json,
                                         size_t len, const char *jsonpath,
                                         DataBindValue **out_value, DataBindError *error) {
  json_value_t *root = NULL;
  json_value_t *selected;
  DataBindValue *result;
  char error_path[256];
  const char *path_error;
  if (jsonpath == NULL || jsonpath[0] == '\0')
    return data_bind_parse_json(codec, type_name, json, len, out_value, error);
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid JSONPath bind arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_json((const uint8_t *)json, len, &root) != 0 || root == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "JSON parse failed");
  }
  selected = turbo_json_path_get(root, jsonpath);
  path_error = turbo_json_path_error();
  if (selected == NULL) {
    turbo_free_json(&root);
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    if (path_error != NULL)
      return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1,
                          "JSONPath parse failed: %s", path_error);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "JSONPath selected no value for type: %s", type_name);
  }
  result = bind_json_typed_value(codec->schema_root, type_name, selected);
  turbo_free_json(&root);
  if (result == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "JSONPath bind failed for type: %s", type_name);
  }
  *out_value = result;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_parse_json_path_all(DataBind *codec, const char *type_name,
                                             const char *json, size_t len, const char *jsonpath,
                                             DataBindValue **out_value, DataBindError *error) {
  json_value_t *root = NULL;
  turbo_json_path_result_t *matches = NULL;
  DataBindValue *list;
  size_t i;
  char error_path[256];
  const char *path_error;
  if (jsonpath == NULL || jsonpath[0] == '\0')
    return data_bind_parse_json_all(codec, type_name, json, len, out_value, error);
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid JSONPath bind_all arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_json((const uint8_t *)json, len, &root) != 0 || root == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "JSON parse failed");
  }
  matches = turbo_json_path_query(root, jsonpath);
  path_error = turbo_json_path_error();
  if (matches == NULL && path_error != NULL) {
    turbo_free_json(&root);
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "JSONPath parse failed: %s",
                        path_error);
  }
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list != NULL && matches != NULL) {
    for (i = 0; i < turbo_json_path_result_size(matches); i++) {
      json_value_t *matched = turbo_json_path_result_get(matches, i);
      DataBindValue *item = bind_json_typed_value(codec->schema_root, type_name, matched);
      if (item != NULL) {
        if (!dbv_array_push(&list->data.array_val, item)) {
          data_bind_value_free(item);
          data_bind_value_free(list);
          list = NULL;
          break;
        }
      }
    }
  }
  if (matches != NULL) turbo_json_path_result_free(matches);
  turbo_free_json(&root);
  if (list == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "json", jsonpath);
    return db_error_set(error, DATA_BIND_ERR_OOM, error_path, -1, -1,
                        "Out of memory binding JSONPath result");
  }
  *out_value = list;
  db_error_clear(error);
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_parse_yaml_selected(DataBind *codec, const char *type_name,
                                                    const char *yaml, size_t len,
                                                    const char *yamlpath, int bind_all,
                                                    DataBindValue **out_value,
                                                    DataBindError *error) {
  turbo_yaml_doc_t *doc = NULL;
  turbo_yaml_path_result_t *matches = NULL;
  turbo_yaml_node_t *root;
  DataBindValue *result = NULL;
  char error_path[256];
  size_t count = 0;
  size_t i;

  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || yaml == NULL ||
      out_value == NULL) {
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid YAML bind arguments");
  }
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_yaml((const uint8_t *)yaml, len, &doc) != 0 || doc == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "yaml", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "YAML parse failed");
  }

  root = turbo_yaml_root(doc);
  if (yamlpath != NULL && yamlpath[0] != '\0') {
    matches = turbo_yaml_path_query(doc, NULL, yamlpath);
    if (matches == NULL) {
      turbo_free_yaml(&doc);
      db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
      return db_error_set(error, DATA_BIND_ERR_OOM, error_path, -1, -1,
                          "Out of memory executing YPATH");
    }
    if (turbo_yaml_path_result_error(matches) != NULL) {
      const char *path_error = turbo_yaml_path_result_error(matches);
      size_t error_pos = turbo_yaml_path_result_error_pos(matches);
      char path_message[160];
      snprintf(path_message, sizeof(path_message), "%s", path_error);
      turbo_yaml_path_result_free(matches);
      turbo_free_yaml(&doc);
      db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
      return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1,
                          error_pos <= INT_MAX ? (int)error_pos : -1, "YPATH parse failed: %s",
                          path_message);
    }
    count = turbo_yaml_path_result_size(matches);
  } else if (bind_all && turbo_yaml_node_type(root) == TURBO_YAML_NODE_SEQUENCE) {
    count = turbo_yaml_sequence_size(root);
  } else if (root != NULL) {
    count = 1;
  }

  if (count == 0) {
    turbo_yaml_path_result_free(matches);
    turbo_free_yaml(&doc);
    db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "YAML selection matched no values");
  }

  if (bind_all) {
    result = dbv_new(DATA_BIND_VALUE_LIST);
    if (result == NULL) {
      turbo_yaml_path_result_free(matches);
      turbo_free_yaml(&doc);
      return db_error_set(error, DATA_BIND_ERR_OOM, "yaml", -1, -1,
                          "Out of memory creating YAML result list");
    }
  }

  for (i = 0; i < (bind_all ? count : 1); i++) {
    turbo_yaml_node_t *node;
    json_value_t *json_value;
    DataBindValue *bound;
    if (matches != NULL) node = turbo_yaml_path_result_get(matches, i);
    else if (bind_all && turbo_yaml_node_type(root) == TURBO_YAML_NODE_SEQUENCE)
      node = turbo_yaml_sequence_get(root, i);
    else node = root;

    json_value = turbo_yaml_node_to_json(doc, node);
    if (json_value == NULL) {
      data_bind_value_free(result);
      turbo_yaml_path_result_free(matches);
      turbo_free_yaml(&doc);
      db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
      return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                          "YAML value cannot be represented as JSON-compatible data");
    }
    bound = bind_json_typed_value(codec->schema_root, type_name, json_value);
    turbo_free_json(&json_value);
    if (bound == NULL) {
      data_bind_value_free(result);
      turbo_yaml_path_result_free(matches);
      turbo_free_yaml(&doc);
      db_error_format_path(error_path, sizeof(error_path), "yaml", yamlpath);
      return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                          "YAML bind failed for type: %s", type_name);
    }
    if (!bind_all) {
      result = bound;
    } else if (!dbv_array_push(&result->data.array_val, bound)) {
      data_bind_value_free(bound);
      data_bind_value_free(result);
      turbo_yaml_path_result_free(matches);
      turbo_free_yaml(&doc);
      return db_error_set(error, DATA_BIND_ERR_OOM, "yaml", -1, -1,
                          "Out of memory appending YAML result");
    }
  }

  turbo_yaml_path_result_free(matches);
  turbo_free_yaml(&doc);
  *out_value = result;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_parse_yaml(DataBind *codec, const char *type_name, const char *yaml,
                                    size_t len, DataBindValue **out_value, DataBindError *error) {
  return data_bind_parse_yaml_selected(codec, type_name, yaml, len, NULL, 0, out_value, error);
}

DataBindStatus data_bind_parse_yaml_all(DataBind *codec, const char *type_name, const char *yaml,
                                        size_t len, DataBindValue **out_value,
                                        DataBindError *error) {
  return data_bind_parse_yaml_selected(codec, type_name, yaml, len, NULL, 1, out_value, error);
}

DataBindStatus data_bind_parse_yaml_path(DataBind *codec, const char *type_name, const char *yaml,
                                         size_t len, const char *yamlpath,
                                         DataBindValue **out_value, DataBindError *error) {
  if (yamlpath == NULL || yamlpath[0] == '\0')
    return data_bind_parse_yaml(codec, type_name, yaml, len, out_value, error);
  return data_bind_parse_yaml_selected(codec, type_name, yaml, len, yamlpath, 0, out_value, error);
}

DataBindStatus data_bind_parse_yaml_path_all(DataBind *codec, const char *type_name,
                                             const char *yaml, size_t len, const char *yamlpath,
                                             DataBindValue **out_value, DataBindError *error) {
  if (yamlpath == NULL || yamlpath[0] == '\0')
    return data_bind_parse_yaml_all(codec, type_name, yaml, len, out_value, error);
  return data_bind_parse_yaml_selected(codec, type_name, yaml, len, yamlpath, 1, out_value, error);
}

DataBindStatus data_bind_parse_csv(DataBind *codec, const char *type_name, const char *csv,
                                   size_t len, size_t row, DataBindValue **out_value,
                                   DataBindError *error) {
  turbo_csv_doc_t *doc = NULL;
  data_bind_csv_headers_t headers = {0};
  turbo_csv_options_t opts = {true, ',', '"', true};
  DataBindValue *result = NULL;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || csv == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid CSV bind arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_csv_opts((const uint8_t *)csv, len, &opts, &doc) != 0 || doc == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "CSV parse failed");
  }
  if (csv_parse_header_names(csv, len, &headers))
    result = bind_csv_typed_value(codec->schema_root, type_name, doc, row, &headers, "");
  csv_headers_free(&headers);
  turbo_free_csv(&doc);
  if (result == NULL) {
    snprintf(error_path, sizeof(error_path), "csv: row %zu", row);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, (int)row, -1,
                        "CSV bind failed for type: %s", type_name);
  }
  *out_value = result;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_parse_csv_all(DataBind *codec, const char *type_name, const char *csv,
                                       size_t len, DataBindValue **out_value,
                                       DataBindError *error) {
  turbo_csv_doc_t *doc = NULL;
  data_bind_csv_headers_t headers = {0};
  turbo_csv_options_t opts = {true, ',', '"', true};
  DataBindValue *list = NULL;
  size_t row;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || csv == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid CSV bind_all arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_csv_opts((const uint8_t *)csv, len, &opts, &doc) != 0 || doc == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "CSV parse failed");
  }
  if (csv_parse_header_names(csv, len, &headers)) {
    list = dbv_new(DATA_BIND_VALUE_LIST);
    if (list != NULL) {
      for (row = 0; row < turbo_csv_row_count(doc); row++) {
        DataBindValue *item =
            bind_csv_typed_value(codec->schema_root, type_name, doc, row, &headers, "");
        if (item != NULL) {
          if (!dbv_array_push(&list->data.array_val, item)) {
            data_bind_value_free(item);
            data_bind_value_free(list);
            list = NULL;
            break;
          }
        }
      }
    }
  }
  csv_headers_free(&headers);
  turbo_free_csv(&doc);
  if (list == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", "multiple rows");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "CSV bind_all failed for type: %s", type_name);
  }
  *out_value = list;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_parse_csv_path(DataBind *codec, const char *type_name, const char *csv,
                                        size_t len, const char *csvpath, DataBindValue **out_value,
                                        DataBindError *error) {
  turbo_csv_doc_t *bind_doc = NULL;
  turbo_csv_doc_t *filter_doc = NULL;
  turbo_dsv_filter_t *filter = NULL;
  data_bind_csv_headers_t headers = {0};
  turbo_csv_options_t bind_opts = {true, ',', '"', true};
  turbo_csv_options_t filter_opts = {false, ',', '"', true};
  DataBindValue *list = NULL;
  size_t raw_row;
  char error_path[256];
  DataBindStatus failure = DATA_BIND_OK;
  const char *failure_msg = NULL;
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || csv == NULL ||
      csvpath == NULL || csvpath[0] == '\0' || out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid CSVPath bind arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "csv", csvpath);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_csv_opts((const uint8_t *)csv, len, &bind_opts, &bind_doc) != 0 ||
      bind_doc == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "CSV parse failed");
  }
  if (turbo_parse_csv_opts((const uint8_t *)csv, len, &filter_opts, &filter_doc) != 0 ||
      filter_doc == NULL) {
    turbo_free_csv(&bind_doc);
    db_error_format_path(error_path, sizeof(error_path), "csv", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "CSVPath parse failed");
  }
  if (!csv_parse_header_names(csv, len, &headers)) {
    turbo_free_csv(&filter_doc);
    turbo_free_csv(&bind_doc);
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "CSV header parse failed");
  }
  filter = turbo_dsv_filter_create(filter_doc, 0);
  if (filter == NULL || !turbo_dsv_filter_compile(filter, csvpath)) {
    const char *filter_error = turbo_dsv_filter_error(filter);
    char filter_msg[128];
    snprintf(filter_msg, sizeof(filter_msg), "%s",
             filter_error != NULL && filter_error[0] != '\0' ? filter_error : "invalid filter");
    if (filter != NULL) turbo_dsv_filter_destroy(filter);
    csv_headers_free(&headers);
    turbo_free_csv(&filter_doc);
    turbo_free_csv(&bind_doc);
    db_error_format_path(error_path, sizeof(error_path), "csv", csvpath);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1,
                        "CSVPath compile failed: %s", filter_msg);
  }
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list != NULL) {
    for (raw_row = 1; raw_row < turbo_csv_row_count(filter_doc); raw_row++) {
      int match = turbo_dsv_filter_check_row(filter, raw_row);
      if (match < 0) {
        data_bind_value_free(list);
        list = NULL;
        failure = DATA_BIND_ERR_PARSE;
        failure_msg = "CSVPath evaluation failed";
        break;
      }
      if (match) {
        size_t bind_row = raw_row - 1;
        DataBindValue *item =
            bind_csv_typed_value(codec->schema_root, type_name, bind_doc, bind_row, &headers, "");
        if (item == NULL) {
          data_bind_value_free(list);
          list = NULL;
          failure = DATA_BIND_ERR_TYPE_MISMATCH;
          failure_msg = "CSVPath row bind failed";
          break;
        }
        if (item != NULL) {
          if (!dbv_array_push(&list->data.array_val, item)) {
            data_bind_value_free(item);
            data_bind_value_free(list);
            list = NULL;
            failure = DATA_BIND_ERR_OOM;
            failure_msg = "Out of memory binding CSVPath rows";
            break;
          }
        }
      }
    }
  }
  turbo_dsv_filter_destroy(filter);
  csv_headers_free(&headers);
  turbo_free_csv(&filter_doc);
  turbo_free_csv(&bind_doc);
  if (list == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "csv", csvpath);
    return db_error_set(error, failure != DATA_BIND_OK ? failure : DATA_BIND_ERR_TYPE_MISMATCH,
                        error_path, -1, -1, "%s for type: %s",
                        failure_msg != NULL ? failure_msg : "CSVPath bind failed", type_name);
  }
  *out_value = list;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_parse_xml(DataBind *codec, const char *type_name, const char *xml,
                                   size_t len, DataBindValue **out_value, DataBindError *error) {
  turbo_xml_doc_t *doc = NULL;
  DataBindValue *result = NULL;
  char error_path[128];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || xml == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG, "Invalid XML bind arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "xml", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_xml((const uint8_t *)xml, len, &doc) != 0 || doc == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "xml", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "XML parse failed");
  }
  result = bind_xml_typed_value(codec->schema_root, type_name, doc, "/*");
  turbo_free_xml(&doc);
  if (result == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "xml", "/*");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "XML bind failed for type: %s", type_name);
  }
  *out_value = result;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_parse_xml_path_all(DataBind *codec, const char *type_name, const char *xml,
                                            size_t len, const char *xmlpath,
                                            DataBindValue **out_value, DataBindError *error) {
  turbo_xml_doc_t *doc = NULL;
  DataBindValue *list = NULL;
  char error_path[256];
  if (out_value != NULL) *out_value = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || xml == NULL ||
      out_value == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid XML bind_all arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    db_error_format_path(error_path, sizeof(error_path), "xml", NULL);
    return db_error_set(error, DATA_BIND_ERR_TYPE_NOT_FOUND, error_path, -1, -1,
                        "Type not found: %s", type_name);
  }
  if (turbo_parse_xml((const uint8_t *)xml, len, &doc) != 0 || doc == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "xml", NULL);
    return db_error_set(error, DATA_BIND_ERR_PARSE, error_path, -1, -1, "XML parse failed");
  }
  list = dbv_new(DATA_BIND_VALUE_LIST);
  if (list != NULL) {
    if (xmlpath == NULL || xmlpath[0] == '\0') {
      DataBindValue *item = bind_xml_typed_value(codec->schema_root, type_name, doc, "/*");
      if (item == NULL || !dbv_array_push(&list->data.array_val, item)) {
        data_bind_value_free(item);
        data_bind_value_free(list);
        list = NULL;
      }
    } else {
      turbo_xml_list_t nodes;
      int index = 0;
      turbo_xml_xpath_query(doc, xmlpath, &nodes);
      for (index = 0; index < nodes.len; index++) {
        char item_path[320];
        DataBindValue *item;
        if (snprintf(item_path, sizeof(item_path), "%s[%d]", xmlpath, index + 1) >=
            (int)sizeof(item_path)) {
          data_bind_value_free(list);
          list = NULL;
          break;
        }
        item = bind_xml_typed_value(codec->schema_root, type_name, doc, item_path);
        if (item != NULL) {
          if (!dbv_array_push(&list->data.array_val, item)) {
            data_bind_value_free(item);
            data_bind_value_free(list);
            list = NULL;
            break;
          }
        }
      }
      turbo_xml_list_free(&nodes);
    }
  }
  turbo_free_xml(&doc);
  if (list == NULL) {
    db_error_format_path(error_path, sizeof(error_path), "xml",
                         xmlpath != NULL && xmlpath[0] != '\0' ? xmlpath : "/*");
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, error_path, -1, -1,
                        "XML bind_all failed for type: %s", type_name);
  }
  *out_value = list;
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_validate_json(DataBind *codec, const char *type_name, const char *json,
                                       size_t len, DataBindError *error) {
  json_value_t *root = NULL;
  size_t i;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || json == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid JSON validate arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_NOT_FOUND, "Type not found: %s",
                          type_name);
  }
  if (turbo_parse_json((const uint8_t *)json, len, &root) != 0 || root == NULL) {
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "JSON parse failed");
  }
  if (turbo_json_type(root) == TURBO_JSON_ARRAY) {
    for (i = 0; i < turbo_json_array_size(root); i++) {
      DataBindValue *item =
          bind_json_typed_value(codec->schema_root, type_name, turbo_json_array_get(root, i));
      if (item == NULL) {
        turbo_free_json(&root);
        return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                              "JSON validation failed for type: %s", type_name);
      }
      data_bind_value_free(item);
    }
  } else {
    DataBindValue *item = bind_json_typed_value(codec->schema_root, type_name, root);
    if (item == NULL) {
      turbo_free_json(&root);
      return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                            "JSON validation failed for type: %s", type_name);
    }
    data_bind_value_free(item);
  }
  turbo_free_json(&root);
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_validate_json_path(DataBind *codec, const char *type_name,
                                            const char *json, size_t len, const char *jsonpath,
                                            DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (jsonpath == NULL || jsonpath[0] == '\0')
    return data_bind_validate_json(codec, type_name, json, len, error);
  status = data_bind_parse_json_path(codec, type_name, json, len, jsonpath, &value, error);
  data_bind_value_free(value);
  return status;
}

DataBindStatus data_bind_validate_yaml(DataBind *codec, const char *type_name, const char *yaml,
                                       size_t len, DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status = data_bind_parse_yaml_all(codec, type_name, yaml, len, &value, error);
  data_bind_value_free(value);
  return status;
}

DataBindStatus data_bind_validate_yaml_path(DataBind *codec, const char *type_name,
                                            const char *yaml, size_t len, const char *yamlpath,
                                            DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (yamlpath == NULL || yamlpath[0] == '\0')
    return data_bind_validate_yaml(codec, type_name, yaml, len, error);
  status = data_bind_parse_yaml_path(codec, type_name, yaml, len, yamlpath, &value, error);
  data_bind_value_free(value);
  return status;
}

DataBindStatus data_bind_validate_csv(DataBind *codec, const char *type_name, const char *csv,
                                      size_t len, DataBindError *error) {
  turbo_csv_doc_t *doc = NULL;
  data_bind_csv_headers_t headers = {0};
  turbo_csv_options_t opts = {true, ',', '"', true};
  size_t row;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || csv == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid CSV validate arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_NOT_FOUND, "Type not found: %s",
                          type_name);
  }
  if (turbo_parse_csv_opts((const uint8_t *)csv, len, &opts, &doc) != 0 || doc == NULL) {
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "CSV parse failed");
  }
  if (!csv_parse_header_names(csv, len, &headers)) {
    turbo_free_csv(&doc);
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "CSV header parse failed");
  }
  for (row = 0; row < turbo_csv_row_count(doc); row++) {
    DataBindValue *item =
        bind_csv_typed_value(codec->schema_root, type_name, doc, row, &headers, "");
    if (item == NULL) {
      csv_headers_free(&headers);
      turbo_free_csv(&doc);
      return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                            "CSV validation failed for type: %s", type_name);
    }
    data_bind_value_free(item);
  }
  csv_headers_free(&headers);
  turbo_free_csv(&doc);
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_validate_csv_path(DataBind *codec, const char *type_name, const char *csv,
                                           size_t len, const char *csvpath, DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status =
      data_bind_parse_csv_path(codec, type_name, csv, len, csvpath, &value, error);
  data_bind_value_free(value);
  return status;
}

DataBindStatus data_bind_validate_xml_path(DataBind *codec, const char *type_name, const char *xml,
                                           size_t len, const char *xmlpath, DataBindError *error) {
  turbo_xml_doc_t *doc = NULL;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || xml == NULL)
    return db_codec_error(codec, error, DATA_BIND_ERR_INVALID_ARG,
                          "Invalid XML validate arguments");
  codec->error[0] = '\0';
  if (!bind_type_supported(codec->schema_root, type_name)) {
    return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_NOT_FOUND, "Type not found: %s",
                          type_name);
  }
  if (turbo_parse_xml((const uint8_t *)xml, len, &doc) != 0 || doc == NULL) {
    return db_codec_error(codec, error, DATA_BIND_ERR_PARSE, "XML parse failed");
  }
  if (xmlpath == NULL || xmlpath[0] == '\0') {
    DataBindValue *item = bind_xml_typed_value(codec->schema_root, type_name, doc, "/*");
    if (item == NULL) {
      turbo_free_xml(&doc);
      return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                            "XML validation failed for type: %s", type_name);
    }
    data_bind_value_free(item);
  } else {
    turbo_xml_list_t nodes;
    int index;
    turbo_xml_xpath_query(doc, xmlpath, &nodes);
    for (index = 0; index < nodes.len; index++) {
      char item_path[320];
      DataBindValue *item;
      if (snprintf(item_path, sizeof(item_path), "%s[%d]", xmlpath, index + 1) >=
          (int)sizeof(item_path)) {
        turbo_xml_list_free(&nodes);
        turbo_free_xml(&doc);
        return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                              "XML validation path is too long");
      }
      item = bind_xml_typed_value(codec->schema_root, type_name, doc, item_path);
      if (item == NULL) {
        turbo_xml_list_free(&nodes);
        turbo_free_xml(&doc);
        return db_codec_error(codec, error, DATA_BIND_ERR_TYPE_MISMATCH,
                              "XML validation failed for type: %s", type_name);
      }
      data_bind_value_free(item);
    }
    turbo_xml_list_free(&nodes);
  }
  turbo_free_xml(&doc);
  db_error_clear(error);
  return DATA_BIND_OK;
}

#define DATA_BIND_JSON_MAX_DEPTH 64u

static DataBindStatus data_bind_object_take(const char *type_name, DataBindValue *value,
                                            DataBindObject **out_object, DataBindError *error) {
  DataBindObject *object;
  size_t type_len;

  if (out_object == NULL || type_name == NULL || value == NULL) {
    data_bind_value_free(value);
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object arguments");
  }
  *out_object = NULL;
  type_len = strlen(type_name);
  object = (DataBindObject *)calloc(1, sizeof(*object));
  if (object != NULL) {
    object->type_name = (char *)malloc(type_len + 1);
    if (object->type_name != NULL) {
      memcpy(object->type_name, type_name, type_len + 1);
      object->value = value;
      *out_object = object;
      db_error_clear(error);
      return DATA_BIND_OK;
    }
  }
  free(object);
  data_bind_value_free(value);
  return db_error_set(error, DATA_BIND_ERR_OOM, NULL, -1, -1,
                      "Out of memory creating DataBind object");
}

DataBindStatus data_bind_object_from_json(DataBind *codec, const char *type_name, const char *json,
                                          size_t len, DataBindObject **out_object,
                                          DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse_json(codec, type_name, json, len, &value, error);
  return status == DATA_BIND_OK ? data_bind_object_take(type_name, value, out_object, error)
                                : status;
}

DataBindStatus data_bind_object_from_bin(DataBind *codec, const char *type_name,
                                         const uint8_t *data, size_t len,
                                         DataBindObject **out_object, DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse(codec, type_name, data, len, &value, error);
  return status == DATA_BIND_OK ? data_bind_object_take(type_name, value, out_object, error)
                                : status;
}

DataBindStatus data_bind_record_from_bin(DataBind *codec, const char *type_name,
                                         const uint8_t *data, size_t len,
                                         DataBindRecord **out_object, DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind Record output");
  status = data_bind_parse_record_v1(codec, type_name, data, len, &value, error);
  return status == DATA_BIND_OK ? data_bind_object_take(type_name, value, out_object, error)
                                : status;
}

DataBindStatus data_bind_object_from_yaml(DataBind *codec, const char *type_name, const char *yaml,
                                          size_t len, DataBindObject **out_object,
                                          DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse_yaml(codec, type_name, yaml, len, &value, error);
  return status == DATA_BIND_OK ? data_bind_object_take(type_name, value, out_object, error)
                                : status;
}

DataBindStatus data_bind_object_from_xml(DataBind *codec, const char *type_name, const char *xml,
                                         size_t len, DataBindObject **out_object,
                                         DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse_xml(codec, type_name, xml, len, &value, error);
  return status == DATA_BIND_OK ? data_bind_object_take(type_name, value, out_object, error)
                                : status;
}

DataBindStatus data_bind_object_from_csv(DataBind *codec, const char *type_name, const char *csv,
                                         size_t len, size_t row, DataBindObject **out_object,
                                         DataBindError *error) {
  DataBindValue *value = NULL;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (out_object == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid DataBind object output");
  status = data_bind_parse_csv(codec, type_name, csv, len, row, &value, error);
  return status == DATA_BIND_OK ? data_bind_object_take(type_name, value, out_object, error)
                                : status;
}

DataBindStatus data_bind_object_clone(const DataBindObject *object, DataBindObject **out_object) {
  DataBindValue *value = NULL;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindStatus status;
  if (out_object != NULL) *out_object = NULL;
  if (object == NULL || out_object == NULL) return DATA_BIND_ERR_INVALID_ARG;
  status = data_bind_value_clone(object->value, &value);
  if (status != DATA_BIND_OK) return status;
  return data_bind_object_take(object->type_name, value, out_object, &error);
}

const char *data_bind_object_type_name(const DataBindObject *object) {
  return object != NULL ? object->type_name : NULL;
}

const DataBindValue *data_bind_object_value(const DataBindObject *object) {
  return object != NULL ? object->value : NULL;
}

DataBindStatus data_bind_object_serialize_bin_into(DataBind *codec, const DataBindObject *object,
                                                   uint8_t *output, size_t capacity,
                                                   size_t *out_len, DataBindError *error) {
  emit_field_array_t fields = {0};
  data_bind_binary_writer_t writer;
  DataBindStatus status;
  size_t required = 0;
  if (out_len != NULL) *out_len = 0;
  if (output == NULL || out_len == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "binary", -1, -1,
                        "Invalid binary output buffer arguments");
  status = db_binary_plan(codec, object, &fields, error);
  if (status != DATA_BIND_OK) return status;
  status = db_binary_measure(&fields, object->value, &required, error);
  if (status != DATA_BIND_OK) goto cleanup;
  *out_len = required;
  if (capacity < required) {
    status = db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "binary", -1, -1,
                          "Binary output buffer is too small");
    goto cleanup;
  }
  writer.data = output;
  writer.capacity = capacity;
  writer.offset = 0;
  writer.error = error;
  status = db_binary_write_fields(&writer, &fields, object->value);
  if (status == DATA_BIND_OK) db_error_clear(error);

cleanup:
  emit_field_array_free(&fields);
  return status;
}

DataBindStatus data_bind_object_serialize_bin(DataBind *codec, const DataBindObject *object,
                                              uint8_t **out_bin, size_t *out_len,
                                              DataBindError *error) {
  emit_field_array_t fields = {0};
  data_bind_binary_writer_t writer;
  DataBindStatus status;
  uint8_t *data = NULL;
  size_t required = 0;
  if (out_bin != NULL) *out_bin = NULL;
  if (out_len != NULL) *out_len = 0;
  if (out_bin == NULL || out_len == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, "binary", -1, -1,
                        "Invalid binary output arguments");
  status = db_binary_plan(codec, object, &fields, error);
  if (status != DATA_BIND_OK) return status;
  status = db_binary_measure(&fields, object->value, &required, error);
  if (status != DATA_BIND_OK) goto cleanup;
  data = (uint8_t *)malloc(required != 0 ? required : 1);
  if (data == NULL) {
    status = db_error_set(error, DATA_BIND_ERR_OOM, object->type_name, -1, -1,
                          "Out of memory serializing binary object");
    goto cleanup;
  }
  writer.data = data;
  writer.capacity = required;
  writer.offset = 0;
  writer.error = error;
  status = db_binary_write_fields(&writer, &fields, object->value);
  if (status == DATA_BIND_OK) {
    *out_bin = data;
    *out_len = required;
    data = NULL;
    db_error_clear(error);
  }

cleanup:
  free(data);
  emit_field_array_free(&fields);
  return status;
}

static json_value_t *data_bind_value_to_json(const DataBindValue *value, unsigned depth,
                                             DataBindStatus *status) {
  json_value_t *json = NULL;
  size_t i;
  char text[128];

  if (value == NULL || depth > DATA_BIND_JSON_MAX_DEPTH) {
    *status = DATA_BIND_ERR_RUNTIME;
    return NULL;
  }

  switch (value->kind) {
  case DATA_BIND_VALUE_NULL:
    json = turbo_json_create_null();
    break;
  case DATA_BIND_VALUE_INT:
    json = turbo_json_create_int64(value->data.int_val);
    break;
  case DATA_BIND_VALUE_INT64:
    json = turbo_json_create_int64(value->data.int64_val);
    break;
  case DATA_BIND_VALUE_UINT64:
    json = turbo_json_create_uint64(value->data.uint64_val);
    break;
  case DATA_BIND_VALUE_DOUBLE:
    if (!isfinite(value->data.double_val)) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = turbo_json_create_number(value->data.double_val);
    break;
  case DATA_BIND_VALUE_BOOL:
    json = turbo_json_create_bool(value->data.bool_val != 0);
    break;
  case DATA_BIND_VALUE_STRING:
    json = turbo_json_create_string(value->data.string_val.ptr);
    break;
  case DATA_BIND_VALUE_BYTES:
    if (!tstr_v_utf8_valid(
            tstr_v_from_buf((const char *)value->data.bytes_val.ptr, value->data.bytes_val.len))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = turbo_json_create_string_n((const char *)value->data.bytes_val.ptr,
                                      value->data.bytes_val.len);
    break;
  case DATA_BIND_VALUE_UUID:
    if (turbo_uuid_format(&value->data.uuid_val, text, sizeof(text)) != TURBO_OK) {
      *status = DATA_BIND_ERR_RUNTIME;
      return NULL;
    }
    json = turbo_json_create_string(text);
    break;
  case DATA_BIND_VALUE_DATETIME: {
    time_t timestamp = turbo_datetime_to_time(&value->data.datetime_val);
    if (timestamp == (time_t)-1 ||
        turbo_datetime_format_rfc822(timestamp, text, sizeof(text)) < 0) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = turbo_json_create_string(text);
    break;
  }
  case DATA_BIND_VALUE_DATE:
    if (!db_date_to_text(value->data.date_val, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = turbo_json_create_string(text);
    break;
  case DATA_BIND_VALUE_TIME:
    if (!db_time_to_text(value->data.time_val, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = turbo_json_create_string(text);
    break;
  case DATA_BIND_VALUE_DURATION:
    if (!db_duration_to_text(value->data.duration_ms, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = turbo_json_create_string(text);
    break;
  case DATA_BIND_VALUE_DECIMAL:
    if (!db_decimal_to_text(value->data.decimal_val, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = turbo_json_create_string(text);
    break;
  case DATA_BIND_VALUE_BIGINT:
    json = turbo_json_create_string(value->data.bigint_val.ptr);
    break;
  case DATA_BIND_VALUE_MONEY: {
    json_value_t *amount;
    json_value_t *currency;
    if (!db_decimal_to_text(value->data.money_val.amount, text, sizeof(text))) {
      *status = DATA_BIND_ERR_TYPE_MISMATCH;
      return NULL;
    }
    json = turbo_json_create_object();
    amount = turbo_json_create_string(text);
    currency = turbo_json_create_string(value->data.money_val.currency);
    if (json == NULL || amount == NULL || currency == NULL) {
      turbo_free_json(&amount);
      turbo_free_json(&currency);
      turbo_free_json(&json);
      *status = DATA_BIND_ERR_OOM;
      return NULL;
    }
    if (!turbo_json_object_add_checked(json, "amount", amount)) {
      turbo_free_json(&amount);
      turbo_free_json(&currency);
      turbo_free_json(&json);
      *status = DATA_BIND_ERR_OOM;
      return NULL;
    }
    amount = NULL;
    if (!turbo_json_object_add_checked(json, "currency", currency)) {
      turbo_free_json(&currency);
      turbo_free_json(&json);
      *status = DATA_BIND_ERR_OOM;
      return NULL;
    }
    break;
  }
  case DATA_BIND_VALUE_OBJECT:
    json = turbo_json_create_object();
    for (i = 0; json != NULL && i < value->data.object_val.count; ++i) {
      json_value_t *child =
          data_bind_value_to_json(value->data.object_val.items[i].value, depth + 1, status);
      if (child == NULL ||
          !turbo_json_object_add_checked(json, value->data.object_val.items[i].name, child)) {
        turbo_free_json(&child);
        turbo_free_json(&json);
        if (*status == DATA_BIND_OK) *status = DATA_BIND_ERR_OOM;
      }
    }
    break;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET:
    json = turbo_json_create_array();
    for (i = 0; json != NULL && i < value->data.array_val.count; ++i) {
      json_value_t *child =
          data_bind_value_to_json(value->data.array_val.items[i], depth + 1, status);
      if (child == NULL || !turbo_json_array_add_checked(json, child)) {
        turbo_free_json(&child);
        turbo_free_json(&json);
        if (*status == DATA_BIND_OK) *status = DATA_BIND_ERR_OOM;
      }
    }
    break;
  case DATA_BIND_VALUE_MAP:
    json = turbo_json_create_object();
    for (i = 0; json != NULL && i < value->data.map_val.count; ++i) {
      json_value_t *child =
          data_bind_value_to_json(value->data.map_val.items[i].value, depth + 1, status);
      if (child == NULL ||
          !turbo_json_object_add_checked(json, value->data.map_val.items[i].key, child)) {
        turbo_free_json(&child);
        turbo_free_json(&json);
        if (*status == DATA_BIND_OK) *status = DATA_BIND_ERR_OOM;
      }
    }
    break;
  default:
    *status = DATA_BIND_ERR_TYPE_MISMATCH;
    return NULL;
  }

  if (json == NULL && *status == DATA_BIND_OK) *status = DATA_BIND_ERR_OOM;
  return json;
}

DataBindStatus data_bind_object_serialize_json(const DataBindObject *object, char **out_json,
                                               size_t *out_len, DataBindError *error) {
  DataBindStatus status = DATA_BIND_OK;
  json_value_t *json;

  if (out_json != NULL) *out_json = NULL;
  if (out_len != NULL) *out_len = 0;
  if (object == NULL || object->value == NULL || out_json == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid JSON serialize arguments");

  json = data_bind_value_to_json(object->value, 0, &status);
  if (json == NULL)
    return db_error_set(error, status, "json", -1, -1,
                        status == DATA_BIND_ERR_TYPE_MISMATCH
                            ? "DataBind value cannot be represented as UTF-8 JSON"
                            : "Failed to construct JSON document");
  *out_json = turbo_json_serialize(json, out_len);
  turbo_free_json(&json);
  if (*out_json == NULL)
    return db_error_set(error, DATA_BIND_ERR_OOM, "json", -1, -1, "Out of memory serializing JSON");
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_object_serialize_yaml(const DataBindObject *object, char **out_yaml,
                                               size_t *out_len, DataBindError *error) {
  DataBindStatus status = DATA_BIND_OK;
  json_value_t *json;
  turbo_yaml_doc_t *yaml;

  if (out_yaml != NULL) *out_yaml = NULL;
  if (out_len != NULL) *out_len = 0;
  if (object == NULL || object->value == NULL || out_yaml == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid YAML serialize arguments");
  json = data_bind_value_to_json(object->value, 0, &status);
  if (!json)
    return db_error_set(error, status, "yaml", -1, -1,
                        "DataBind value cannot be represented as YAML");
  yaml = turbo_yaml_from_json(json);
  turbo_free_json(&json);
  if (!yaml)
    return db_error_set(error, DATA_BIND_ERR_OOM, "yaml", -1, -1,
                        "Failed to construct YAML document");
  *out_yaml = turbo_yaml_serialize(yaml, out_len);
  turbo_free_yaml(&yaml);
  if (!*out_yaml)
    return db_error_set(error, DATA_BIND_ERR_OOM, "yaml", -1, -1,
                        "Failed to serialize YAML document");
  db_error_clear(error);
  return DATA_BIND_OK;
}

static int data_bind_xml_name_valid(const char *name) {
  const unsigned char *p = (const unsigned char *)name;
  if (!p || !(isalpha(*p) || *p == '_' || *p == ':')) return 0;
  for (++p; *p; ++p)
    if (!(isalnum(*p) || *p == '_' || *p == ':' || *p == '-' || *p == '.')) return 0;
  return 1;
}

static int data_bind_standard_scalar_text(const DataBindValue *value, char *text, size_t size) {
  switch (value->kind) {
  case DATA_BIND_VALUE_INT:
    return snprintf(text, size, "%d", value->data.int_val) > 0;
  case DATA_BIND_VALUE_INT64:
    return snprintf(text, size, "%lld", (long long)value->data.int64_val) > 0;
  case DATA_BIND_VALUE_UINT64:
    return snprintf(text, size, "%llu", (unsigned long long)value->data.uint64_val) > 0;
  case DATA_BIND_VALUE_DOUBLE:
    return isfinite(value->data.double_val) &&
           snprintf(text, size, "%.17g", value->data.double_val) > 0;
  case DATA_BIND_VALUE_BOOL:
    return snprintf(text, size, "%s", value->data.bool_val ? "true" : "false") > 0;
  case DATA_BIND_VALUE_UUID:
    return turbo_uuid_format(&value->data.uuid_val, text, size) == TURBO_OK;
  case DATA_BIND_VALUE_DATETIME: {
    time_t timestamp = turbo_datetime_to_time(&value->data.datetime_val);
    return timestamp != (time_t)-1 && turbo_datetime_format_rfc822(timestamp, text, size) >= 0;
  }
  case DATA_BIND_VALUE_DATE:
    return db_date_to_text(value->data.date_val, text, size);
  case DATA_BIND_VALUE_TIME:
    return db_time_to_text(value->data.time_val, text, size);
  case DATA_BIND_VALUE_DURATION:
    return db_duration_to_text(value->data.duration_ms, text, size);
  case DATA_BIND_VALUE_DECIMAL:
    return db_decimal_to_text(value->data.decimal_val, text, size);
  case DATA_BIND_VALUE_BIGINT:
    return snprintf(text, size, "%s", value->data.bigint_val.ptr) > 0;
  case DATA_BIND_VALUE_MONEY:
    return db_money_to_text(value->data.money_val, text, size);
  default:
    return 0;
  }
}

static int data_bind_value_to_xml(const DataBindValue *value, turbo_xml_node_t *node,
                                  unsigned depth) {
  char text[128];
  size_t i;
  if (!value || !node || depth > DATA_BIND_JSON_MAX_DEPTH) return 0;
  switch (value->kind) {
  case DATA_BIND_VALUE_STRING:
    return value->data.string_val.ptr && turbo_xml_set_text(node, value->data.string_val.ptr) == 0;
  case DATA_BIND_VALUE_BYTES:
    if (memchr(value->data.bytes_val.ptr, '\0', value->data.bytes_val.len)) return 0;
    if (value->data.bytes_val.len >= sizeof(text)) return 0;
    memcpy(text, value->data.bytes_val.ptr, value->data.bytes_val.len);
    text[value->data.bytes_val.len] = '\0';
    return tstr_v_utf8_valid(tstr_v_from_buf(text, value->data.bytes_val.len)) &&
           turbo_xml_set_text(node, text) == 0;
  case DATA_BIND_VALUE_BIGINT:
    return value->data.bigint_val.ptr && turbo_xml_set_text(node, value->data.bigint_val.ptr) == 0;
  case DATA_BIND_VALUE_OBJECT:
    for (i = 0; i < value->data.object_val.count; ++i) {
      const char *name = value->data.object_val.items[i].name;
      const DataBindValue *child_value = value->data.object_val.items[i].value;
      if (child_value->kind == DATA_BIND_VALUE_LIST || child_value->kind == DATA_BIND_VALUE_SET) {
        for (size_t j = 0; j < child_value->data.array_val.count; ++j) {
          turbo_xml_node_t *child = turbo_xml_add_element(node, name);
          if (!child ||
              !data_bind_value_to_xml(child_value->data.array_val.items[j], child, depth + 1))
            return 0;
        }
      } else {
        turbo_xml_node_t *child = turbo_xml_add_element(node, name);
        if (!child || !data_bind_value_to_xml(child_value, child, depth + 1)) return 0;
      }
    }
    return 1;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET:
    for (i = 0; i < value->data.array_val.count; ++i) {
      turbo_xml_node_t *child = turbo_xml_add_element(node, "item");
      if (!child || !data_bind_value_to_xml(value->data.array_val.items[i], child, depth + 1))
        return 0;
    }
    return 1;
  case DATA_BIND_VALUE_MAP:
    for (i = 0; i < value->data.map_val.count; ++i) {
      const char *key = value->data.map_val.items[i].key;
      turbo_xml_node_t *child;
      if (!data_bind_xml_name_valid(key)) return 0;
      child = turbo_xml_add_element(node, key);
      if (!child || !data_bind_value_to_xml(value->data.map_val.items[i].value, child, depth + 1))
        return 0;
    }
    return 1;
  case DATA_BIND_VALUE_NULL:
    return 0;
  default:
    return data_bind_standard_scalar_text(value, text, sizeof(text)) &&
           turbo_xml_set_text(node, text) == 0;
  }
}

DataBindStatus data_bind_object_serialize_xml(const DataBindObject *object, char **out_xml,
                                              size_t *out_len, DataBindError *error) {
  turbo_xml_doc_t *xml;
  turbo_xml_node_t *root;
  if (out_xml != NULL) *out_xml = NULL;
  if (out_len != NULL) *out_len = 0;
  if (!object || !object->value || !object->type_name || !out_xml ||
      !data_bind_xml_name_valid(object->type_name))
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid XML serialize arguments");
  xml = turbo_xml_create_document(object->type_name);
  root = turbo_xml_root_element(xml);
  if (!xml || !root || !data_bind_value_to_xml(object->value, root, 0)) {
    turbo_free_xml(&xml);
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, "xml", -1, -1,
                        "DataBind value cannot be represented as XML");
  }
  *out_xml = turbo_xml_serialize(xml, out_len);
  turbo_free_xml(&xml);
  if (!*out_xml)
    return db_error_set(error, DATA_BIND_ERR_OOM, "xml", -1, -1,
                        "Failed to serialize XML document");
  db_error_clear(error);
  return DATA_BIND_OK;
}

#define DATA_BIND_CSV_MAX_PATH_LENGTH 255u

typedef struct data_bind_csv_cell {
  tstr_t path;
  tstr_t text;
} data_bind_csv_cell_t;

TURBO_VEC_DEFINE(data_bind_csv_cell_vec_t, data_bind_csv_cell_t)

static void data_bind_csv_cells_destroy(data_bind_csv_cell_vec_t *cells) {
  size_t i;
  if (cells == NULL) return;
  for (i = 0; i < data_bind_csv_cell_vec_t_size(cells); ++i) {
    data_bind_csv_cell_t *cell = data_bind_csv_cell_vec_t_at(cells, i);
    if (cell != NULL) {
      tstr_free(cell->path);
      tstr_free(cell->text);
    }
  }
  data_bind_csv_cell_vec_t_destroy(cells);
}

static int data_bind_csv_tstr_append(tstr_t *out, const char *data, size_t len) {
  tstr_t next;
  if (out == NULL || *out == NULL || (data == NULL && len != 0)) return 0;
  next = tstr_cat_len(*out, data, len);
  if (next == NULL) return 0;
  *out = next;
  return 1;
}

static int data_bind_csv_path_component_valid(const char *component) {
  if (component == NULL || component[0] == '\0' || strchr(component, '.') != NULL ||
      strchr(component, '[') != NULL)
    return 0;
  return tstr_v_utf8_valid(tstr_v_from_cstr(component));
}

static tstr_t data_bind_csv_child_path(const tstr_t prefix, const char *name,
                                       DataBindStatus *status) {
  size_t prefix_len = prefix != NULL ? tstr_len(prefix) : 0;
  size_t name_len;
  size_t separator_len = prefix_len != 0 ? 1u : 0u;
  tstr_t path;
  if (status == NULL) return NULL;
  *status = DATA_BIND_ERR_TYPE_MISMATCH;
  if (!data_bind_csv_path_component_valid(name)) return NULL;
  name_len = strlen(name);
  if (prefix_len > DATA_BIND_CSV_MAX_PATH_LENGTH - separator_len ||
      name_len > DATA_BIND_CSV_MAX_PATH_LENGTH - prefix_len - separator_len)
    return NULL;
  path = prefix != NULL ? tstr_clone(prefix) : tstr_new();
  if (path == NULL) {
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  if ((separator_len != 0 && !data_bind_csv_tstr_append(&path, ".", 1u)) ||
      !data_bind_csv_tstr_append(&path, name, name_len)) {
    tstr_free(path);
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  *status = DATA_BIND_OK;
  return path;
}

static tstr_t data_bind_csv_index_path(const tstr_t prefix, size_t index,
                                       DataBindStatus *status) {
  char suffix[32];
  int suffix_len;
  size_t prefix_len;
  tstr_t path;
  if (status == NULL) return NULL;
  *status = DATA_BIND_ERR_TYPE_MISMATCH;
  if (prefix == NULL || tstr_empty(prefix)) return NULL;
  suffix_len = fmt(suffix, sizeof(suffix), "[{}]", index);
  if (suffix_len <= 0 || (size_t)suffix_len >= sizeof(suffix)) return NULL;
  prefix_len = tstr_len(prefix);
  if (prefix_len > DATA_BIND_CSV_MAX_PATH_LENGTH ||
      (size_t)suffix_len > DATA_BIND_CSV_MAX_PATH_LENGTH - prefix_len)
    return NULL;
  path = tstr_clone(prefix);
  if (path == NULL) {
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  if (!data_bind_csv_tstr_append(&path, suffix, (size_t)suffix_len)) {
    tstr_free(path);
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  *status = DATA_BIND_OK;
  return path;
}

static tstr_t data_bind_csv_scalar_text(const DataBindValue *value, DataBindStatus *status) {
  char text[128];
  tstr_t result = NULL;
  if (status == NULL) return NULL;
  *status = DATA_BIND_ERR_TYPE_MISMATCH;
  if (value == NULL) return NULL;
  if (value->kind == DATA_BIND_VALUE_STRING) {
    const char *string = value->data.string_val.ptr;
    if (string == NULL || !tstr_v_utf8_valid(tstr_v_from_cstr(string))) return NULL;
    result = tstr_dup(string);
  } else if (value->kind == DATA_BIND_VALUE_BYTES) {
    const char *bytes = (const char *)value->data.bytes_val.ptr;
    size_t len = value->data.bytes_val.len;
    if (len != 0 &&
        (bytes == NULL || memchr(bytes, '\0', len) != NULL ||
         !tstr_v_utf8_valid(tstr_v_from_buf(bytes, len))))
      return NULL;
    result = len != 0 ? tstr_dup_len(bytes, len) : tstr_new();
  } else if (value->kind == DATA_BIND_VALUE_BIGINT) {
    if (value->data.bigint_val.ptr == NULL) return NULL;
    result = tstr_dup(value->data.bigint_val.ptr);
  } else {
    if (!data_bind_standard_scalar_text(value, text, sizeof(text))) return NULL;
    result = tstr_dup(text);
  }
  if (result == NULL) {
    *status = DATA_BIND_ERR_OOM;
    return NULL;
  }
  *status = DATA_BIND_OK;
  return result;
}

static DataBindStatus data_bind_csv_add_scalar(data_bind_csv_cell_vec_t *cells,
                                               const tstr_t path,
                                               const DataBindValue *value) {
  data_bind_csv_cell_t cell = {0};
  DataBindStatus status;
  cell.path = path != NULL && !tstr_empty(path) ? tstr_clone(path) : tstr_dup("value");
  if (cell.path == NULL) return DATA_BIND_ERR_OOM;
  cell.text = data_bind_csv_scalar_text(value, &status);
  if (cell.text == NULL) {
    tstr_free(cell.path);
    return status;
  }
  if (data_bind_csv_cell_vec_t_push(cells, cell) != TURBO_OK) {
    tstr_free(cell.path);
    tstr_free(cell.text);
    return DATA_BIND_ERR_OOM;
  }
  return DATA_BIND_OK;
}

static DataBindStatus data_bind_csv_flatten_value(data_bind_csv_cell_vec_t *cells,
                                                  const DataBindValue *value,
                                                  const tstr_t path, unsigned depth) {
  size_t i;
  if (cells == NULL || value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (depth > DATA_BIND_JSON_MAX_DEPTH) return DATA_BIND_ERR_RUNTIME;
  switch (value->kind) {
  case DATA_BIND_VALUE_OBJECT:
    if (value->data.object_val.count == 0) return DATA_BIND_ERR_TYPE_MISMATCH;
    for (i = 0; i < value->data.object_val.count; ++i) {
      DataBindStatus status;
      tstr_t child_path =
          data_bind_csv_child_path(path, value->data.object_val.items[i].name, &status);
      if (child_path == NULL) return status;
      status = data_bind_csv_flatten_value(cells, value->data.object_val.items[i].value,
                                           child_path, depth + 1);
      tstr_free(child_path);
      if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
  case DATA_BIND_VALUE_LIST:
  case DATA_BIND_VALUE_SET:
    if (path == NULL || tstr_empty(path) || value->data.array_val.count == 0)
      return DATA_BIND_ERR_TYPE_MISMATCH;
    for (i = 0; i < value->data.array_val.count; ++i) {
      DataBindStatus status;
      tstr_t child_path = data_bind_csv_index_path(path, i, &status);
      if (child_path == NULL) return status;
      status = data_bind_csv_flatten_value(cells, value->data.array_val.items[i], child_path,
                                           depth + 1);
      tstr_free(child_path);
      if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
  case DATA_BIND_VALUE_MAP:
    if (path == NULL || tstr_empty(path) || value->data.map_val.count == 0)
      return DATA_BIND_ERR_TYPE_MISMATCH;
    for (i = 0; i < value->data.map_val.count; ++i) {
      DataBindStatus status;
      tstr_t child_path =
          data_bind_csv_child_path(path, value->data.map_val.items[i].key, &status);
      if (child_path == NULL) return status;
      status = data_bind_csv_flatten_value(cells, value->data.map_val.items[i].value,
                                           child_path, depth + 1);
      tstr_free(child_path);
      if (status != DATA_BIND_OK) return status;
    }
    return DATA_BIND_OK;
  case DATA_BIND_VALUE_NULL:
    return DATA_BIND_ERR_TYPE_MISMATCH;
  default:
    return data_bind_csv_add_scalar(cells, path, value);
  }
}

static int data_bind_csv_append_field(tstr_t *csv, const tstr_t field) {
  size_t i;
  size_t start = 0;
  size_t len;
  int quoted = 0;
  if (csv == NULL || *csv == NULL || field == NULL) return 0;
  len = tstr_len(field);
  if ((len != 0 && (field[0] == ' ' || field[0] == '\t' || field[len - 1] == ' ' ||
                    field[len - 1] == '\t')) ||
      memchr(field, ',', len) != NULL || memchr(field, '"', len) != NULL ||
      memchr(field, '\r', len) != NULL || memchr(field, '\n', len) != NULL)
    quoted = 1;
  if (!quoted) return data_bind_csv_tstr_append(csv, field, len);
  if (!data_bind_csv_tstr_append(csv, "\"", 1u)) return 0;
  for (i = 0; i < len; ++i) {
    if (field[i] != '"') continue;
    if (!data_bind_csv_tstr_append(csv, field + start, i - start) ||
        !data_bind_csv_tstr_append(csv, "\"\"", 2u))
      return 0;
    start = i + 1;
  }
  return data_bind_csv_tstr_append(csv, field + start, len - start) &&
         data_bind_csv_tstr_append(csv, "\"", 1u);
}

DataBindStatus data_bind_object_serialize_csv(const DataBindObject *object, char **out_csv,
                                              size_t *out_len, DataBindError *error) {
  data_bind_csv_cell_vec_t cells;
  DataBindStatus status;
  tstr_t csv = NULL;
  size_t i;
  if (out_csv != NULL) *out_csv = NULL;
  if (out_len != NULL) *out_len = 0;
  if (object == NULL || object->value == NULL || out_csv == NULL)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1,
                        "Invalid CSV serialize arguments");
  if (object->value->kind == DATA_BIND_VALUE_LIST ||
      object->value->kind == DATA_BIND_VALUE_SET || object->value->kind == DATA_BIND_VALUE_MAP)
    return db_error_set(error, DATA_BIND_ERR_TYPE_MISMATCH, "csv", -1, -1,
                        "A CSV object must contain one record or scalar value");
  if (data_bind_csv_cell_vec_t_init(&cells) != TURBO_OK)
    return db_error_set(error, DATA_BIND_ERR_OOM, "csv", -1, -1,
                        "Out of memory creating CSV columns");
  status = data_bind_csv_flatten_value(&cells, object->value, NULL, 0);
  if (status != DATA_BIND_OK || data_bind_csv_cell_vec_t_empty(&cells)) {
    data_bind_csv_cells_destroy(&cells);
    return db_error_set(error, status != DATA_BIND_OK ? status : DATA_BIND_ERR_TYPE_MISMATCH,
                        "csv", -1, -1,
                        status == DATA_BIND_ERR_OOM
                            ? "Out of memory flattening CSV columns"
                            : "DataBind value cannot be represented losslessly as CSV");
  }
  csv = tstr_new();
  if (csv == NULL) status = DATA_BIND_ERR_OOM;
  for (i = 0; status == DATA_BIND_OK && i < data_bind_csv_cell_vec_t_size(&cells); ++i) {
    const data_bind_csv_cell_t *cell = data_bind_csv_cell_vec_t_at_const(&cells, i);
    if ((i != 0 && !data_bind_csv_tstr_append(&csv, ",", 1u)) ||
        !data_bind_csv_append_field(&csv, cell->path))
      status = DATA_BIND_ERR_OOM;
  }
  if (status == DATA_BIND_OK && !data_bind_csv_tstr_append(&csv, "\r\n", 2u))
    status = DATA_BIND_ERR_OOM;
  for (i = 0; status == DATA_BIND_OK && i < data_bind_csv_cell_vec_t_size(&cells); ++i) {
    const data_bind_csv_cell_t *cell = data_bind_csv_cell_vec_t_at_const(&cells, i);
    if ((i != 0 && !data_bind_csv_tstr_append(&csv, ",", 1u)) ||
        !data_bind_csv_append_field(&csv, cell->text))
      status = DATA_BIND_ERR_OOM;
  }
  if (status == DATA_BIND_OK && !data_bind_csv_tstr_append(&csv, "\r\n", 2u))
    status = DATA_BIND_ERR_OOM;
  if (status == DATA_BIND_OK) {
    *out_csv = tstr_to_cstr(csv);
    if (*out_csv == NULL) status = DATA_BIND_ERR_OOM;
    else if (out_len != NULL) *out_len = tstr_len(csv);
  }
  tstr_free(csv);
  data_bind_csv_cells_destroy(&cells);
  if (status != DATA_BIND_OK)
    return db_error_set(error, status, "csv", -1, -1, "Out of memory serializing CSV");
  db_error_clear(error);
  return DATA_BIND_OK;
}

typedef DataBindStatus (*data_bind_object_serialize_fn)(const DataBindObject *, char **, size_t *,
                                                        DataBindError *);

static DataBindStatus data_bind_object_write(const DataBindObject *object, DataBindWriteFn write,
                                             void *user, DataBindError *error,
                                             data_bind_object_serialize_fn serialize) {
  char *text = NULL;
  size_t len = 0;
  DataBindStatus status;
  if (!write)
    return db_error_set(error, DATA_BIND_ERR_INVALID_ARG, NULL, -1, -1, "Invalid serialize writer");
  status = serialize(object, &text, &len, error);
  if (status != DATA_BIND_OK) return status;
  if (write(text, len, user) != 0) {
    data_bind_serialized_free(text);
    return db_error_set(error, DATA_BIND_ERR_IO, NULL, -1, -1, "Serialize writer failed");
  }
  data_bind_serialized_free(text);
  db_error_clear(error);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_object_write_json(const DataBindObject *object, DataBindWriteFn write,
                                           void *user, DataBindError *error) {
  return data_bind_object_write(object, write, user, error, data_bind_object_serialize_json);
}

DataBindStatus data_bind_object_write_yaml(const DataBindObject *object, DataBindWriteFn write,
                                           void *user, DataBindError *error) {
  return data_bind_object_write(object, write, user, error, data_bind_object_serialize_yaml);
}

DataBindStatus data_bind_object_write_xml(const DataBindObject *object, DataBindWriteFn write,
                                          void *user, DataBindError *error) {
  return data_bind_object_write(object, write, user, error, data_bind_object_serialize_xml);
}

DataBindStatus data_bind_object_write_csv(const DataBindObject *object, DataBindWriteFn write,
                                          void *user, DataBindError *error) {
  return data_bind_object_write(object, write, user, error, data_bind_object_serialize_csv);
}

void data_bind_serialized_free(char *data) { turbo_json_serialize_free(data); }

void data_bind_binary_free(void *data) { free(data); }

void data_bind_object_free(DataBindObject *object) {
  if (object == NULL) return;
  free(object->type_name);
  data_bind_value_free(object->value);
  free(object);
}

DataBindValueKind data_bind_value_kind(const DataBindValue *value) {
  return value != NULL ? value->kind : DATA_BIND_VALUE_NULL;
}

size_t data_bind_value_field_count(const DataBindValue *value) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_OBJECT) return 0;
  return value->data.object_val.count;
}

const char *data_bind_value_field_name(const DataBindValue *value, size_t index) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_OBJECT ||
      index >= value->data.object_val.count)
    return NULL;
  return value->data.object_val.items[index].name;
}

const DataBindValue *data_bind_value_field_at(const DataBindValue *value, size_t index) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_OBJECT ||
      index >= value->data.object_val.count)
    return NULL;
  return value->data.object_val.items[index].value;
}

const DataBindValue *data_bind_value_get(const DataBindValue *value, const char *name) {
  size_t i;
  if (value == NULL || value->kind != DATA_BIND_VALUE_OBJECT || name == NULL) return NULL;
  for (i = 0; i < value->data.object_val.count; i++)
    if (strcmp(value->data.object_val.items[i].name, name) == 0)
      return value->data.object_val.items[i].value;
  return NULL;
}

size_t data_bind_value_count(const DataBindValue *value) {
  if (value == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_LIST || value->kind == DATA_BIND_VALUE_SET)
    return value->data.array_val.count;
  if (value->kind == DATA_BIND_VALUE_MAP) return value->data.map_val.count;
  return 0;
}

const DataBindValue *data_bind_value_at(const DataBindValue *value, size_t index) {
  if (value == NULL ||
      (value->kind != DATA_BIND_VALUE_LIST && value->kind != DATA_BIND_VALUE_SET) ||
      index >= value->data.array_val.count)
    return NULL;
  return value->data.array_val.items[index];
}

DataBindMapEntry data_bind_value_map_entry_at(const DataBindValue *value, size_t index) {
  DataBindMapEntry entry;
  entry.key = NULL;
  entry.value = NULL;
  if (value == NULL || value->kind != DATA_BIND_VALUE_MAP || index >= value->data.map_val.count)
    return entry;
  entry.key = value->data.map_val.items[index].key;
  entry.value = value->data.map_val.items[index].value;
  return entry;
}

int32_t data_bind_value_as_int(const DataBindValue *value) {
  if (value == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_INT) return value->data.int_val;
  if (value->kind == DATA_BIND_VALUE_INT64 && value->data.int64_val >= INT32_MIN &&
      value->data.int64_val <= INT32_MAX)
    return (int32_t)value->data.int64_val;
  if (value->kind == DATA_BIND_VALUE_UINT64 && value->data.uint64_val <= INT32_MAX)
    return (int32_t)value->data.uint64_val;
  if (value->kind == DATA_BIND_VALUE_DOUBLE) return (int32_t)value->data.double_val;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val ? 1 : 0;
  return 0;
}

int64_t data_bind_value_as_int64(const DataBindValue *value) {
  if (value == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_INT64) return value->data.int64_val;
  if (value->kind == DATA_BIND_VALUE_INT) return value->data.int_val;
  if (value->kind == DATA_BIND_VALUE_UINT64 && value->data.uint64_val <= INT64_MAX)
    return (int64_t)value->data.uint64_val;
  if (value->kind == DATA_BIND_VALUE_DOUBLE) return (int64_t)value->data.double_val;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val ? 1 : 0;
  return 0;
}

uint64_t data_bind_value_as_uint64(const DataBindValue *value) {
  if (value == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_UINT64) return value->data.uint64_val;
  if (value->kind == DATA_BIND_VALUE_INT64 && value->data.int64_val >= 0)
    return (uint64_t)value->data.int64_val;
  if (value->kind == DATA_BIND_VALUE_INT && value->data.int_val >= 0)
    return (uint64_t)value->data.int_val;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val ? 1u : 0u;
  return 0;
}

double data_bind_value_as_double(const DataBindValue *value) {
  if (value == NULL) return 0.0;
  if (value->kind == DATA_BIND_VALUE_DOUBLE) return value->data.double_val;
  if (value->kind == DATA_BIND_VALUE_INT64) return (double)value->data.int64_val;
  if (value->kind == DATA_BIND_VALUE_UINT64) return (double)value->data.uint64_val;
  if (value->kind == DATA_BIND_VALUE_INT) return (double)value->data.int_val;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val ? 1.0 : 0.0;
  return 0.0;
}

int data_bind_value_as_bool(const DataBindValue *value) {
  if (value == NULL) return 0;
  if (value->kind == DATA_BIND_VALUE_BOOL) return value->data.bool_val != 0;
  if (value->kind == DATA_BIND_VALUE_INT) return value->data.int_val != 0;
  if (value->kind == DATA_BIND_VALUE_INT64) return value->data.int64_val != 0;
  if (value->kind == DATA_BIND_VALUE_UINT64) return value->data.uint64_val != 0;
  if (value->kind == DATA_BIND_VALUE_DOUBLE) return value->data.double_val != 0.0;
  return 0;
}

const char *data_bind_value_as_string(const DataBindValue *value) {
  return value != NULL && value->kind == DATA_BIND_VALUE_STRING ? value->data.string_val.ptr : NULL;
}

const uint8_t *data_bind_value_as_bytes(const DataBindValue *value, size_t *len) {
  if (len != NULL) *len = 0;
  if (value == NULL || value->kind != DATA_BIND_VALUE_BYTES) return NULL;
  if (len != NULL) *len = value->data.bytes_val.len;
  return value->data.bytes_val.ptr;
}

int data_bind_value_as_uuid(const DataBindValue *value, uint8_t out[DATA_BIND_UUID_SIZE]) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_UUID || out == NULL) return 0;
  memcpy(out, value->data.uuid_val.bytes, DATA_BIND_UUID_SIZE);
  return 1;
}

const char *data_bind_value_as_uuid_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_UUID || out == NULL ||
      len < TURBO_UUID_STRING_SIZE)
    return NULL;
  return turbo_uuid_format(&value->data.uuid_val, out, len) == TURBO_OK ? out : NULL;
}

int data_bind_value_as_datetime(const DataBindValue *value, turbo_datetime_t *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATETIME || out == NULL) return 0;
  *out = value->data.datetime_val;
  return 1;
}

double data_bind_value_as_datetime_timestamp(const DataBindValue *value) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATETIME) return -1.0;
  return (double)turbo_datetime_to_time(&value->data.datetime_val);
}

const char *data_bind_value_as_datetime_string(const DataBindValue *value, char *out, size_t len) {
  time_t ts;
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATETIME || out == NULL || len < 32)
    return NULL;
  ts = turbo_datetime_to_time(&value->data.datetime_val);
  if (ts == (time_t)-1 || turbo_datetime_format_rfc822(ts, out, len) < 0) return NULL;
  return out;
}

int data_bind_value_as_date(const DataBindValue *value, DataBindDate *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATE || out == NULL) return 0;
  *out = value->data.date_val;
  return 1;
}

const char *data_bind_value_as_date_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DATE) return NULL;
  return db_date_to_text(value->data.date_val, out, len) ? out : NULL;
}

int data_bind_value_as_time(const DataBindValue *value, DataBindTime *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_TIME || out == NULL) return 0;
  *out = value->data.time_val;
  return 1;
}

const char *data_bind_value_as_time_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_TIME) return NULL;
  return db_time_to_text(value->data.time_val, out, len) ? out : NULL;
}

int64_t data_bind_value_as_duration_milliseconds(const DataBindValue *value) {
  return value != NULL && value->kind == DATA_BIND_VALUE_DURATION ? value->data.duration_ms : 0;
}

const char *data_bind_value_as_duration_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DURATION) return NULL;
  return db_duration_to_text(value->data.duration_ms, out, len) ? out : NULL;
}

int data_bind_value_as_decimal(const DataBindValue *value, DataBindDecimal *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DECIMAL || out == NULL) return 0;
  *out = value->data.decimal_val;
  return 1;
}

const char *data_bind_value_as_decimal_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_DECIMAL) return NULL;
  return db_decimal_to_text(value->data.decimal_val, out, len) ? out : NULL;
}

const char *data_bind_value_as_bigint_string(const DataBindValue *value) {
  return value != NULL && value->kind == DATA_BIND_VALUE_BIGINT ? value->data.bigint_val.ptr : NULL;
}

int data_bind_value_as_money(const DataBindValue *value, DataBindMoney *out) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_MONEY || out == NULL) return 0;
  *out = value->data.money_val;
  return 1;
}

const char *data_bind_value_as_money_string(const DataBindValue *value, char *out, size_t len) {
  if (value == NULL || value->kind != DATA_BIND_VALUE_MONEY) return NULL;
  return db_money_to_text(value->data.money_val, out, len) ? out : NULL;
}

DataBindStatus data_bind_value_get_int32(const DataBindValue *value, int32_t *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind == DATA_BIND_VALUE_INT) {
    *out = value->data.int_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_BOOL) {
    *out = value->data.bool_val ? 1 : 0;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT64 && value->data.int64_val >= INT32_MIN &&
      value->data.int64_val <= INT32_MAX) {
    *out = (int32_t)value->data.int64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_UINT64 && value->data.uint64_val <= INT32_MAX) {
    *out = (int32_t)value->data.uint64_val;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

DataBindStatus data_bind_value_get_int64(const DataBindValue *value, int64_t *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind == DATA_BIND_VALUE_INT64) {
    *out = value->data.int64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT) {
    *out = value->data.int_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_BOOL) {
    *out = value->data.bool_val ? 1 : 0;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_UINT64 && value->data.uint64_val <= INT64_MAX) {
    *out = (int64_t)value->data.uint64_val;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

DataBindStatus data_bind_value_get_uint64(const DataBindValue *value, uint64_t *out) {
  if (out == NULL || value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind == DATA_BIND_VALUE_UINT64) {
    *out = value->data.uint64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT64 && value->data.int64_val >= 0) {
    *out = (uint64_t)value->data.int64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT && value->data.int_val >= 0) {
    *out = (uint64_t)value->data.int_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_BOOL) {
    *out = value->data.bool_val ? 1u : 0u;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

DataBindStatus data_bind_value_get_double(const DataBindValue *value, double *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind == DATA_BIND_VALUE_DOUBLE) {
    *out = value->data.double_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT64) {
    *out = (double)value->data.int64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_UINT64) {
    *out = (double)value->data.uint64_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_INT) {
    *out = (double)value->data.int_val;
    return DATA_BIND_OK;
  }
  if (value->kind == DATA_BIND_VALUE_BOOL) {
    *out = value->data.bool_val ? 1.0 : 0.0;
    return DATA_BIND_OK;
  }
  return DATA_BIND_ERR_TYPE_MISMATCH;
}

DataBindStatus data_bind_value_get_bool(const DataBindValue *value, int *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_BOOL) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.bool_val != 0;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_string(const DataBindValue *value, const char **data,
                                          size_t *len) {
  if (data == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *data = NULL;
  if (len != NULL) *len = 0;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_STRING) return DATA_BIND_ERR_TYPE_MISMATCH;
  *data = value->data.string_val.ptr;
  if (len != NULL) *len = value->data.string_val.ptr ? strlen(value->data.string_val.ptr) : 0;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_bytes(const DataBindValue *value, const uint8_t **data,
                                         size_t *len) {
  if (data == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *data = NULL;
  if (len != NULL) *len = 0;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_BYTES) return DATA_BIND_ERR_TYPE_MISMATCH;
  *data = value->data.bytes_val.ptr;
  if (len != NULL) *len = value->data.bytes_val.len;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_uuid(const DataBindValue *value,
                                        uint8_t out[DATA_BIND_UUID_SIZE]) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_UUID) return DATA_BIND_ERR_TYPE_MISMATCH;
  memcpy(out, value->data.uuid_val.bytes, DATA_BIND_UUID_SIZE);
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_datetime(const DataBindValue *value, turbo_datetime_t *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_DATETIME) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.datetime_val;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_date(const DataBindValue *value, DataBindDate *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_DATE) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.date_val;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_time(const DataBindValue *value, DataBindTime *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_TIME) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.time_val;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_duration_milliseconds(const DataBindValue *value, int64_t *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_DURATION) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.duration_ms;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_decimal(const DataBindValue *value, DataBindDecimal *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_DECIMAL) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.decimal_val;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_bigint(const DataBindValue *value, const char **text,
                                          size_t *len) {
  if (text == NULL) return DATA_BIND_ERR_INVALID_ARG;
  *text = NULL;
  if (len != NULL) *len = 0;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_BIGINT) return DATA_BIND_ERR_TYPE_MISMATCH;
  *text = value->data.bigint_val.ptr;
  if (len != NULL)
    *len = value->data.bigint_val.ptr != NULL ? strlen(value->data.bigint_val.ptr) : 0;
  return DATA_BIND_OK;
}

DataBindStatus data_bind_value_get_money(const DataBindValue *value, DataBindMoney *out) {
  if (out == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value == NULL) return DATA_BIND_ERR_INVALID_ARG;
  if (value->kind != DATA_BIND_VALUE_MONEY) return DATA_BIND_ERR_TYPE_MISMATCH;
  *out = value->data.money_val;
  return DATA_BIND_OK;
}

const char *data_bind_schema_kind_name(DataBindSchemaKind kind) {
  switch (kind) {
  case DATA_BIND_SCHEMA_MESSAGE:
    return "message";
  case DATA_BIND_SCHEMA_COMPOSITE:
    return "composite";
  case DATA_BIND_SCHEMA_GROUP:
    return "group";
  case DATA_BIND_SCHEMA_ENUM:
    return "enum";
  case DATA_BIND_SCHEMA_FLAGS:
    return "flags";
  case DATA_BIND_SCHEMA_UNION:
    return "union";
  case DATA_BIND_SCHEMA_SCALAR:
    return "scalar";
  case DATA_BIND_SCHEMA_UNKNOWN:
  default:
    return "unknown";
  }
}

size_t data_bind_schema_type_count(DataBind *codec) {
  size_t count = 0;
  static const char *const lists[] = {"messages", "composites", "groups", "unions", "enums"};
  size_t i;
  if (codec == NULL || codec->schema_root == NULL) return 0;
  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
    Node *list = find_child(codec->schema_root, lists[i]);
    if (list != NULL && list->type == NODE_LIST) count += list->data.list.count;
  }
  return count;
}

int data_bind_schema_type_at(DataBind *codec, size_t index, DataBindSchemaType *out) {
  static const char *const lists[] = {"messages", "composites", "groups", "unions", "enums"};
  size_t i;
  if (codec == NULL || codec->schema_root == NULL || out == NULL) return 0;
  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
    Node *list = find_child(codec->schema_root, lists[i]);
    if (list == NULL || list->type != NODE_LIST) continue;
    if (index < list->data.list.count)
      return fill_schema_type(list->data.list.items[index], lists[i], out);
    index -= list->data.list.count;
  }
  db_reflect_clear(out, out->size, sizeof(*out));
  return 0;
}

int data_bind_schema_find_type(DataBind *codec, const char *name, DataBindSchemaType *out) {
  static const char *const lists[] = {"messages", "composites", "groups", "unions", "enums"};
  size_t i;
  if (codec == NULL || codec->schema_root == NULL || name == NULL || out == NULL) return 0;
  for (i = 0; i < sizeof(lists) / sizeof(lists[0]); i++) {
    Node *record = find_named_record(codec->schema_root, lists[i], name);
    if (record != NULL) return fill_schema_type(record, lists[i], out);
  }
  db_reflect_clear(out, out->size, sizeof(*out));
  return 0;
}

size_t data_bind_schema_field_count(DataBind *codec, const char *type_name) {
  Node *record;
  Node *fields;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL) return 0;
  record = find_schema_record(codec->schema_root, type_name);
  fields = fields_node_for_record(record);
  return fields != NULL ? fields->data.list.count : 0;
}

int data_bind_schema_field_at(DataBind *codec, const char *type_name, size_t index,
                              DataBindSchemaField *out) {
  Node *record;
  Node *fields;
  if (codec == NULL || codec->schema_root == NULL || type_name == NULL || out == NULL) return 0;
  record = find_schema_record(codec->schema_root, type_name);
  fields = fields_node_for_record(record);
  if (fields == NULL || index >= fields->data.list.count) {
    db_reflect_clear(out, out->size, sizeof(*out));
    return 0;
  }
  return fill_schema_field(codec->schema_root, fields->data.list.items[index], out);
}

size_t data_bind_schema_enum_count(DataBind *codec) {
  Node *enums;
  if (codec == NULL || codec->schema_root == NULL) return 0;
  enums = find_child(codec->schema_root, "enums");
  return enums != NULL && enums->type == NODE_LIST ? enums->data.list.count : 0;
}

int data_bind_schema_enum_at(DataBind *codec, size_t index, DataBindSchemaType *out) {
  Node *enums;
  if (codec == NULL || codec->schema_root == NULL || out == NULL) return 0;
  enums = find_child(codec->schema_root, "enums");
  if (enums == NULL || enums->type != NODE_LIST || index >= enums->data.list.count) {
    db_reflect_clear(out, out->size, sizeof(*out));
    return 0;
  }
  return fill_schema_type(enums->data.list.items[index], "enums", out);
}

size_t data_bind_schema_enum_item_count(DataBind *codec, const char *enum_name) {
  Node *record;
  Node *items;
  if (codec == NULL || codec->schema_root == NULL || enum_name == NULL) return 0;
  record = find_named_record(codec->schema_root, "enums", enum_name);
  items = items_node_for_enum(record);
  return items != NULL ? items->data.list.count : 0;
}

int data_bind_schema_enum_item_at(DataBind *codec, const char *enum_name, size_t index,
                                  DataBindSchemaEnumItem *out) {
  Node *record;
  Node *items;
  Node *item;
  size_t out_size;
  if (codec == NULL || codec->schema_root == NULL || enum_name == NULL || out == NULL) return 0;
  record = find_named_record(codec->schema_root, "enums", enum_name);
  items = items_node_for_enum(record);
  if (items == NULL || index >= items->data.list.count) {
    db_reflect_clear(out, out->size, sizeof(*out));
    return 0;
  }
  item = items->data.list.items[index];
  out_size = db_reflect_out_size(out->size, sizeof(*out));
  memset(out, 0, out_size);
  DB_REFLECT_SET(DataBindSchemaEnumItem, out, out_size, size, out_size);
  DB_REFLECT_SET(DataBindSchemaEnumItem, out, out_size, name,
                 get_string_val(find_child(item, "name")));
  DB_REFLECT_SET(DataBindSchemaEnumItem, out, out_size, value,
                 get_string_val(find_child(item, "value")));
  return get_string_val(find_child(item, "name")) != NULL;
}

const char *data_bind_schema_name(DataBind *codec) {
  Node *schema;
  if (codec == NULL || codec->schema_root == NULL) return NULL;
  schema = find_child(codec->schema_root, "schema");
  return get_string_val(find_child(schema, "name"));
}

size_t data_bind_schema_attribute_count(DataBind *codec) {
  Node *schema;
  Node *attrs;
  if (codec == NULL || codec->schema_root == NULL) return 0;
  schema = find_child(codec->schema_root, "schema");
  attrs = find_child(schema, "attributes");
  return attrs != NULL && attrs->type == NODE_LIST ? attrs->data.list.count : 0;
}

int data_bind_schema_attribute_at(DataBind *codec, size_t index, DataBindSchemaAttribute *out) {
  Node *schema;
  Node *attrs;
  Node *attr;
  size_t out_size;
  if (codec == NULL || codec->schema_root == NULL || out == NULL) return 0;
  schema = find_child(codec->schema_root, "schema");
  attrs = find_child(schema, "attributes");
  if (attrs == NULL || attrs->type != NODE_LIST || index >= attrs->data.list.count) {
    db_reflect_clear(out, out->size, sizeof(*out));
    return 0;
  }
  attr = attrs->data.list.items[index];
  out_size = db_reflect_out_size(out->size, sizeof(*out));
  memset(out, 0, out_size);
  DB_REFLECT_SET(DataBindSchemaAttribute, out, out_size, size, out_size);
  DB_REFLECT_SET(DataBindSchemaAttribute, out, out_size, name,
                 get_string_val(find_child(attr, "name")));
  DB_REFLECT_SET(DataBindSchemaAttribute, out, out_size, value,
                 get_string_val(find_child(attr, "value")));
  return get_string_val(find_child(attr, "name")) != NULL;
}

const char *data_bind_schema_attribute_get(DataBind *codec, const char *name) {
  size_t i;
  size_t count;
  if (codec == NULL || name == NULL) return NULL;
  count = data_bind_schema_attribute_count(codec);
  for (i = 0; i < count; i++) {
    DataBindSchemaAttribute attr = DATA_BIND_SCHEMA_ATTRIBUTE_INIT;
    if (data_bind_schema_attribute_at(codec, i, &attr) && attr.name != NULL &&
        strcmp(attr.name, name) == 0)
      return attr.value;
  }
  return NULL;
}

const char *data_bind_status_name(DataBindStatus status) {
  switch (status) {
  case DATA_BIND_OK:
    return "ok";
  case DATA_BIND_ERR_INVALID_ARG:
    return "invalid_arg";
  case DATA_BIND_ERR_IO:
    return "io";
  case DATA_BIND_ERR_PARSE:
    return "parse";
  case DATA_BIND_ERR_SCHEMA:
    return "schema";
  case DATA_BIND_ERR_TYPE_NOT_FOUND:
    return "type_not_found";
  case DATA_BIND_ERR_TYPE_MISMATCH:
    return "type_mismatch";
  case DATA_BIND_ERR_OOM:
    return "oom";
  case DATA_BIND_ERR_RUNTIME:
    return "runtime";
  default:
    return "unknown";
  }
}

int data_bind_library_version(void) { return DATA_BIND_VERSION; }

int data_bind_abi_version(void) { return DATA_BIND_ABI_VERSION; }

const char *data_bind_version_string(void) { return "1.12.0"; }
