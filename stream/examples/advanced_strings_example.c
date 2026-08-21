#include "stream_turbo_containers.h"
#include "turbo_vstr.h"

#include <stdio.h>

typedef struct {
    vstr text;
    size_t byte_length;
} word_info_t;

TURBO_LIST_DEFINE(vstr_list_t, vstr)

static bool contains_stream(const void *value)
{
    static const vstr needle = {"stream", 6};
    return vstr_contains(*(const vstr *)value, needle) != 0;
}

static stream_result_t describe_word(const void *input, void *output)
{
    const vstr *word = (const vstr *)input;
    word_info_t *info = (word_info_t *)output;

    if (!word->data && word->len != 0) {
        return STREAM_ERROR;
    }

    info->text = *word;
    info->byte_length = vstr_len(*word);
    return STREAM_OK;
}

static void print_word_info(const void *value)
{
    const word_info_t *info = (const word_info_t *)value;

    fwrite(info->text.data, 1, info->text.len, stdout);
    printf(" (%zu bytes)\n", info->byte_length);
}

static void print_word(const void *value)
{
    const vstr *word = (const vstr *)value;

    fwrite(word->data, 1, word->len, stdout);
    fputc('\n', stdout);
}

int main(void)
{
    const vstr source[] = {
        vstr_from_cstr("turbo-utils"),
        vstr_from_cstr("stream pipeline"),
        vstr_from_cstr("vstr borrowed view"),
        vstr_from_cstr("event stream"),
        vstr_from_cstr("container adapter"),
        vstr_from_cstr("stream reset")
    };
    vstr_list_t words;
    stream_t stream;
    stream_t *s = &stream;
    stream_result_t result;
    int exit_code = 1;

    if (vstr_list_t_from(
            &words, source, sizeof(source) / sizeof(source[0])) != TURBO_OK) {
        return 1;
    }

    if (stream_from_turbo_list(s, &words.raw) != STREAM_OK) {
        goto cleanup;
    }

    puts("strings containing 'stream':");
    result = s->filter(s, contains_stream)
              ->map(s, sizeof(word_info_t), describe_word)
              ->take(s, 3)
              ->for_each(s, print_word_info);
    if (result != STREAM_END || s->error != STREAM_ERR_NONE) {
        goto cleanup;
    }

    s->clear(s);
    if (s->reset(s) != STREAM_OK) {
        goto cleanup;
    }

    puts("all strings after reset, skipping the first:");
    result = s->skip(s, 1)->for_each(s, print_word);
    if (result != STREAM_END || s->error != STREAM_ERR_NONE) {
        goto cleanup;
    }

    exit_code = 0;

cleanup:
    vstr_list_t_destroy(&words);
    return exit_code;
}
