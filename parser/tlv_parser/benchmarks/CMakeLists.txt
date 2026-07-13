add_executable(benchmark_tlv_parser benchmark_tlv_parser.c)
target_link_libraries(benchmark_tlv_parser PRIVATE TurboUtils::TinyTest ${TARGET_NAME})
cmake_config_target(benchmark_tlv_parser
                    FOLDER "parser/${TARGET_NAME}/benchmarks")
