find_package(unofficial-sqlite3 CONFIG REQUIRED)

cmake_add_benchmark(bench_sqlite_csv_vdbe
  SOURCES bench_sqlite_csv_vdbe.c
  LIBS Rocida::CsvParser Rocida::Core Rocida::TinyTest
       unofficial::sqlite3::sqlite3
  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/../parser
           ${CMAKE_CURRENT_BINARY_DIR}/..
  FOLDER "parser/csv_parser/benchmarks")
