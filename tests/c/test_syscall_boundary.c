#include "solc/crypto.h"
#include "solc/encoding.h"
#include "solc/programs.h"
#include "solc/rati_bridge.h"

#include <errno.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SOLC_SOURCE_DIR
#error "SOLC_SOURCE_DIR must identify the repository root"
#endif

static int hex_nibble(int value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static int load_legacy(uint8_t *output, size_t capacity, size_t *output_len) {
    const char path[] = SOLC_SOURCE_DIR "/tests/vectors/legacy.hex";
    FILE *file = fopen(path, "r");
    int high = -1;
    int comment = 0;
    int value;
    size_t length = 0u;
    if (file == NULL) {
        return 0;
    }
    while ((value = fgetc(file)) != EOF) {
        int nibble;
        if (comment != 0) {
            if (value == '\n') {
                comment = 0;
            }
            continue;
        }
        if (value == '#') {
            comment = 1;
            continue;
        }
        nibble = hex_nibble(value);
        if (nibble < 0) {
            continue;
        }
        if (high < 0) {
            high = nibble;
        } else {
            if (length == capacity) {
                fclose(file);
                return 0;
            }
            output[length++] = (uint8_t)((unsigned int)high << 4u | (unsigned int)nibble);
            high = -1;
        }
    }
    if (fclose(file) != 0 || high >= 0) {
        return 0;
    }
    *output_len = length;
    return 1;
}

static int install_no_host_syscalls(void) {
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_exit_group, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_rt_sigreturn, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
    };
    const struct sock_fprog program = {
        (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        filter,
    };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return 0;
    }
    return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program) == 0;
}

static int exercise_core(const uint8_t *legacy, size_t legacy_len) {
    solc_compiled_instruction instructions[SOLC_MAX_DECODE_INSTRUCTIONS];
    solc_address_table_lookup lookups[SOLC_MAX_DECODE_LOOKUPS];
    solc_decode_scratch scratch = {
        instructions,
        SOLC_MAX_DECODE_INSTRUCTIONS,
        lookups,
        SOLC_MAX_DECODE_LOOKUPS,
    };
    solc_transaction transaction;
    solc_error error;
    solc_system_instruction system_instruction;
    solc_rati_bridge_instruction rati_instruction;
    uint8_t encoded[SOLC_LEGACY_V0_MAX_TRANSACTION_BYTES];
    uint8_t digest[SOLC_HASH_BYTES];
    char base64[32];
    const uint8_t system_transfer[] = {
        2u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    };
    const uint8_t rati_init[] = {SOLC_RATI_BRIDGE_INIT};
    size_t encoded_len = 0u;
    size_t base64_len = 0u;
    uint8_t short_u16[3];
    size_t short_len = 0u;

    if (solc_transaction_decode(
            legacy, legacy_len, &scratch, &transaction, &error) != SOLC_OK ||
        solc_transaction_encode(&transaction,
                                encoded,
                                sizeof(encoded),
                                &encoded_len,
                                &error) != SOLC_OK ||
        encoded_len != legacy_len || memcmp(encoded, legacy, legacy_len) != 0 ||
        solc_sha256_builtin(NULL, encoded, encoded_len, digest) != SOLC_OK ||
        solc_base64_encode(digest, 8u, base64, sizeof(base64), &base64_len) != SOLC_OK ||
        base64_len == 0u ||
        solc_short_u16_encode(UINT16_MAX, short_u16, &short_len) != SOLC_OK ||
        short_len != 3u ||
        solc_system_instruction_decode(system_transfer,
                                       sizeof(system_transfer),
                                       &system_instruction,
                                       &error) != SOLC_OK ||
        solc_rati_bridge_instruction_decode(
            rati_init, sizeof(rati_init), &rati_instruction, &error) != SOLC_OK) {
        return 0;
    }
    return 1;
}

int main(void) {
    uint8_t legacy[SOLC_LEGACY_V0_MAX_TRANSACTION_BYTES];
    size_t legacy_len = 0u;
    pid_t child;
    int status;

    if (!load_legacy(legacy, sizeof(legacy), &legacy_len)) {
        fputs("failed to load syscall-boundary fixture\n", stderr);
        return EXIT_FAILURE;
    }
    if (!exercise_core(legacy, legacy_len)) {
        fputs("failed to warm syscall-boundary fixture\n", stderr);
        return EXIT_FAILURE;
    }
    child = fork();
    if (child < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }
    if (child == 0) {
        int success;
        if (!install_no_host_syscalls()) {
            _exit(125);
        }
        success = exercise_core(legacy, legacy_len);
        _exit(success ? 0 : 1);
    }
    if (waitpid(child, &status, 0) != child) {
        perror("waitpid");
        return EXIT_FAILURE;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "deterministic core attempted a forbidden host syscall\n");
        return EXIT_FAILURE;
    }
    puts("solc_syscall_boundary_tests: ok");
    return EXIT_SUCCESS;
}
