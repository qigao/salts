/**
 * @file main.c
 * @brief tbe_compiler — code generator from schema definitions.
 *
 * Reads a .schema file, parses it, and renders output through a Mustache
 * template.  Supports multiple target languages by selecting different
 * template files (built-in or custom).
 *
 * CLI (via cmd_arger):
 *   tbe_compiler <file> [--template <file>] [--lang c|cpp|go|rust|python|py|ts|mir|bmir]
 *              [--output <file>] [--source-output <file>] [--dsl-output <file>]
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compiler_core.h"
#include "turbo_fs.h"
#include "turbo_parser.h"

static int resolve_resource_dir(const char *argv0, char *out, size_t out_size) {
    const char *path_env;
    const char *cursor;
    char candidate[TURBO_FS_MAX_PATH];
    char directory[TURBO_FS_MAX_PATH];

    if (argv0 == NULL || out == NULL || out_size == 0) return 0;
    if (strchr(argv0, '/') != NULL || strchr(argv0, '\\') != NULL) {
        return turbo_fs_path_dirname(argv0, out, out_size) == 0;
    }
    path_env = getenv("PATH");
    if (path_env == NULL) return 0;
    cursor = path_env;
    while (*cursor != '\0') {
        const char *end = strchr(cursor,
#ifdef _WIN32
                                 ';'
#else
                                 ':'
#endif
        );
        size_t len = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (len > 0 && len < sizeof(directory)) {
            memcpy(directory, cursor, len);
            directory[len] = '\0';
            if (turbo_fs_path_join(candidate, sizeof(candidate), directory, argv0) == 0 &&
                turbo_fs_access(candidate, TURBO_FS_ACCESS_EXISTS) == 0)
                return turbo_fs_path_dirname(candidate, out, out_size) == 0;
#ifdef _WIN32
            if (snprintf(candidate, sizeof(candidate), "%s\\%s.exe", directory, argv0) > 0 &&
                turbo_fs_access(candidate, TURBO_FS_ACCESS_EXISTS) == 0)
                return turbo_fs_path_dirname(candidate, out, out_size) == 0;
#endif
        }
        if (end == NULL) break;
        cursor = end + 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    char    *schema_path   = NULL;
    char    *template_path = NULL;
    char    *output_path   = NULL;
    char    *source_output_path = NULL;
    char    *guest_output_path = NULL;
    char    *dsl_output_path = NULL;
    int64_t  lang_enum     = 0;
    char resource_dir[TURBO_FS_MAX_PATH];

    if (!resolve_resource_dir(argc > 0 ? argv[0] : NULL, resource_dir, sizeof(resource_dir))) {
        fprintf(stderr, "Failed to locate tbe_compiler resource directory\n");
        return 1;
    }

    turbo_cmd_parser_t *parser = turbo_cmd_create("tbe_compiler", "1.0");
    
    turbo_cmd_add_required_string(parser, &schema_path, "schema",
                                 "Path to the .schema definition file");
    
    turbo_cmd_add_string(parser, &template_path, "template", "t",
                                 "Path to a custom Mustache template file");
    
    turbo_cmd_enum_t lang_choices[] = {
        { "c",      "C header output",                 TBE_COMPILER_LANG_C },
        { "cpp",    "C++ type output",                 TBE_COMPILER_LANG_CPP },
        { "cxx",    "C++ type output",                 TBE_COMPILER_LANG_CPP },
        { "go",     "Go type output",                  TBE_COMPILER_LANG_GO },
        { "rust",   "Rust struct output",              TBE_COMPILER_LANG_RUST },
        { "python", "Python dataclass output",         TBE_COMPILER_LANG_PYTHON },
        { "py",     "Python dataclass output",         TBE_COMPILER_LANG_PYTHON },
        { "ts",     "TypeScript type output",          TBE_COMPILER_LANG_TS },
        { "typescript", "TypeScript type output",      TBE_COMPILER_LANG_TS },
        { "mir",    "MIR textual IR output",           TBE_COMPILER_LANG_MIR },
        { "bmir",   "MIR binary IR output",            TBE_COMPILER_LANG_BMIR },
    };
    
    turbo_cmd_add_enum(parser, &lang_enum, "lang", "l",
                               "Target language (built-in template)",
                               lang_choices, sizeof(lang_choices) / sizeof(lang_choices[0]));
                               
    turbo_cmd_add_string(parser, &output_path, "output", "o",
                                 "Output file path (default: stdout)");

    turbo_cmd_add_string(parser, &source_output_path, "source-output", "s",
                                 "Generate the C typed serde companion source");

    turbo_cmd_add_string(parser, &guest_output_path, "guest-output", "g",
                                 "Generate the C Wasm guest adapter source");
                                 
    turbo_cmd_add_string(parser, &dsl_output_path, "dsl-output", "d",
                                 "Generate DSL type declarations (.rfl file)");

    turbo_cmd_parse(parser, argc, argv, true);

    tbe_compiler_options_t options = {
        .schema_path = schema_path,
        .template_path = template_path,
        .output_path = output_path,
        .source_output_path = source_output_path,
        .guest_output_path = guest_output_path,
        .dsl_output_path = dsl_output_path,
        .resource_dir = resource_dir,
        .lang_enum = lang_enum,
    };

    int res = tbe_compiler_run(&options);
    turbo_cmd_destroy(parser);
    return res;
}
