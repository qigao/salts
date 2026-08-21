#include "stream_turbo_containers.h"
#include "turbo_vstr.h"

#include <stdio.h>

static bool long_word(const void *value)
{
    return vstr_len(*(const vstr *)value) > 6;
}

static void print_word(const void *value)
{
    const vstr *word = (const vstr *)value;
    fwrite(word->data, 1, word->len, stdout);
    puts("");
}

static stream_result_t print_group(const char *label, turbo_list_t *values)
{
    stream_t stream;
    stream_t *s = &stream;

    if (!values) {
        puts(label);
        return STREAM_OK;
    }

    printf("%s\n", label);
    if (!STREAM_FROM_TURBO_LIST(s, values)) {
        return STREAM_ERROR;
    }
    return s->for_each(s, print_word);
}

int main(void)
{
    const vstr words[] = {
        vstr_from_cstr("stream"),
        vstr_from_cstr("partitioning"),
        vstr_from_cstr("turbo"),
        vstr_from_cstr("utils"),
        vstr_from_cstr("collectors"),
        vstr_from_cstr("view"),
    };
    turbo_list_t source;
    turbo_list_t long_words;
    turbo_list_t short_words;
    stream_t stream;
    const size_t word_count = sizeof(words) / sizeof(words[0]);

    if (turbo_list_from_array(&source, words, word_count, sizeof(words[0])) !=
        TURBO_OK) {
        return 1;
    }
    if (turbo_list_init(&long_words, sizeof(vstr)) != TURBO_OK) {
        turbo_list_destroy(&source);
        return 1;
    }
    if (turbo_list_init(&short_words, sizeof(vstr)) != TURBO_OK) {
        turbo_list_destroy(&long_words);
        turbo_list_destroy(&source);
        return 1;
    }

    if (!STREAM_FROM_TURBO_LIST(&stream, &source) ||
        stream_collect_turbo_partition(
            &stream, &long_words, 8, &short_words, 8, long_word) !=
            STREAM_END) {
        turbo_list_destroy(&short_words);
        turbo_list_destroy(&long_words);
        turbo_list_destroy(&source);
        return 1;
    }

    if (print_group("long words", &long_words) != STREAM_END ||
        print_group("short words", &short_words) != STREAM_END) {
        turbo_list_destroy(&short_words);
        turbo_list_destroy(&long_words);
        turbo_list_destroy(&source);
        return 1;
    }

    turbo_list_destroy(&short_words);
    turbo_list_destroy(&long_words);
    turbo_list_destroy(&source);
    return 0;
}
