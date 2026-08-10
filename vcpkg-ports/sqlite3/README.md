# SQLite overlay port

This overlay is based on the `sqlite3` port selected by vcpkg baseline
`b1b19307e2d2ec1eefbdb7ea069de7d4bcd31f01` and remains pinned to SQLite
3.50.4 (public domain).

Local changes are intentionally narrow:

- force the SQLite library to use static linkage;
- add the opt-in `vdbe` feature;
- append `sqlite3_vdbe_bridge.c` to the SQLite amalgamation and install its
  version-locked public header.

Bridge version 3 can construct `OP_VOpen`/`OP_VFilter` programs directly and
drive B-tree range cursors with `OP_SeekGE`, `OP_Gt`, and `OP_Next`. A
successful `sqlite3_vdbe_add_vopen()` transfers the supplied `sqlite3_vtab`
ownership to the VDBE program; finalization calls the module's `xDisconnect()`.
SQL text, module registration, and `sqlite3_prepare_v2()` are not required.
`sqlite3_vdbe_add_open_read_table()` similarly resolves a rowid table from the
current connection schema and emits `OP_OpenRead` without parsing SQL. The
`bench_sqlite_index_reference` target compares a prepared range query against
the direct cursor program on the same hot-cache table.

The bridge design was informed by
[`el-yawd/sqlite-vdbe`](https://github.com/el-yawd/sqlite-vdbe) commit
`d2521a5c65903ec1224359e42e63b4a83d9507c4`. No Rust code is copied.
