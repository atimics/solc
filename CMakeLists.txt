cmake_minimum_required(VERSION 3.20)
project(solc_wire VERSION 0.1.0 LANGUAGES C)

include(CTest)

option(SOLC_ENABLE_SANITIZERS "Build C tests with AddressSanitizer and UBSan" OFF)
option(SOLC_BUILD_FUZZER "Build the libFuzzer transaction decoder target" OFF)
option(SOLC_WARNINGS_AS_ERRORS "Treat C compiler warnings as errors" ON)
option(SOLC_ENABLE_GCC_ANALYZER "Enable GCC's interprocedural static analyzer" OFF)
option(SOLC_ENABLE_COVERAGE "Instrument C tests for LLVM source coverage" OFF)
option(SOLC_ENABLE_THREAD_SANITIZER "Build C tests with ThreadSanitizer" OFF)

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
    target_compile_options(solc_wire PRIVATE /W4)
    if(SOLC_WARNINGS_AS_ERRORS)
        target_compile_options(solc_wire PRIVATE /WX)
    endif()
else()
    target_compile_options(solc_wire PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wcast-align
        -Wpointer-arith
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wswitch-enum
        -Wundef
        -Wformat=2
        -Wnull-dereference
        -Wvla
        -Walloca
        -Wfloat-equal
        -Wdouble-promotion
    )
    if(SOLC_WARNINGS_AS_ERRORS)
        target_compile_options(solc_wire PRIVATE -Werror)
    endif()
    if(SOLC_ENABLE_GCC_ANALYZER)
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            target_compile_options(solc_wire PRIVATE -fanalyzer)
        else()
            message(FATAL_ERROR "SOLC_ENABLE_GCC_ANALYZER requires GCC")
        endif()
    endif()
endif()

if(SOLC_ENABLE_SANITIZERS AND SOLC_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "Address/UB sanitizers and ThreadSanitizer are mutually exclusive")
endif()

if(SOLC_ENABLE_SANITIZERS AND NOT MSVC)
    target_compile_options(solc_wire PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(solc_wire PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

if(SOLC_ENABLE_THREAD_SANITIZER)
    if(MSVC)
        message(FATAL_ERROR "SOLC_ENABLE_THREAD_SANITIZER is not supported by MSVC")
    endif()
    target_compile_options(solc_wire PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
    target_link_options(solc_wire PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
endif()

if(SOLC_ENABLE_COVERAGE)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "SOLC_ENABLE_COVERAGE requires Clang")
    endif()
    target_compile_options(solc_wire PUBLIC
        -O0 -g -fprofile-instr-generate -fcoverage-mapping
    )
    target_link_options(solc_wire PUBLIC -fprofile-instr-generate -fcoverage-mapping)
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

    add_executable(solc_determinism_tests tests/c/test_determinism.c)
    target_link_libraries(solc_determinism_tests PRIVATE solc::wire)
    target_compile_definitions(solc_determinism_tests PRIVATE
        SOLC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_test(NAME solc_determinism_tests COMMAND solc_determinism_tests)

    if(UNIX)
        find_package(Threads REQUIRED)
        add_executable(solc_thread_safety_tests tests/c/test_thread_safety.c)
        target_link_libraries(solc_thread_safety_tests PRIVATE solc::wire Threads::Threads)
        target_compile_definitions(solc_thread_safety_tests PRIVATE
            SOLC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME solc_thread_safety_tests COMMAND solc_thread_safety_tests)
        set_tests_properties(solc_thread_safety_tests PROPERTIES LABELS "thread-safety")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
       NOT SOLC_ENABLE_SANITIZERS AND
       NOT SOLC_ENABLE_THREAD_SANITIZER AND
       NOT SOLC_ENABLE_COVERAGE)
        add_executable(solc_syscall_boundary_tests tests/c/test_syscall_boundary.c)
        target_link_libraries(solc_syscall_boundary_tests PRIVATE solc::wire)
        target_compile_definitions(solc_syscall_boundary_tests PRIVATE
            SOLC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        )
        add_test(NAME solc_syscall_boundary_tests COMMAND solc_syscall_boundary_tests)
        set_tests_properties(solc_syscall_boundary_tests PROPERTIES LABELS "purity")
    endif()

    set_tests_properties(
        solc_wire_tests
        solc_encoding_tests
        solc_crypto_tests
        solc_sbf_tests
        solc_program_tests
        solc_migration_tests
        solc_determinism_tests
        PROPERTIES TIMEOUT 30
    )
    if(TARGET solc_thread_safety_tests)
        set_tests_properties(solc_thread_safety_tests PROPERTIES TIMEOUT 60)
    endif()
    if(TARGET solc_syscall_boundary_tests)
        set_tests_properties(solc_syscall_boundary_tests PROPERTIES TIMEOUT 30)
    endif()
    set_tests_properties(solc_determinism_tests PROPERTIES LABELS "determinism")
    set_tests_properties(solc_migration_tests PROPERTIES LABELS "migration")
endif()

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    find_program(SOLC_CLANG_EXECUTABLE NAMES clang)
    set(SOLC_POLICY_CLANG_ARGUMENTS)
    if(SOLC_CLANG_EXECUTABLE)
        list(APPEND SOLC_POLICY_CLANG_ARGUMENTS --clang "${SOLC_CLANG_EXECUTABLE}")
    endif()
    add_custom_target(solc_policy
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-kernel-policy.py"
            --policy "${CMAKE_CURRENT_SOURCE_DIR}/ci/kernel-policy.toml"
            ${SOLC_POLICY_CLANG_ARGUMENTS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking deterministic C core policy"
        VERBATIM
    )
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
