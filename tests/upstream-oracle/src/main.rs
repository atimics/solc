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
            assert!(
                transaction
                    .verify_with_results()
                    .iter()
                    .all(|result| *result),
                "official signature verifier rejected {}",
                path.display()
            );
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
        println!("official SDK accepted {}", path.display());
    }
}
