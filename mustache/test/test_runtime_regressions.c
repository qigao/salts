#include "tinytest.h"

#include "json_parser.h"
#include "mustache.h"
#include "mustache_json.h"
#include "turbo_buffer.h"

#include <stdlib.h>
#include <string.h>

typedef struct RUNTIME_PROVIDER_DATA {
  int root;
  int lambda;
  int fail_lambda;
  MUSTACHE_TEMPLATE *partial;
} RUNTIME_PROVIDER_DATA;

static void *runtime_get_root(void *provider_data) {
  return &((RUNTIME_PROVIDER_DATA *)provider_data)->root;
}

static void *runtime_get_child_by_name(void *node, const char *name, size_t size,
                                       void *provider_data) {
  RUNTIME_PROVIDER_DATA *data = (RUNTIME_PROVIDER_DATA *)provider_data;
  if (node == &data->root && size == 6 && memcmp(name, "lambda", 6) == 0) {
    return &data->lambda;
  }
  return NULL;
}

static void *runtime_get_child_by_index(void *node, unsigned index, void *provider_data) {
  (void)provider_data;
  return node && index == 0 ? node : NULL;
}

static int runtime_dump(void *node, int (*out_fn)(const char *, size_t, void *),
                        void *renderer_data, void *provider_data) {
  (void)node;
  (void)out_fn;
  (void)renderer_data;
  (void)provider_data;
  return 0;
}

static MUSTACHE_TEMPLATE *runtime_get_partial(const char *name, size_t size,
                                              void *provider_data) {
  RUNTIME_PROVIDER_DATA *data = (RUNTIME_PROVIDER_DATA *)provider_data;
  return size == 4 && memcmp(name, "self", 4) == 0 ? data->partial : NULL;
}

static int runtime_is_lambda(void *node, void *provider_data) {
  return node == &((RUNTIME_PROVIDER_DATA *)provider_data)->lambda;
}

static int runtime_call_lambda(void *node, const char *text, size_t text_len, char **out_text,
                               size_t *out_len, void *provider_data) {
  RUNTIME_PROVIDER_DATA *data = (RUNTIME_PROVIDER_DATA *)provider_data;
  static const char result[] = "value";
  (void)node;
  (void)text;
  (void)text_len;

  if (data->fail_lambda) return -1;
  *out_text = (char *)malloc(sizeof(result));
  if (!*out_text) return -1;
  memcpy(*out_text, result, sizeof(result));
  *out_len = sizeof(result) - 1;
  return 0;
}

static MUSTACHE_DATAPROVIDER runtime_provider(void) {
  MUSTACHE_DATAPROVIDER provider = {
      runtime_dump,          runtime_get_root, runtime_get_child_by_name,
      runtime_get_child_by_index, runtime_get_partial, runtime_is_lambda,
      runtime_call_lambda};
  return provider;
}

static int failing_output(const char *output, size_t size, void *renderer_data) {
  (void)output;
  (void)size;
  (void)renderer_data;
  return -1;
}

static char *render_json_text(const char *template_text, const char *json_text) {
  json_value_t *json = json_parse(json_text, strlen(json_text));
  MUSTACHE_TEMPLATE *templ = NULL;
  MUSTACHE_STRING_RENDERER renderer = {0};
  char *result = NULL;
  int renderer_ready = 0;

  if (!json) return NULL;
  templ = mustache_compile(template_text, strlen(template_text), NULL, NULL, 0);
  if (!templ) goto cleanup;
  if (mustache_string_renderer_init(&renderer) != 0) goto cleanup;
  renderer_ready = 1;
  if (mustache_render_json(templ, json, &renderer.base, &renderer, NULL, NULL) != 0) goto cleanup;
  result = mustache_string_renderer_get(&renderer);

cleanup:
  if (renderer_ready) mustache_string_renderer_free(&renderer);
  mustache_release(templ);
  json_free(json);
  return result;
}

spec("mustache runtime regressions") {
  describe("arena renderer") {
    it("should reserve the full escaped quote plus terminator") {
      mem_pool_t pool = {0};
      MUSTACHE_STRING_RENDERER_ARENA renderer = {0};
      char *prefix = NULL;
      char *result = NULL;
      size_t prefix_len = 0;
      int pool_ready = 0;
      int renderer_ready = 0;

      pool_ready = mem_init(&pool, 0) == 0;
      check(pool_ready);
      if (pool_ready) {
        renderer_ready = mustache_string_renderer_init_arena(&renderer, &pool, 1024) == 0;
        check(renderer_ready);
      }
      if (renderer_ready) check_size_gt(renderer.buffer->capacity, 5);

      if (renderer_ready && renderer.buffer->capacity > 5) {
        prefix_len = renderer.buffer->capacity - 5;
        prefix = (char *)malloc(prefix_len);
        check_not_null(prefix);
      }
      if (prefix) {
        memset(prefix, 'a', prefix_len);
        check_int_eq(renderer.base.out_verbatim(prefix, prefix_len, &renderer), 0);
        check_int_eq(renderer.base.out_escaped("\"", 1, &renderer), 0);
        check_size_eq(renderer.buffer->used, prefix_len + 6);
        result = mustache_string_renderer_get_arena(&renderer);
        check_not_null(result);
        if (result) check_mem_eq(result + prefix_len, "&quot;", 6);
      }

      free(prefix);
      if (renderer_ready) mustache_string_renderer_free_arena(&renderer);
      if (pool_ready) mem_destroy(&pool);
    }

    it("should reject a missing arena") {
      MUSTACHE_STRING_RENDERER_ARENA renderer;
      check_int_ne(mustache_string_renderer_init_arena(&renderer, NULL, 16), 0);
    }
  }

  describe("JSON provider") {
    it("should preserve large JSON integer text") {
      char *result = render_json_text("{{signed}}|{{unsigned}}",
                                      "{\"signed\":9007199254740993,"
                                      "\"unsigned\":18446744073709551615}");
      check_not_null(result);
      if (result) check_str_eq(result, "9007199254740993|18446744073709551615");
      free(result);
    }

    it("should resolve names without allocating a C string in arena mode") {
      static const char template_text[] = "{{answer}}";
      static const char json_text[] = "{\"answer\":\"ok\"}";
      json_value_t *json = json_parse(json_text, sizeof(json_text) - 1);
      MUSTACHE_TEMPLATE *templ = mustache_compile(template_text, sizeof(template_text) - 1,
                                                  NULL, NULL, 0);
      MUSTACHE_JSON_PROVIDER provider = {0};
      MUSTACHE_STRING_RENDERER renderer = {0};
      mem_pool_t pool = {0};
      char *result = NULL;
      int pool_ready = 0;
      int provider_ready = 0;
      int renderer_ready = 0;

      check_not_null(json);
      check_not_null(templ);
      pool_ready = mem_init(&pool, 0) == 0;
      check(pool_ready);
      if (pool_ready) {
        provider_ready =
            mustache_json_provider_init_arena(&provider, json, NULL, NULL, &pool) == 0;
        check(provider_ready);
      }
      renderer_ready = mustache_string_renderer_init(&renderer) == 0;
      check(renderer_ready);
      if (json && templ && provider_ready && renderer_ready) {
        check_int_eq(mustache_process(templ, &renderer.base, &renderer, &provider.base, &provider),
                     0);
        result = mustache_string_renderer_get(&renderer);
        check_not_null(result);
        if (result) check_str_eq(result, "ok");
      }

      free(result);
      if (renderer_ready) mustache_string_renderer_free(&renderer);
      if (pool_ready) mem_destroy(&pool);
      mustache_release(templ);
      json_free(json);
    }
  }

  describe("failure propagation") {
    it("should propagate lambda callback failures") {
      static const char template_text[] = "{{lambda}}";
      MUSTACHE_TEMPLATE *templ = mustache_compile(template_text, sizeof(template_text) - 1,
                                                  NULL, NULL, 0);
      MUSTACHE_DATAPROVIDER provider = runtime_provider();
      RUNTIME_PROVIDER_DATA data = {0};
      MUSTACHE_STRING_RENDERER renderer = {0};
      int renderer_ready;

      data.fail_lambda = 1;
      check_not_null(templ);
      renderer_ready = mustache_string_renderer_init(&renderer) == 0;
      check(renderer_ready);
      if (templ && renderer_ready) {
        check_int_ne(mustache_process(templ, &renderer.base, &renderer, &provider, &data), 0);
      }
      if (renderer_ready) mustache_string_renderer_free(&renderer);
      mustache_release(templ);
    }

    it("should propagate section lambda callback failures") {
      static const char template_text[] = "{{#lambda}}content{{/lambda}}";
      MUSTACHE_TEMPLATE *templ = mustache_compile(template_text, sizeof(template_text) - 1,
                                                  NULL, NULL, 0);
      MUSTACHE_DATAPROVIDER provider = runtime_provider();
      RUNTIME_PROVIDER_DATA data = {0};
      MUSTACHE_STRING_RENDERER renderer = {0};
      int renderer_ready;

      data.fail_lambda = 1;
      check_not_null(templ);
      renderer_ready = mustache_string_renderer_init(&renderer) == 0;
      check(renderer_ready);
      if (templ && renderer_ready) {
        check_int_ne(mustache_process(templ, &renderer.base, &renderer, &provider, &data), 0);
      }
      if (renderer_ready) mustache_string_renderer_free(&renderer);
      mustache_release(templ);
    }

    it("should propagate output failures after lambda rendering") {
      static const char template_text[] = "{{lambda}}";
      MUSTACHE_TEMPLATE *templ = mustache_compile(template_text, sizeof(template_text) - 1,
                                                  NULL, NULL, 0);
      MUSTACHE_DATAPROVIDER provider = runtime_provider();
      MUSTACHE_RENDERER renderer = {failing_output, failing_output};
      RUNTIME_PROVIDER_DATA data = {0};

      check_not_null(templ);
      if (templ) check_int_ne(mustache_process(templ, &renderer, NULL, &provider, &data), 0);
      mustache_release(templ);
    }
  }

  describe("render depth") {
    it("should stop self-recursive partials at the configured limit") {
      static const char template_text[] = "{{>self}}";
      MUSTACHE_TEMPLATE *templ = mustache_compile(template_text, sizeof(template_text) - 1,
                                                  NULL, NULL, 0);
      MUSTACHE_DATAPROVIDER provider = runtime_provider();
      RUNTIME_PROVIDER_DATA data = {0};
      MUSTACHE_STRING_RENDERER renderer = {0};
      int renderer_ready;

      data.partial = templ;
      check_not_null(templ);
      renderer_ready = mustache_string_renderer_init(&renderer) == 0;
      check(renderer_ready);
      if (templ && renderer_ready) {
        check_int_ne(mustache_process_ex(templ, &renderer.base, &renderer, &provider, &data, 4), 0);
      }
      if (renderer_ready) mustache_string_renderer_free(&renderer);
      mustache_release(templ);
    }
  }
}
