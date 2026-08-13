use solana_address_lookup_table_interface::state::{
    AddressLookupTable, LookupTableMeta, LOOKUP_TABLE_META_SIZE,
};
use solana_compute_budget_interface::ComputeBudgetInstruction;
use solana_program_option::COption;
use solana_program_pack::Pack;
use solana_pubkey::Pubkey;
use solana_system_interface::instruction::SystemInstruction;
use spl_token::instruction::TokenInstruction as ClassicTokenInstruction;
use spl_token::state::{Account, AccountState, Mint, Multisig};
use spl_token_2022::instruction::TokenInstruction as Token2022Instruction;
use spl_token_2022::state::{Account as Account2022, Mint as Mint2022};
use std::borrow::Cow;
use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};

fn key(byte: u8) -> Pubkey {
    Pubkey::new_from_array([byte; 32])
}

fn vectors() -> BTreeMap<&'static str, Vec<u8>> {
    let mut vectors = BTreeMap::new();
    vectors.insert(
        "system-create-account-with-seed.hex",
        bincode::serialize(&SystemInstruction::CreateAccountWithSeed {
            base: key(0x11),
            seed: "é".to_owned(),
            lamports: 0x0102_0304_0506_0708,
            space: 0x1112_1314_1516_1718,
            owner: key(0x22),
        })
        .expect("serialize system instruction"),
    );
    vectors.insert(
        "compute-unit-limit.hex",
        ComputeBudgetInstruction::set_compute_unit_limit(257).data,
    );
    vectors.insert(
        "compute-unit-price.hex",
        ComputeBudgetInstruction::set_compute_unit_price(2_000_001).data,
    );
    vectors.insert(
        "token-initialize-mint.hex",
        ClassicTokenInstruction::InitializeMint {
            decimals: 9,
            mint_authority: key(0x11),
            freeze_authority: COption::Some(key(0x22)),
        }
        .pack(),
    );
    vectors.insert(
        "token2022-get-account-data-size.hex",
        Token2022Instruction::GetAccountDataSize {
            extension_types: vec![
                spl_token_2022::extension::ExtensionType::TransferFeeConfig,
                spl_token_2022::extension::ExtensionType::PausableAccount,
            ],
        }
        .pack(),
    );

    let mint = Mint {
        mint_authority: COption::Some(key(0x11)),
        supply: 99,
        decimals: 6,
        is_initialized: true,
        freeze_authority: COption::None,
    };
    let mut mint_bytes = vec![0; Mint::LEN];
    Mint::pack(mint, &mut mint_bytes).expect("pack mint");
    vectors.insert("token-mint-account.hex", mint_bytes);

    let account = Account {
        mint: key(0x22),
        owner: key(0x33),
        amount: 77,
        delegate: COption::None,
        state: AccountState::Initialized,
        is_native: COption::None,
        delegated_amount: 0,
        close_authority: COption::None,
    };
    let mut account_bytes = vec![0; Account::LEN];
    Account::pack(account, &mut account_bytes).expect("pack token account");
    vectors.insert("token-account.hex", account_bytes);

    let multisig = Multisig {
        m: 2,
        n: 3,
        is_initialized: true,
        signers: [key(0x44); 11],
    };
    let mut multisig_bytes = vec![0; Multisig::LEN];
    Multisig::pack(multisig, &mut multisig_bytes).expect("pack multisig");
    vectors.insert("token-multisig.hex", multisig_bytes);

    let mint_2022 = Mint2022 {
        mint_authority: COption::Some(key(0x11)),
        supply: 99,
        decimals: 6,
        is_initialized: true,
        freeze_authority: COption::None,
    };
    let mut mint_2022_bytes = vec![0; Mint2022::LEN];
    Mint2022::pack(mint_2022, &mut mint_2022_bytes).expect("pack Token-2022 mint");
    assert_eq!(mint_2022_bytes, vectors["token-mint-account.hex"]);
    let account_2022 = Account2022 {
        mint: key(0x22),
        owner: key(0x33),
        amount: 77,
        delegate: COption::None,
        state: spl_token_2022::state::AccountState::Initialized,
        is_native: COption::None,
        delegated_amount: 0,
        close_authority: COption::None,
    };
    let mut account_2022_bytes = vec![0; Account2022::LEN];
    Account2022::pack(account_2022, &mut account_2022_bytes).expect("pack Token-2022 account");
    assert_eq!(account_2022_bytes, vectors["token-account.hex"]);

    let lookup = AddressLookupTable {
        meta: LookupTableMeta {
            deactivation_slot: u64::MAX,
            last_extended_slot: 42,
            last_extended_slot_start_index: 1,
            authority: Some(key(0x11)),
            _padding: 0,
        },
        addresses: Cow::Owned(vec![key(0x22), key(0x33)]),
    };
    let lookup_bytes = lookup
        .serialize_for_tests()
        .expect("serialize lookup table");
    assert_eq!(lookup_bytes.len(), LOOKUP_TABLE_META_SIZE + 64);
    vectors.insert("address-lookup-table.hex", lookup_bytes);
    vectors
}

fn vector_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../vectors/programs")
}

fn encode_hex(bytes: &[u8]) -> String {
    let mut output = String::with_capacity(bytes.len() * 2 + 1);
    for byte in bytes {
        output.push_str(&format!("{byte:02x}"));
    }
    output.push('\n');
    output
}

fn parse_hex(path: &Path) -> Vec<u8> {
    let text =
        fs::read_to_string(path).unwrap_or_else(|error| panic!("read {}: {error}", path.display()));
    let digits: String = text
        .chars()
        .filter(|character| !character.is_whitespace())
        .collect();
    assert_eq!(digits.len() % 2, 0, "odd hex length in {}", path.display());
    digits
        .as_bytes()
        .chunks_exact(2)
        .map(|pair| {
            u8::from_str_radix(std::str::from_utf8(pair).expect("ASCII hex"), 16)
                .unwrap_or_else(|error| panic!("invalid hex in {}: {error}", path.display()))
        })
        .collect()
}

fn main() {
    let expected = vectors();
    let directory = vector_dir();
    if std::env::args().any(|argument| argument == "--write") {
        fs::create_dir_all(&directory).expect("create program vector directory");
        for (name, bytes) in &expected {
            fs::write(directory.join(name), encode_hex(bytes)).expect("write program vector");
        }
    }
    for (name, bytes) in &expected {
        let path = directory.join(name);
        assert_eq!(parse_hex(&path), *bytes, "official encoding changed {name}");
        println!("official program crates accepted {}", path.display());
    }
}
