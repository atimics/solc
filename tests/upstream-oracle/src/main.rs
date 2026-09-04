use solana_message::VersionedMessage;
use solana_transaction::versioned::VersionedTransaction;
use std::fs;
use std::path::{Path, PathBuf};

fn parse_hex(path: &Path) -> Vec<u8> {
    let text = fs::read_to_string(path).unwrap_or_else(|error| {
        panic!("read {}: {error}", path.display());
    });
    let digits = text
        .lines()
        .flat_map(|line| line.split('#').next().unwrap_or_default().chars())
        .filter(|character| !character.is_whitespace())
        .collect::<String>();
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

fn vector_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../vectors")
}

fn check_verification(transaction: &VersionedTransaction, label: &str, accepted: bool) {
    let official = transaction.verify_and_hash_message().is_ok();
    let bytes = wincode::serialize(transaction).expect("serialize verification case");
    let native = solc_orchestrator::verify_transaction(&bytes).is_ok();
    assert_eq!(official, accepted, "official verification: {label}");
    assert_eq!(
        native, official,
        "C/official verification mismatch: {label}"
    );
    if let Some(legacy) = transaction.clone().into_legacy_transaction() {
        assert_eq!(
            legacy.verify().is_ok(),
            official,
            "legacy verification: {label}"
        );
        assert_eq!(
            legacy.verify_and_hash_message().is_ok(),
            official,
            "legacy verification and hash: {label}"
        );
    }
}

fn check_malformed_signatures(transaction: &VersionedTransaction, path: &Path) {
    let mut missing_signature = transaction.clone();
    missing_signature.signatures.clear();

    let mut extra_signature = transaction.clone();
    extra_signature.signatures.push(Default::default());

    let mut missing_keys = transaction.clone();
    match &mut missing_keys.message {
        VersionedMessage::Legacy(message) => message.account_keys.clear(),
        VersionedMessage::V0(message) => message.account_keys.clear(),
        VersionedMessage::V1(message) => message.account_keys.clear(),
    }

    for (name, malformed) in [
        ("missing signature", missing_signature),
        ("extra signature", extra_signature),
        ("missing keys", missing_keys),
    ] {
        let label = format!("{}: {name}", path.display());
        assert!(malformed.sanitize().is_err(), "official sanitizer: {label}");
        check_verification(&malformed, &label, false);
    }

    let mut invalid_signature = transaction.clone();
    invalid_signature.signatures[0] = Default::default();
    assert!(invalid_signature.sanitize().is_ok());
    check_verification(
        &invalid_signature,
        &format!("{}: invalid signature", path.display()),
        false,
    );
    println!(
        "C and official SDK reject four malformed cases for {}",
        path.display()
    );
}

fn main() {
    let mut paths = fs::read_dir(vector_dir())
        .expect("read vector directory")
        .map(|entry| entry.expect("read vector entry").path())
        .filter(|path| path.extension().is_some_and(|extension| extension == "hex"))
        .collect::<Vec<_>>();
    paths.sort();
    assert!(!paths.is_empty(), "no canonical vectors found");

    for path in &paths {
        let bytes = parse_hex(path);
        let transaction: VersionedTransaction =
            wincode::deserialize(&bytes).unwrap_or_else(|error| {
                panic!("official decoder rejected {}: {error}", path.display())
            });
        transaction.sanitize().unwrap_or_else(|error| {
            panic!("official sanitizer rejected {}: {error}", path.display())
        });
        if path
            .file_name()
            .and_then(|name| name.to_str())
            .is_some_and(|name| name.starts_with("signed-"))
        {
            check_verification(&transaction, &path.display().to_string(), true);
            check_malformed_signatures(&transaction, path);
        }
        let encoded = wincode::serialize(&transaction).unwrap_or_else(|error| {
            panic!("official encoder rejected {}: {error}", path.display())
        });
        assert_eq!(
            encoded,
            bytes,
            "official round-trip changed {}",
            path.display()
        );
        assert_eq!(
            solc_orchestrator::roundtrip(&bytes).expect("C round-trip"),
            bytes,
            "C round-trip changed {}",
            path.display()
        );
        println!("official SDK accepted {}", path.display());
    }
}
