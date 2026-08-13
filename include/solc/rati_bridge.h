#ifndef SOLC_RATI_BRIDGE_H
#define SOLC_RATI_BRIDGE_H

#include "solc/programs.h"
#include "solc/sbf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLC_RATI_BRIDGE_INIT 0u
#define SOLC_RATI_BRIDGE_ATTESTATION 1u
#define SOLC_RATI_BRIDGE_CHAIN_PROOF 2u
#define SOLC_RATI_CHAIN_EVENT_HEADER_BYTES 51u
#define SOLC_RATI_ED25519_SIGNATURE_BYTES 64u
#define SOLC_RATI_TOKEN_DECIMALS 9u

typedef struct solc_rati_bridge_instruction {
    uint8_t kind;
    const uint8_t *station_pubkey;
    uint64_t epoch;
    uint64_t amount;
    const uint8_t *target;
    solc_slice event;
    solc_slice event_payload;
    const uint8_t *signature;
    uint64_t event_id;
    uint64_t event_timestamp;
    uint8_t event_type;
} solc_rati_bridge_instruction;

/* Strict replacement for the RATi bridge's ad-hoc instruction unpacker. */
solc_status solc_rati_bridge_instruction_decode(
    const uint8_t *input,
    size_t input_len,
    solc_rati_bridge_instruction *out,
    solc_error *error);

/* Owns all storage borrowed by `instruction` after a successful build. */
typedef struct solc_rati_token_cpi {
    uint8_t data[10];
    solc_cpi_account_meta metas[3];
    const solc_sbf_account *account_infos[4];
    size_t account_info_count;
    solc_cpi_instruction instruction;
} solc_rati_token_cpi;

/* Build classic SPL Token MintToChecked: mint, destination, authority. */
solc_status solc_rati_mint_checked_cpi(
    const solc_sbf_account *token_program,
    const solc_sbf_account *mint,
    const solc_sbf_account *destination,
    const solc_sbf_account *authority,
    uint64_t amount,
    int authority_via_pda,
    solc_rati_token_cpi *out);

/* Build classic SPL Token BurnChecked: token account, mint, authority. */
solc_status solc_rati_burn_checked_cpi(
    const solc_sbf_account *token_program,
    const solc_sbf_account *token_account,
    const solc_sbf_account *mint,
    const solc_sbf_account *authority,
    uint64_t amount,
    int authority_via_pda,
    solc_rati_token_cpi *out);

#ifdef __cplusplus
}
#endif

#endif
