#ifndef SQLITE3_VDBE_H
#define SQLITE3_VDBE_H

#include <sqlite3.h>

#if SQLITE_VERSION_NUMBER != 3050004
#error "sqlite3_vdbe.h requires SQLite 3.50.4"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SQLITE_VDBE_BRIDGE_VERSION 3

typedef struct sqlite3_vdbe_program sqlite3_vdbe_program;

typedef enum sqlite3_vdbe_opcode {
  /* OP_SeekGE expects a rowid in P3 for an integer-key table; P2 is the exit. */
  SQLITE_VDBE_OP_SEEK_GE = 23,
  /* OP_Next advances the cursor and jumps to P2 while another row exists. */
  SQLITE_VDBE_OP_NEXT = 39,
  SQLITE_VDBE_OP_GT = 55,
  SQLITE_VDBE_OP_VFILTER = 6,
  SQLITE_VDBE_OP_VNEXT = 63,
  SQLITE_VDBE_OP_TRANSACTION = 2,
  SQLITE_VDBE_OP_SEEK_ROWID = 30,
  SQLITE_VDBE_OP_HALT = 70,
  SQLITE_VDBE_OP_INTEGER = 71,
  SQLITE_VDBE_OP_RESULT_ROW = 84,
  SQLITE_VDBE_OP_COLUMN = 94,
  SQLITE_VDBE_OP_MAKE_RECORD = 97,
  SQLITE_VDBE_OP_OPEN_READ = 102,
  SQLITE_VDBE_OP_ADD = 107,
  SQLITE_VDBE_OP_OPEN_WRITE = 113,
  SQLITE_VDBE_OP_STRING8 = 118,
  SQLITE_VDBE_OP_CLOSE = 122,
  SQLITE_VDBE_OP_INSERT = 128,
  SQLITE_VDBE_OP_ROWID = 135,
  SQLITE_VDBE_OP_CREATE_BTREE = 147,
  SQLITE_VDBE_OP_TABLE_LOCK = 169,
  SQLITE_VDBE_OP_VOPEN = 173,
  SQLITE_VDBE_OP_VCOLUMN = 176
} sqlite3_vdbe_opcode;

typedef enum sqlite3_vdbe_btree_kind {
  SQLITE_VDBE_BTREE_INTKEY = 1,
  SQLITE_VDBE_BTREE_BLOBKEY = 2
} sqlite3_vdbe_btree_kind;

/* The connection must outlive the program. A program is single-thread owned. */
SQLITE_API sqlite3_vdbe_program *sqlite3_vdbe_create(sqlite3 *db);
SQLITE_API int sqlite3_vdbe_add_op0(sqlite3_vdbe_program *program, int opcode);
SQLITE_API int sqlite3_vdbe_add_op1(sqlite3_vdbe_program *program, int opcode, int p1);
SQLITE_API int sqlite3_vdbe_add_op2(sqlite3_vdbe_program *program, int opcode, int p1, int p2);
SQLITE_API int sqlite3_vdbe_add_op3(sqlite3_vdbe_program *program, int opcode, int p1, int p2,
                                    int p3);
SQLITE_API int sqlite3_vdbe_add_op4_int(sqlite3_vdbe_program *program, int opcode, int p1,
                                        int p2, int p3, int p4);
/**
 * Adds an OP_OpenRead table cursor by resolving table_name from the current
 * connection schema. The schema must remain unchanged until finalization.
 */
SQLITE_API int sqlite3_vdbe_add_open_read_table(sqlite3_vdbe_program *program, int cursor,
                                                int database_index,
                                                const char *table_name);
SQLITE_API int sqlite3_vdbe_add_text(sqlite3_vdbe_program *program, int reg,
                                    const char *value);
/**
 * Adds OP_VOpen and transfers vtab ownership to program on success.
 * vtab->pModule and its scan callbacks must remain valid until finalization.
 * On failure the caller retains ownership and must call xDisconnect.
 */
SQLITE_API int sqlite3_vdbe_add_vopen(sqlite3_vdbe_program *program, int cursor,
                                     sqlite3_vtab *vtab);
/** Adds OP_VFilter and copies idx_string into the program when non-NULL. */
SQLITE_API int sqlite3_vdbe_add_vfilter(sqlite3_vdbe_program *program, int cursor,
                                       int empty_jump, int plan_register,
                                       const char *idx_string);
SQLITE_API int sqlite3_vdbe_current_addr(sqlite3_vdbe_program *program);
SQLITE_API int sqlite3_vdbe_jump_here(sqlite3_vdbe_program *program, int address);
SQLITE_API int sqlite3_vdbe_use_btree(sqlite3_vdbe_program *program, int database_index);
SQLITE_API int sqlite3_vdbe_set_num_columns(sqlite3_vdbe_program *program, int column_count);
SQLITE_API int sqlite3_vdbe_make_ready(sqlite3_vdbe_program *program, int max_register,
                                       int cursor_count);
SQLITE_API int sqlite3_vdbe_set_int64(sqlite3_vdbe_program *program, int reg,
                                      sqlite3_int64 value);
SQLITE_API int sqlite3_vdbe_set_double(sqlite3_vdbe_program *program, int reg, double value);
SQLITE_API int sqlite3_vdbe_set_null(sqlite3_vdbe_program *program, int reg);
SQLITE_API int sqlite3_vdbe_step(sqlite3_vdbe_program *program);
SQLITE_API int sqlite3_vdbe_reset(sqlite3_vdbe_program *program);
SQLITE_API int sqlite3_vdbe_finalize(sqlite3_vdbe_program *program);
SQLITE_API int sqlite3_vdbe_column_type(sqlite3_vdbe_program *program, int column);
SQLITE_API sqlite3_int64 sqlite3_vdbe_column_int64(sqlite3_vdbe_program *program, int column);
SQLITE_API double sqlite3_vdbe_column_double(sqlite3_vdbe_program *program, int column);

#ifdef __cplusplus
}
#endif

#endif
