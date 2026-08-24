cmake_minimum_required(VERSION 3.10)

project(fec_packet LANGUAGES C CXX)

option(FEC_BUILD_TESTS "Build FEC unit tests" ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(fec STATIC
    src/fec.cpp
    src/internal/fec_controller.cpp
    src/internal/fec_decoder.cpp
    src/internal/fec_encoder.cpp
    src/internal/fec_frame.cpp
    src/internal/fec_profile.cpp
    src/internal/fec_symbol.cpp
    src/internal/reed_solomon_codec.cpp
    src/internal/xor_codec.cpp
)
target_include_directories(fec
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(NOT MSVC)
    target_compile_options(fec PRIVATE -Wall -Wextra -Wpedantic)
else()
    target_compile_options(fec PRIVATE /W4)
endif()

if(FEC_BUILD_TESTS)
    enable_testing()

    add_executable(fec_tests tests/test_fec.cpp)
    target_link_libraries(fec_tests PRIVATE fec)
    # 测试中使用 assert 同时执行被测调用；Release 模式也必须保留断言求值。
    if(MSVC)
        target_compile_options(fec_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(fec_tests PRIVATE -UNDEBUG)
    endif()

    add_executable(fec_c_api_compile tests/c_api_compile.c)
    target_link_libraries(fec_c_api_compile PRIVATE fec)
    # 纯 C 源文件用于验证头文件，最终链接必须由 C++ 链接器完成。
    set_property(TARGET fec_c_api_compile PROPERTY LINKER_LANGUAGE CXX)

    add_test(NAME fec_tests COMMAND fec_tests)
    add_test(NAME fec_c_api_compile COMMAND fec_c_api_compile)
endif()
