/* Appended to SQLite 3.50.4's amalgamation so internal symbols stay private. */
#include "sqlite3_vdbe.h"

#if SQLITE_VERSION_NUMBER != 3050004
#error "The direct VDBE bridge is locked to SQLite 3.50.4"
#endif

#if OP_SeekGE != 23 || OP_Next != 39 || OP_Gt != 55 || OP_VFilter != 6 || \
    OP_Transaction != 2 || OP_SeekRowid != 30 || OP_VNext != 63 || \
    OP_Halt != 70 || OP_Integer != 71 || \
    OP_ResultRow != 84 || OP_Column != 94 || OP_MakeRecord != 97 || OP_OpenRead != 102 || \
    OP_Add != 107 || OP_OpenWrite != 113 || OP_String8 != 118 || OP_Close != 122 || \
    OP_Insert != 128 || OP_Rowid != 135 || OP_CreateBtree != 147 || OP_TableLock != 169 || \
    OP_VOpen != 173 || OP_VColumn != 176
#error "SQLite 3.50.4 VDBE opcode values changed"
#endif

enum { SQLITE_VDBE_BRIDGE_LAST_OPCODE = 189 };

static Vdbe *sqlite3VdbeBridgeProgram(sqlite3_vdbe_program *program) {
  return (Vdbe *)(void *)program;
}

static int sqlite3VdbeBridgeBuildable(Vdbe *program) {
  return program != 0 && program->db != 0 && program->eVdbeState == VDBE_INIT_STATE;
}

static int sqlite3VdbeBridgeRegisterValid(Vdbe *program, int reg) {
  int maxRegister;
  if (program == 0 || program->eVdbeState != VDBE_READY_STATE) return 0;
  maxRegister = program->nMem - program->nCursor;
  if (program->nCursor == 0 && maxRegister > 0) --maxRegister;
  return reg > 0 && reg <= maxRegister;
}

SQLITE_API sqlite3_vdbe_program *sqlite3_vdbe_create(sqlite3 *db) {
  Parse parse;
  Vdbe *program;
  if (!sqlite3SafetyCheckOk(db)) return 0;
  sqlite3_mutex_enter(db->mutex);
  memset(&parse, 0, sizeof(parse));
  parse.db = db;
  program = sqlite3VdbeCreate(&parse);
  if (program != 0) program->pParse = 0;
  (void)sqlite3ApiExit(db, program != 0 ? SQLITE_OK : SQLITE_NOMEM);
  sqlite3_mutex_leave(db->mutex);
  return (sqlite3_vdbe_program *)(void *)program;
}

static int sqlite3VdbeBridgeAddOp(sqlite3_vdbe_program *opaque, int opcode, int p1, int p2,
                                  int p3, int p4, int operandCount) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  int address = -1;
  if (!sqlite3VdbeBridgeBuildable(program) || opcode < 0 ||
      opcode > SQLITE_VDBE_BRIDGE_LAST_OPCODE) {
    return -1;
  }
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  switch (operandCount) {
    case 0:
      address = sqlite3VdbeAddOp0(program, opcode);
      break;
    case 1:
      address = sqlite3VdbeAddOp1(program, opcode, p1);
      break;
    case 2:
      address = sqlite3VdbeAddOp2(program, opcode, p1, p2);
      break;
    case 3:
      address = sqlite3VdbeAddOp3(program, opcode, p1, p2, p3);
      break;
    case 4:
      address = sqlite3VdbeAddOp4Int(program, opcode, p1, p2, p3, p4);
      break;
    default:
      break;
  }
  if (sqlite3ApiExit(db, SQLITE_OK) != SQLITE_OK) address = -1;
  sqlite3_mutex_leave(db->mutex);
  return address;
}

SQLITE_API int sqlite3_vdbe_add_op0(sqlite3_vdbe_program *program, int opcode) {
  return sqlite3VdbeBridgeAddOp(program, opcode, 0, 0, 0, 0, 0);
}

SQLITE_API int sqlite3_vdbe_add_op1(sqlite3_vdbe_program *program, int opcode, int p1) {
  return sqlite3VdbeBridgeAddOp(program, opcode, p1, 0, 0, 0, 1);
}

SQLITE_API int sqlite3_vdbe_add_op2(sqlite3_vdbe_program *program, int opcode, int p1, int p2) {
  return sqlite3VdbeBridgeAddOp(program, opcode, p1, p2, 0, 0, 2);
}

SQLITE_API int sqlite3_vdbe_add_op3(sqlite3_vdbe_program *program, int opcode, int p1, int p2,
                                    int p3) {
  return sqlite3VdbeBridgeAddOp(program, opcode, p1, p2, p3, 0, 3);
}

SQLITE_API int sqlite3_vdbe_add_op4_int(sqlite3_vdbe_program *program, int opcode, int p1,
                                        int p2, int p3, int p4) {
  return sqlite3VdbeBridgeAddOp(program, opcode, p1, p2, p3, p4, 4);
}

SQLITE_API int sqlite3_vdbe_add_open_read_table(sqlite3_vdbe_program *opaque, int cursor,
                                                int databaseIndex,
                                                const char *tableName) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  Table *table;
  int address = -1;
  if (!sqlite3VdbeBridgeBuildable(program) || cursor < 0 || tableName == 0) return -1;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  if (databaseIndex >= 0 && databaseIndex < db->nDb &&
      databaseIndex < (int)sizeof(yDbMask) * 8) {
    table = sqlite3FindTable(db, tableName, db->aDb[databaseIndex].zDbSName);
    if (table != 0 && !IsVirtual(table) && HasRowid(table) && table->tnum > 0) {
      sqlite3VdbeUsesBtree(program, databaseIndex);
      if (databaseIndex != 1 && sqlite3BtreeSharable(db->aDb[databaseIndex].pBt) &&
          sqlite3VdbeAddOp4(program, OP_TableLock, databaseIndex, table->tnum, 0,
                            tableName, P4_TRANSIENT) < 0) {
        table = 0;
      }
      if (table != 0) {
        address = sqlite3VdbeAddOp4Int(program, OP_OpenRead, cursor, table->tnum,
                                      databaseIndex, table->nNVCol);
      }
    }
  }
  if (sqlite3ApiExit(db, SQLITE_OK) != SQLITE_OK) address = -1;
  sqlite3_mutex_leave(db->mutex);
  return address;
}

SQLITE_API int sqlite3_vdbe_add_text(sqlite3_vdbe_program *opaque, int reg,
                                     const char *value) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  int address = -1;
  if (!sqlite3VdbeBridgeBuildable(program) || reg <= 0 || value == 0) return -1;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  address = sqlite3VdbeAddOp4(program, OP_String8, 0, reg, 0, value, P4_TRANSIENT);
  if (sqlite3ApiExit(db, SQLITE_OK) != SQLITE_OK) address = -1;
  sqlite3_mutex_leave(db->mutex);
  return address;
}

SQLITE_API int sqlite3_vdbe_add_vopen(sqlite3_vdbe_program *opaque, int cursor,
                                      sqlite3_vtab *vtab) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  const sqlite3_module *callbacks;
  sqlite3 *db;
  Module *module = 0;
  VTable *wrapper = 0;
  int address = -1;
  int installed = 0;
  if (!sqlite3VdbeBridgeBuildable(program) || cursor < 0 || vtab == 0 ||
      vtab->pModule == 0) {
    return -1;
  }
  callbacks = vtab->pModule;
  if (callbacks->xDisconnect == 0 || callbacks->xOpen == 0 || callbacks->xClose == 0 ||
      callbacks->xFilter == 0 || callbacks->xNext == 0 || callbacks->xEof == 0 ||
      callbacks->xColumn == 0) {
    return -1;
  }

  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  module = (Module *)sqlite3DbMallocZero(db, sizeof(*module));
  wrapper = (VTable *)sqlite3DbMallocZero(db, sizeof(*wrapper));
  if (module != 0 && wrapper != 0) {
    module->pModule = callbacks;
    module->nRefModule = 1;
    wrapper->db = db;
    wrapper->pMod = module;
    wrapper->pVtab = vtab;
    address = sqlite3VdbeAddOp3(program, OP_VOpen, cursor, 0, 0);
    if (address >= 0) {
      sqlite3VdbeChangeP4(program, address, (const char *)wrapper, P4_VTAB);
      installed = program->aOp[address].p4type == P4_VTAB;
      if (!installed) address = -1;
    }
  }
  if (sqlite3ApiExit(db, SQLITE_OK) != SQLITE_OK && !installed) address = -1;
  if (address < 0 && !installed) {
    sqlite3DbFree(db, wrapper);
    sqlite3DbFree(db, module);
  }
  sqlite3_mutex_leave(db->mutex);
  return address;
}

SQLITE_API int sqlite3_vdbe_add_vfilter(sqlite3_vdbe_program *opaque, int cursor,
                                        int empty_jump, int plan_register,
                                        const char *idx_string) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  int address = -1;
  if (!sqlite3VdbeBridgeBuildable(program) || cursor < 0 || empty_jump < 0 ||
      plan_register <= 0) {
    return -1;
  }
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  address = sqlite3VdbeAddOp4(program, OP_VFilter, cursor, empty_jump, plan_register,
                             idx_string, P4_TRANSIENT);
  if (sqlite3ApiExit(db, SQLITE_OK) != SQLITE_OK) address = -1;
  sqlite3_mutex_leave(db->mutex);
  return address;
}

SQLITE_API int sqlite3_vdbe_current_addr(sqlite3_vdbe_program *opaque) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  int address;
  if (!sqlite3VdbeBridgeBuildable(program)) return -1;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  address = sqlite3VdbeCurrentAddr(program);
  sqlite3_mutex_leave(db->mutex);
  return address;
}

SQLITE_API int sqlite3_vdbe_jump_here(sqlite3_vdbe_program *opaque, int address) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  if (!sqlite3VdbeBridgeBuildable(program) || address < 0 || address >= program->nOp) {
    return SQLITE_MISUSE;
  }
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  sqlite3VdbeJumpHere(program, address);
  sqlite3_mutex_leave(db->mutex);
  return SQLITE_OK;
}

SQLITE_API int sqlite3_vdbe_use_btree(sqlite3_vdbe_program *opaque, int databaseIndex) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  if (!sqlite3VdbeBridgeBuildable(program)) return SQLITE_MISUSE;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  if (databaseIndex < 0 || databaseIndex >= db->nDb || databaseIndex >= (int)sizeof(yDbMask) * 8) {
    sqlite3_mutex_leave(db->mutex);
    return SQLITE_RANGE;
  }
  sqlite3VdbeUsesBtree(program, databaseIndex);
  sqlite3_mutex_leave(db->mutex);
  return SQLITE_OK;
}

SQLITE_API int sqlite3_vdbe_set_num_columns(sqlite3_vdbe_program *opaque, int columnCount) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  int rc;
  if (!sqlite3VdbeBridgeBuildable(program) || columnCount < 0 || columnCount > 65535) {
    return SQLITE_MISUSE;
  }
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  sqlite3VdbeSetNumCols(program, columnCount);
  rc = sqlite3ApiExit(db, SQLITE_OK);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

SQLITE_API int sqlite3_vdbe_make_ready(sqlite3_vdbe_program *opaque, int maxRegister,
                                       int cursorCount) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  Parse parse;
  sqlite3 *db;
  int rc;
  if (!sqlite3VdbeBridgeBuildable(program) || maxRegister < 0 || cursorCount < 0) {
    return SQLITE_MISUSE;
  }
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  memset(&parse, 0, sizeof(parse));
  parse.db = db;
  parse.pVdbe = program;
  parse.nMem = maxRegister;
  parse.nTab = cursorCount;
  parse.szOpAlloc = sqlite3DbMallocSize(db, program->aOp);
  program->pParse = &parse;
  sqlite3VdbeMakeReady(program, &parse);
  program->pParse = 0;
  rc = sqlite3ApiExit(db, SQLITE_OK);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

SQLITE_API int sqlite3_vdbe_set_int64(sqlite3_vdbe_program *opaque, int reg,
                                      sqlite3_int64 value) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  if (!sqlite3VdbeBridgeRegisterValid(program, reg)) return SQLITE_RANGE;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  sqlite3VdbeMemSetInt64(&program->aMem[reg], value);
  sqlite3_mutex_leave(db->mutex);
  return SQLITE_OK;
}

SQLITE_API int sqlite3_vdbe_set_double(sqlite3_vdbe_program *opaque, int reg, double value) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  if (!sqlite3VdbeBridgeRegisterValid(program, reg)) return SQLITE_RANGE;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  sqlite3VdbeMemSetDouble(&program->aMem[reg], value);
  sqlite3_mutex_leave(db->mutex);
  return SQLITE_OK;
}

SQLITE_API int sqlite3_vdbe_set_null(sqlite3_vdbe_program *opaque, int reg) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  if (!sqlite3VdbeBridgeRegisterValid(program, reg)) return SQLITE_RANGE;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  sqlite3VdbeMemSetNull(&program->aMem[reg]);
  sqlite3_mutex_leave(db->mutex);
  return SQLITE_OK;
}

SQLITE_API int sqlite3_vdbe_step(sqlite3_vdbe_program *opaque) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  int rc;
  if (program == 0 || program->db == 0 || program->eVdbeState == VDBE_INIT_STATE) {
    return SQLITE_MISUSE;
  }
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  rc = sqlite3Step(program);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

SQLITE_API int sqlite3_vdbe_reset(sqlite3_vdbe_program *opaque) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  int rc;
  if (program == 0 || program->db == 0 || program->eVdbeState == VDBE_INIT_STATE) {
    return SQLITE_MISUSE;
  }
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  rc = sqlite3VdbeReset(program);
  sqlite3VdbeRewind(program);
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

SQLITE_API int sqlite3_vdbe_finalize(sqlite3_vdbe_program *opaque) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  int rc;
  if (program == 0) return SQLITE_OK;
  if (program->db == 0) return SQLITE_MISUSE;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  rc = sqlite3VdbeFinalize(program);
  rc = sqlite3ApiExit(db, rc);
  sqlite3_mutex_leave(db->mutex);
  return rc;
}

static Mem *sqlite3VdbeBridgeColumn(Vdbe *program, int column) {
  if (program == 0 || program->pResultRow == 0 || column < 0 || column >= program->nResColumn) {
    return 0;
  }
  return &program->pResultRow[column];
}

SQLITE_API int sqlite3_vdbe_column_type(sqlite3_vdbe_program *opaque, int column) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  Mem *value;
  int type = SQLITE_NULL;
  if (program == 0 || program->db == 0) return SQLITE_NULL;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  value = sqlite3VdbeBridgeColumn(program, column);
  if (value != 0) type = sqlite3_value_type((sqlite3_value *)(void *)value);
  sqlite3_mutex_leave(db->mutex);
  return type;
}

SQLITE_API sqlite3_int64 sqlite3_vdbe_column_int64(sqlite3_vdbe_program *opaque, int column) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  Mem *value;
  sqlite3_int64 result = 0;
  if (program == 0 || program->db == 0) return 0;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  value = sqlite3VdbeBridgeColumn(program, column);
  if (value != 0) result = sqlite3VdbeIntValue(value);
  sqlite3_mutex_leave(db->mutex);
  return result;
}

SQLITE_API double sqlite3_vdbe_column_double(sqlite3_vdbe_program *opaque, int column) {
  Vdbe *program = sqlite3VdbeBridgeProgram(opaque);
  sqlite3 *db;
  Mem *value;
  double result = 0.0;
  if (program == 0 || program->db == 0) return 0.0;
  db = program->db;
  sqlite3_mutex_enter(db->mutex);
  value = sqlite3VdbeBridgeColumn(program, column);
  if (value != 0) result = sqlite3VdbeRealValue(value);
  sqlite3_mutex_leave(db->mutex);
  return result;
}
