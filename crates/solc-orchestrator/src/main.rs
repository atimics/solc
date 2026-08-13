use solc_orchestrator::{
    decode_base58, decode_base64, encode_base58, encode_base64, inspect, roundtrip,
    signing_message, verify_transaction,
};
use std::env;
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};

fn usage() -> ! {
    eprintln!(concat!(
        "usage:\n",
        "  solc-wire inspect <hex|@file>\n",
        "  solc-wire roundtrip <hex|@file>\n",
        "  solc-wire message <hex|@file>\n",
        "  solc-wire verify <hex|@file>\n",
        "  solc-wire encode <base58|base64> <hex|@file>\n",
        "  solc-wire decode <base58|base64> <text|@file>\n",
        "  solc-wire rpc-send-request <hex|@file> [id]\n",
        "  solc-wire rpc-get-request <signature> [id]\n",
        "  solc-wire check-vectors [directory]"
    ));
    std::process::exit(2);
}

fn hex_nibble(byte: u8) -> Option<u8> {
    match byte {
        b'0'..=b'9' => Some(byte - b'0'),
        b'a'..=b'f' => Some(byte - b'a' + 10),
        b'A'..=b'F' => Some(byte - b'A' + 10),
        _ => None,
    }
}

fn parse_hex(text: &str) -> Result<Vec<u8>, String> {
    let mut digits = Vec::new();
    for line in text.lines() {
        let content = line.split('#').next().unwrap_or_default();
        digits.extend(content.bytes().filter(|byte| !byte.is_ascii_whitespace()));
    }
    if digits.len() % 2 != 0 {
        return Err("hex input contains an odd number of digits".to_owned());
    }
    let mut output = Vec::with_capacity(digits.len() / 2);
    for (index, pair) in digits.chunks_exact(2).enumerate() {
        let high = hex_nibble(pair[0])
            .ok_or_else(|| format!("invalid hex digit at character {}", index * 2))?;
        let low = hex_nibble(pair[1])
            .ok_or_else(|| format!("invalid hex digit at character {}", index * 2 + 1))?;
        output.push((high << 4) | low);
    }
    Ok(output)
}

fn read_argument(argument: &str) -> Result<Vec<u8>, String> {
    if argument == "@-" {
        let mut bytes = Vec::new();
        io::stdin()
            .read_to_end(&mut bytes)
            .map_err(|error| format!("read stdin: {error}"))?;
        return Ok(bytes);
    }
    if let Some(path) = argument.strip_prefix('@') {
        let bytes = fs::read(path).map_err(|error| format!("read {path}: {error}"))?;
        if Path::new(path)
            .extension()
            .is_some_and(|extension| extension == "hex")
        {
            let text = std::str::from_utf8(&bytes)
                .map_err(|error| format!("{path} is not UTF-8 hex: {error}"))?;
            return parse_hex(text);
        }
        return Ok(bytes);
    }
    parse_hex(argument)
}

fn read_text_argument(argument: &str) -> Result<String, String> {
    if argument == "@-" {
        let mut text = String::new();
        io::stdin()
            .read_to_string(&mut text)
            .map_err(|error| format!("read stdin: {error}"))?;
        return Ok(text.trim().to_owned());
    }
    if let Some(path) = argument.strip_prefix('@') {
        return fs::read_to_string(path)
            .map(|text| text.trim().to_owned())
            .map_err(|error| format!("read {path}: {error}"));
    }
    Ok(argument.to_owned())
}

fn parse_id(value: Option<String>) -> Result<u64, String> {
    value.map_or(Ok(1), |value| {
        value
            .parse::<u64>()
            .map_err(|error| format!("invalid RPC id: {error}"))
    })
}

fn encode_hex(bytes: &[u8]) -> String {
    const DIGITS: &[u8; 16] = b"0123456789abcdef";
    let mut output = String::with_capacity(bytes.len() * 2);
    for &byte in bytes {
        output.push(DIGITS[(byte >> 4) as usize] as char);
        output.push(DIGITS[(byte & 0x0f) as usize] as char);
    }
    output
}

fn default_vector_dir() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../../tests/vectors")
}

fn check_vectors(directory: &Path) -> Result<(), String> {
    let mut paths = fs::read_dir(directory)
        .map_err(|error| format!("read {}: {error}", directory.display()))?
        .map(|entry| entry.map(|entry| entry.path()))
        .collect::<Result<Vec<_>, _>>()
        .map_err(|error| format!("read {}: {error}", directory.display()))?;
    paths.retain(|path| path.extension().is_some_and(|extension| extension == "hex"));
    paths.sort();
    if paths.is_empty() {
        return Err(format!("no .hex vectors in {}", directory.display()));
    }
    for path in &paths {
        let argument = format!("@{}", path.display());
        let bytes = read_argument(&argument)?;
        let summary = inspect(&bytes).map_err(|error| format!("{}: {error}", path.display()))?;
        let encoded = roundtrip(&bytes).map_err(|error| format!("{}: {error}", path.display()))?;
        if encoded != bytes {
            return Err(format!(
                "{} did not round-trip byte-for-byte",
                path.display()
            ));
        }
        println!("{} {}", path.display(), summary.to_json());
    }
    println!("checked {} canonical vectors", paths.len());
    Ok(())
}

fn main() {
    let mut arguments = env::args().skip(1);
    let command = arguments.next().unwrap_or_else(|| usage());
    let result = match command.as_str() {
        "inspect" => {
            let input = arguments.next().unwrap_or_else(|| usage());
            if arguments.next().is_some() {
                usage();
            }
            read_argument(&input).and_then(|bytes| {
                inspect(&bytes)
                    .map(|summary| println!("{}", summary.to_json()))
                    .map_err(|error| error.to_string())
            })
        }
        "roundtrip" => {
            let input = arguments.next().unwrap_or_else(|| usage());
            if arguments.next().is_some() {
                usage();
            }
            read_argument(&input).and_then(|bytes| {
                roundtrip(&bytes)
                    .map(|encoded| println!("{}", encode_hex(&encoded)))
                    .map_err(|error| error.to_string())
            })
        }
        "message" => {
            let input = arguments.next().unwrap_or_else(|| usage());
            if arguments.next().is_some() {
                usage();
            }
            read_argument(&input).and_then(|bytes| {
                signing_message(&bytes)
                    .map(|message| println!("{}", encode_hex(&message)))
                    .map_err(|error| error.to_string())
            })
        }
        "verify" => {
            let input = arguments.next().unwrap_or_else(|| usage());
            if arguments.next().is_some() {
                usage();
            }
            read_argument(&input).and_then(|bytes| {
                verify_transaction(&bytes)
                    .map(|summary| println!("{}", summary.to_json()))
                    .map_err(|error| error.to_string())
            })
        }
        "encode" => {
            let encoding = arguments.next().unwrap_or_else(|| usage());
            let input = arguments.next().unwrap_or_else(|| usage());
            if arguments.next().is_some() {
                usage();
            }
            read_argument(&input).and_then(|bytes| {
                let encoded = match encoding.as_str() {
                    "base58" => encode_base58(&bytes),
                    "base64" => encode_base64(&bytes),
                    _ => usage(),
                };
                encoded
                    .map(|encoded| println!("{encoded}"))
                    .map_err(|error| error.to_string())
            })
        }
        "decode" => {
            let encoding = arguments.next().unwrap_or_else(|| usage());
            let input = arguments.next().unwrap_or_else(|| usage());
            if arguments.next().is_some() {
                usage();
            }
            read_text_argument(&input).and_then(|text| {
                let decoded = match encoding.as_str() {
                    "base58" => decode_base58(&text),
                    "base64" => decode_base64(&text),
                    _ => usage(),
                };
                decoded
                    .map(|decoded| println!("{}", encode_hex(&decoded)))
                    .map_err(|error| error.to_string())
            })
        }
        "rpc-send-request" => {
            let input = arguments.next().unwrap_or_else(|| usage());
            let id = parse_id(arguments.next());
            if arguments.next().is_some() {
                usage();
            }
            id.and_then(|id| {
                read_argument(&input).and_then(|bytes| {
                    solc_orchestrator::rpc::send_transaction_request(id, &bytes)
                        .map(|request| println!("{request}"))
                        .map_err(|error| error.to_string())
                })
            })
        }
        "rpc-get-request" => {
            let signature = arguments.next().unwrap_or_else(|| usage());
            let id = parse_id(arguments.next());
            if arguments.next().is_some() {
                usage();
            }
            id.and_then(|id| {
                solc_orchestrator::rpc::get_transaction_request(id, &signature)
                    .map(|request| println!("{request}"))
                    .map_err(|error| error.to_string())
            })
        }
        "check-vectors" => {
            let directory = arguments
                .next()
                .map(PathBuf::from)
                .unwrap_or_else(default_vector_dir);
            if arguments.next().is_some() {
                usage();
            }
            check_vectors(&directory)
        }
        _ => usage(),
    };
    if let Err(error) = result {
        eprintln!("solc-wire: {error}");
        std::process::exit(1);
    }
}
