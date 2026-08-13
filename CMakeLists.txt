cmake_minimum_required(VERSION 3.20)
project(solc_wire VERSION 0.1.0 LANGUAGES C)

include(CTest)

option(SOLC_ENABLE_SANITIZERS "Build C tests with AddressSanitizer and UBSan" OFF)
option(SOLC_BUILD_FUZZER "Build the libFuzzer transaction decoder target" OFF)

add_library(solc_wire STATIC
    src/builder.c
    src/crypto.c
    src/encoding.c
    src/programs.c
    src/rati_bridge.c
    src/sbf.c
    src/wire.c
)
add_library(solc::wire ALIAS solc_wire)
target_include_directories(solc_wire
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(solc_wire PUBLIC c_std_11)

if(MSVC)
    target_compile_options(solc_wire PRIVATE /W4 /WX)
else()
    target_compile_options(solc_wire PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
    )
endif()

if(SOLC_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(solc_wire PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(solc_wire PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

if(BUILD_TESTING)
    add_executable(solc_wire_tests tests/c/test_wire.c)
    target_link_libraries(solc_wire_tests PRIVATE solc::wire)
    add_test(NAME solc_wire_tests COMMAND solc_wire_tests)

    add_executable(solc_encoding_tests tests/c/test_encoding.c)
    target_link_libraries(solc_encoding_tests PRIVATE solc::wire)
    add_test(NAME solc_encoding_tests COMMAND solc_encoding_tests)

    add_executable(solc_crypto_tests tests/c/test_crypto.c)
    target_link_libraries(solc_crypto_tests PRIVATE solc::wire)
    add_test(NAME solc_crypto_tests COMMAND solc_crypto_tests)

    add_executable(solc_sbf_tests tests/c/test_sbf.c)
    target_link_libraries(solc_sbf_tests PRIVATE solc::wire)
    add_test(NAME solc_sbf_tests COMMAND solc_sbf_tests)

    add_executable(solc_program_tests tests/c/test_programs.c)
    target_link_libraries(solc_program_tests PRIVATE solc::wire)
    add_test(NAME solc_program_tests COMMAND solc_program_tests)

    add_executable(solc_migration_tests tests/c/test_migrations.c)
    target_link_libraries(solc_migration_tests PRIVATE solc::wire)
    target_compile_definitions(solc_migration_tests PRIVATE
        SOLC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME solc_migration_tests COMMAND solc_migration_tests)
endif()

if(SOLC_BUILD_FUZZER)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "SOLC_BUILD_FUZZER requires Clang")
    endif()
    target_compile_options(solc_wire PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer
    )
    add_executable(solc_wire_fuzz tests/fuzz/fuzz_wire.c)
    target_link_libraries(solc_wire_fuzz PRIVATE solc::wire)
    target_compile_options(solc_wire_fuzz PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer
    )
    target_link_options(solc_wire_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
    )

    add_executable(solc_encoding_fuzz tests/fuzz/fuzz_encoding.c)
    target_link_libraries(solc_encoding_fuzz PRIVATE solc::wire)
    target_compile_options(solc_encoding_fuzz PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer
    )
    target_link_options(solc_encoding_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
    )

    add_executable(solc_sbf_fuzz tests/fuzz/fuzz_sbf.c)
    target_link_libraries(solc_sbf_fuzz PRIVATE solc::wire)
    target_compile_options(solc_sbf_fuzz PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer
    )
    target_link_options(solc_sbf_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
    )

    add_executable(solc_program_fuzz tests/fuzz/fuzz_programs.c)
    target_link_libraries(solc_program_fuzz PRIVATE solc::wire)
    target_compile_options(solc_program_fuzz PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer
    )
    target_link_options(solc_program_fuzz PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer
    )
endif()

install(TARGETS solc_wire EXPORT solc_wire_targets ARCHIVE DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
