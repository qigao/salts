cmake_add_benchmark(benchmark_tlv_parser
  SOURCES benchmark_tlv_parser.c
  LIBS Rocida::TinyTest ${TARGET_NAME}
  FOLDER "parser/${TARGET_NAME}/benchmarks")
