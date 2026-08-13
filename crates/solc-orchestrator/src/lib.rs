use std::ffi::CStr;
use std::fmt;
use std::os::raw::{c_char, c_int, c_void};
use std::ptr;

pub mod process;
pub mod rpc;

// Mirrors the packet-derived public C scratch recommendations.
const MAX_DECODED_INSTRUCTIONS: usize = 410;
const MAX_DECODED_LOOKUPS: usize = 36;

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct CSlice {
    data: *const u8,
    len: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct CError {
    status: c_int,
    offset: usize,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct CInstruction {
    program_id_index: u8,
    account_indices: CSlice,
    data: CSlice,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct CLookup {
    account_key: CSlice,
    writable_indices: CSlice,
    readonly_indices: CSlice,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct CV1Config {
    mask: u32,
    priority_fee: u64,
    compute_unit_limit: u32,
    loaded_accounts_data_size_limit: u32,
    heap_size: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct CMessage {
    version: c_int,
    num_required_signatures: u8,
    num_readonly_signed_accounts: u8,
    num_readonly_unsigned_accounts: u8,
    static_account_keys: CSlice,
    lifetime_specifier: CSlice,
    instructions: *const CInstruction,
    instruction_count: usize,
    address_table_lookups: *const CLookup,
    address_table_lookup_count: usize,
    v1_config: CV1Config,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
struct CTransaction {
    signatures: CSlice,
    message: CMessage,
}

#[repr(C)]
struct CScratch {
    instructions: *mut CInstruction,
    instruction_capacity: usize,
    address_table_lookups: *mut CLookup,
    address_table_lookup_capacity: usize,
}

type CEd25519Verify = unsafe extern "C" fn(
    context: *mut c_void,
    public_key: *const u8,
    signature: *const u8,
    message: *const u8,
    message_len: usize,
) -> c_int;

#[repr(C)]
struct CCryptoProvider {
    context: *mut c_void,
    sha256: Option<
        unsafe extern "C" fn(
            context: *mut c_void,
            message: *const u8,
            message_len: usize,
            digest: *mut u8,
        ) -> c_int,
    >,
    ed25519_verify: Option<CEd25519Verify>,
}

unsafe extern "C" {
    fn solc_transaction_decode(
        input: *const u8,
        input_len: usize,
        scratch: *mut CScratch,
        out: *mut CTransaction,
        error: *mut CError,
    ) -> c_int;
    fn solc_transaction_encode(
        transaction: *const CTransaction,
        output: *mut u8,
        output_capacity: usize,
        output_len: *mut usize,
        error: *mut CError,
    ) -> c_int;
    fn solc_transaction_message_encode(
        transaction: *const CTransaction,
        output: *mut u8,
        output_capacity: usize,
        output_len: *mut usize,
        error: *mut CError,
    ) -> c_int;
    fn solc_transaction_verify_signatures(
        transaction: *const CTransaction,
        provider: *const CCryptoProvider,
        message_buffer: *mut u8,
        message_capacity: usize,
        failed_signature: *mut usize,
        error: *mut CError,
    ) -> c_int;
    fn solc_sha256_builtin(
        context: *mut c_void,
        message: *const u8,
        message_len: usize,
        digest: *mut u8,
    ) -> c_int;
    fn solc_base58_encode(
        input: *const u8,
        input_len: usize,
        output: *mut c_char,
        output_capacity: usize,
        output_len: *mut usize,
    ) -> c_int;
    fn solc_base58_decode(
        input: *const c_char,
        input_len: usize,
        output: *mut u8,
        output_capacity: usize,
        output_len: *mut usize,
    ) -> c_int;
    fn solc_base64_encode(
        input: *const u8,
        input_len: usize,
        output: *mut c_char,
        output_capacity: usize,
        output_len: *mut usize,
    ) -> c_int;
    fn solc_base64_decode(
        input: *const c_char,
        input_len: usize,
        output: *mut u8,
        output_capacity: usize,
        output_len: *mut usize,
    ) -> c_int;
    fn solc_status_string(status: c_int) -> *const c_char;
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error {
    pub code: i32,
    pub offset: usize,
    pub message: String,
}

impl fmt::Display for Error {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            formatter,
            "{} at byte {} (code {})",
            self.message, self.offset, self.code
        )
    }
}

impl std::error::Error for Error {}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct V1Config {
    pub mask: u32,
    pub priority_fee: Option<u64>,
    pub compute_unit_limit: Option<u32>,
    pub loaded_accounts_data_size_limit: Option<u32>,
    pub heap_size: Option<u32>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TransactionSummary {
    pub version: &'static str,
    pub wire_bytes: usize,
    pub signatures: usize,
    pub required_signatures: u8,
    pub readonly_signed_accounts: u8,
    pub readonly_unsigned_accounts: u8,
    pub static_accounts: usize,
    pub loaded_writable_accounts: usize,
    pub loaded_readonly_accounts: usize,
    pub instructions: usize,
    pub address_table_lookups: usize,
    pub v1_config: Option<V1Config>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VerificationSummary {
    pub signatures: usize,
    pub message_bytes: usize,
    pub message_sha256: [u8; 32],
}

impl VerificationSummary {
    pub fn to_json(&self) -> String {
        format!(
            "{{\"verified\":true,\"signatures\":{},\"messageBytes\":{},\"messageSha256\":\"{}\"}}",
            self.signatures,
            self.message_bytes,
            encode_hex(&self.message_sha256),
        )
    }
}

impl TransactionSummary {
    pub fn to_json(&self) -> String {
        let config = match &self.v1_config {
            Some(config) => format!(
                concat!(
                    "{{\"mask\":{},\"priorityFee\":{},\"computeUnitLimit\":{},",
                    "\"loadedAccountsDataSizeLimit\":{},\"heapSize\":{}}}"
                ),
                config.mask,
                json_option(config.priority_fee),
                json_option(config.compute_unit_limit),
                json_option(config.loaded_accounts_data_size_limit),
                json_option(config.heap_size),
            ),
            None => "null".to_owned(),
        };
        format!(
            concat!(
                "{{\"version\":\"{}\",\"wireBytes\":{},\"signatures\":{},",
                "\"requiredSignatures\":{},\"readonlySignedAccounts\":{},",
                "\"readonlyUnsignedAccounts\":{},\"staticAccounts\":{},",
                "\"loadedWritableAccounts\":{},\"loadedReadonlyAccounts\":{},",
                "\"instructions\":{},\"addressTableLookups\":{},\"v1Config\":{}}}"
            ),
            self.version,
            self.wire_bytes,
            self.signatures,
            self.required_signatures,
            self.readonly_signed_accounts,
            self.readonly_unsigned_accounts,
            self.static_accounts,
            self.loaded_writable_accounts,
            self.loaded_readonly_accounts,
            self.instructions,
            self.address_table_lookups,
            config,
        )
    }
}

fn json_option<T: fmt::Display>(value: Option<T>) -> String {
    value.map_or_else(|| "null".to_owned(), |value| value.to_string())
}

fn message_for(code: c_int) -> String {
    // SAFETY: the C library returns a static NUL-terminated string for every code.
    unsafe {
        let pointer = solc_status_string(code);
        if pointer.is_null() {
            return "unknown C codec error".to_owned();
        }
        CStr::from_ptr(pointer).to_string_lossy().into_owned()
    }
}

fn error_from(raw: CError, fallback: c_int) -> Error {
    let code = if raw.status == 0 {
        fallback
    } else {
        raw.status
    };
    Error {
        code,
        offset: raw.offset,
        message: message_for(code),
    }
}

fn status_error(status: c_int) -> Error {
    error_from(CError { status, offset: 0 }, status)
}

struct Decoded {
    transaction: CTransaction,
    _instructions: Box<[CInstruction; MAX_DECODED_INSTRUCTIONS]>,
    _lookups: Box<[CLookup; MAX_DECODED_LOOKUPS]>,
}

fn decode(bytes: &[u8]) -> Result<Decoded, Error> {
    let mut instructions = Box::new([CInstruction::default(); MAX_DECODED_INSTRUCTIONS]);
    let mut lookups = Box::new([CLookup::default(); MAX_DECODED_LOOKUPS]);
    let mut scratch = CScratch {
        instructions: instructions.as_mut_ptr(),
        instruction_capacity: instructions.len(),
        address_table_lookups: lookups.as_mut_ptr(),
        address_table_lookup_capacity: lookups.len(),
    };
    let mut transaction = CTransaction::default();
    let mut error = CError::default();
    // SAFETY: all pointers are valid for their stated lengths for this call. The
    // returned transaction only borrows `bytes`, `instructions`, and `lookups`.
    let status = unsafe {
        solc_transaction_decode(
            bytes.as_ptr(),
            bytes.len(),
            &mut scratch,
            &mut transaction,
            &mut error,
        )
    };
    if status != 0 {
        return Err(error_from(error, status));
    }
    Ok(Decoded {
        transaction,
        _instructions: instructions,
        _lookups: lookups,
    })
}

pub fn inspect(bytes: &[u8]) -> Result<TransactionSummary, Error> {
    let decoded = decode(bytes)?;
    let message = &decoded.transaction.message;
    let lookups = if message.address_table_lookup_count == 0 {
        &[][..]
    } else {
        // SAFETY: successful decoding populated this many entries in owned scratch.
        unsafe {
            std::slice::from_raw_parts(
                message.address_table_lookups,
                message.address_table_lookup_count,
            )
        }
    };
    let loaded_writable_accounts = lookups
        .iter()
        .map(|lookup| lookup.writable_indices.len)
        .sum();
    let loaded_readonly_accounts = lookups
        .iter()
        .map(|lookup| lookup.readonly_indices.len)
        .sum();
    let version = match message.version {
        -1 => "legacy",
        0 => "v0",
        1 => "v1",
        _ => "unknown",
    };
    let config = &message.v1_config;
    let v1_config = (message.version == 1).then(|| V1Config {
        mask: config.mask,
        priority_fee: ((config.mask & 0x3) == 0x3).then_some(config.priority_fee),
        compute_unit_limit: ((config.mask & 0x4) != 0).then_some(config.compute_unit_limit),
        loaded_accounts_data_size_limit: ((config.mask & 0x8) != 0)
            .then_some(config.loaded_accounts_data_size_limit),
        heap_size: ((config.mask & 0x10) != 0).then_some(config.heap_size),
    });
    Ok(TransactionSummary {
        version,
        wire_bytes: bytes.len(),
        signatures: decoded.transaction.signatures.len / 64,
        required_signatures: message.num_required_signatures,
        readonly_signed_accounts: message.num_readonly_signed_accounts,
        readonly_unsigned_accounts: message.num_readonly_unsigned_accounts,
        static_accounts: message.static_account_keys.len / 32,
        loaded_writable_accounts,
        loaded_readonly_accounts,
        instructions: message.instruction_count,
        address_table_lookups: message.address_table_lookup_count,
        v1_config,
    })
}

pub fn roundtrip(bytes: &[u8]) -> Result<Vec<u8>, Error> {
    let decoded = decode(bytes)?;
    let mut required = 0usize;
    let mut error = CError::default();
    // SAFETY: decoded owns the scratch arrays and borrows `bytes` for this scope.
    let status = unsafe {
        solc_transaction_encode(
            &decoded.transaction,
            ptr::null_mut(),
            0,
            &mut required,
            &mut error,
        )
    };
    if status != 0 {
        return Err(error_from(error, status));
    }
    let mut output = vec![0u8; required];
    // SAFETY: `output` is writable for `required` bytes and the model remains alive.
    let status = unsafe {
        solc_transaction_encode(
            &decoded.transaction,
            output.as_mut_ptr(),
            output.len(),
            &mut required,
            &mut error,
        )
    };
    if status != 0 {
        return Err(error_from(error, status));
    }
    output.truncate(required);
    Ok(output)
}

fn signing_message_decoded(decoded: &Decoded) -> Result<Vec<u8>, Error> {
    let mut required = 0usize;
    let mut error = CError::default();
    // SAFETY: the decoded transaction and its scratch storage remain alive.
    let status = unsafe {
        solc_transaction_message_encode(
            &decoded.transaction,
            ptr::null_mut(),
            0,
            &mut required,
            &mut error,
        )
    };
    if status != 0 {
        return Err(error_from(error, status));
    }
    let mut output = vec![0u8; required];
    // SAFETY: output has the exact requested capacity and decoded remains alive.
    let status = unsafe {
        solc_transaction_message_encode(
            &decoded.transaction,
            output.as_mut_ptr(),
            output.len(),
            &mut required,
            &mut error,
        )
    };
    if status != 0 {
        return Err(error_from(error, status));
    }
    output.truncate(required);
    Ok(output)
}

pub fn signing_message(bytes: &[u8]) -> Result<Vec<u8>, Error> {
    let decoded = decode(bytes)?;
    signing_message_decoded(&decoded)
}

unsafe extern "C" fn verify_ed25519(
    _context: *mut c_void,
    public_key: *const u8,
    signature: *const u8,
    message: *const u8,
    message_len: usize,
) -> c_int {
    if public_key.is_null() || signature.is_null() || (message.is_null() && message_len != 0) {
        return 19;
    }
    // SAFETY: C promises fixed-width key/signature pointers and message_len bytes.
    let public_key = unsafe { std::slice::from_raw_parts(public_key, 32) };
    // SAFETY: same fixed-width FFI contract as above.
    let signature = unsafe { std::slice::from_raw_parts(signature, 64) };
    let message = if message_len == 0 {
        &[]
    } else {
        // SAFETY: non-nullness was checked and C promises message_len bytes.
        unsafe { std::slice::from_raw_parts(message, message_len) }
    };
    let Ok(public_key): Result<&[u8; 32], _> = public_key.try_into() else {
        return 19;
    };
    let Ok(signature): Result<&[u8; 64], _> = signature.try_into() else {
        return 19;
    };
    let Ok(verifying_key) = ed25519_dalek::VerifyingKey::from_bytes(public_key) else {
        return 18;
    };
    let signature = ed25519_dalek::Signature::from_bytes(signature);
    if verifying_key.verify_strict(message, &signature).is_ok() {
        0
    } else {
        18
    }
}

pub fn verify_transaction(bytes: &[u8]) -> Result<VerificationSummary, Error> {
    let decoded = decode(bytes)?;
    let mut message = signing_message_decoded(&decoded)?;
    let provider = CCryptoProvider {
        context: ptr::null_mut(),
        sha256: None,
        ed25519_verify: Some(verify_ed25519),
    };
    let mut failed_signature = usize::MAX;
    let mut error = CError::default();
    // SAFETY: provider callbacks obey the C ABI and all borrowed storage remains alive.
    let status = unsafe {
        solc_transaction_verify_signatures(
            &decoded.transaction,
            &provider,
            message.as_mut_ptr(),
            message.len(),
            &mut failed_signature,
            &mut error,
        )
    };
    if status != 0 {
        return Err(error_from(error, status));
    }
    let mut digest = [0u8; 32];
    // SAFETY: message and digest are valid for their stated lengths.
    let status = unsafe {
        solc_sha256_builtin(
            ptr::null_mut(),
            message.as_ptr(),
            message.len(),
            digest.as_mut_ptr(),
        )
    };
    if status != 0 {
        return Err(status_error(status));
    }
    Ok(VerificationSummary {
        signatures: decoded.transaction.signatures.len / 64,
        message_bytes: message.len(),
        message_sha256: digest,
    })
}

pub fn encode_base58(bytes: &[u8]) -> Result<String, Error> {
    let capacity = bytes
        .len()
        .checked_mul(138)
        .and_then(|value| value.checked_div(100))
        .and_then(|value| value.checked_add(2))
        .ok_or_else(|| status_error(4))?;
    encode_text(bytes, capacity, solc_base58_encode)
}

pub fn encode_base64(bytes: &[u8]) -> Result<String, Error> {
    let capacity = bytes
        .len()
        .checked_add(2)
        .and_then(|value| value.checked_div(3))
        .and_then(|value| value.checked_mul(4))
        .ok_or_else(|| status_error(4))?;
    encode_text(bytes, capacity, solc_base64_encode)
}

type CTextEncoder = unsafe extern "C" fn(*const u8, usize, *mut c_char, usize, *mut usize) -> c_int;

fn encode_text(bytes: &[u8], capacity: usize, encoder: CTextEncoder) -> Result<String, Error> {
    let mut output = vec![0u8; capacity];
    let mut output_len = 0usize;
    // SAFETY: input and output pointers are valid for the supplied lengths.
    let status = unsafe {
        encoder(
            bytes.as_ptr(),
            bytes.len(),
            output.as_mut_ptr().cast(),
            output.len(),
            &mut output_len,
        )
    };
    if status != 0 {
        return Err(status_error(status));
    }
    output.truncate(output_len);
    String::from_utf8(output).map_err(|_| status_error(16))
}

pub fn decode_base58(text: &str) -> Result<Vec<u8>, Error> {
    decode_text(text, solc_base58_decode)
}

pub fn decode_base64(text: &str) -> Result<Vec<u8>, Error> {
    decode_text(text, solc_base64_decode)
}

type CTextDecoder = unsafe extern "C" fn(*const c_char, usize, *mut u8, usize, *mut usize) -> c_int;

fn decode_text(text: &str, decoder: CTextDecoder) -> Result<Vec<u8>, Error> {
    let mut output = vec![0u8; text.len()];
    let mut output_len = 0usize;
    // SAFETY: input and output pointers are valid for the supplied lengths.
    let status = unsafe {
        decoder(
            text.as_ptr().cast(),
            text.len(),
            output.as_mut_ptr(),
            output.len(),
            &mut output_len,
        )
    };
    if status != 0 {
        return Err(status_error(status));
    }
    output.truncate(output_len);
    Ok(output)
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

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::{Signer, SigningKey};

    fn decode_hex(input: &str) -> Vec<u8> {
        let compact: String = input
            .chars()
            .filter(|character| !character.is_whitespace())
            .collect();
        assert_eq!(compact.len() % 2, 0);
        compact
            .as_bytes()
            .chunks_exact(2)
            .map(|pair| {
                let text = std::str::from_utf8(pair).unwrap();
                u8::from_str_radix(text, 16).unwrap()
            })
            .collect()
    }

    #[test]
    fn rejects_unknown_discriminator() {
        let error = inspect(&[0x82]).unwrap_err();
        assert_eq!(error.code, 5);
        assert_eq!(error.offset, 0);
    }

    #[test]
    fn hex_helper_is_sound() {
        assert_eq!(decode_hex("00 ff 81"), [0x00, 0xff, 0x81]);
    }

    fn vector(text: &str) -> Vec<u8> {
        let uncommented = text
            .lines()
            .map(|line| line.split('#').next().unwrap_or_default())
            .collect::<Vec<_>>()
            .join("");
        decode_hex(&uncommented)
    }

    fn signed_vector(mut bytes: Vec<u8>) -> Vec<u8> {
        let signing_key = SigningKey::from_bytes(&[7u8; 32]);
        let decoded = decode(&bytes).unwrap();
        let base = bytes.as_ptr() as usize;
        let public_key_offset =
            decoded.transaction.message.static_account_keys.data as usize - base;
        let signature_offset = decoded.transaction.signatures.data as usize - base;
        drop(decoded);
        bytes[public_key_offset..public_key_offset + 32]
            .copy_from_slice(signing_key.verifying_key().as_bytes());

        let decoded = decode(&bytes).unwrap();
        let message = signing_message_decoded(&decoded).unwrap();
        drop(decoded);
        let signature = signing_key.sign(&message);
        bytes[signature_offset..signature_offset + 64].copy_from_slice(&signature.to_bytes());
        bytes
    }

    #[test]
    fn verifies_real_ed25519_for_every_transaction_layout() {
        let vectors = [
            vector(include_str!("../../../tests/vectors/legacy.hex")),
            vector(include_str!("../../../tests/vectors/v0.hex")),
            vector(include_str!("../../../tests/vectors/v1.hex")),
        ];
        for (version, unsigned) in vectors.into_iter().enumerate() {
            let signed = signed_vector(unsigned);
            let message = signing_message(&signed).unwrap();
            if version < 2 {
                assert_eq!(message, signed[65..]);
            } else {
                assert_eq!(message, signed[..signed.len() - 64]);
                assert_eq!(message[0], 0x81);
            }
            let summary = verify_transaction(&signed).unwrap();
            assert_eq!(summary.signatures, 1);
            assert_eq!(summary.message_bytes, message.len());

            let mut corrupted = signed;
            let decoded = decode(&corrupted).unwrap();
            let base = corrupted.as_ptr() as usize;
            let signature_offset = decoded.transaction.signatures.data as usize - base;
            drop(decoded);
            corrupted[signature_offset] ^= 1;
            let error = verify_transaction(&corrupted).unwrap_err();
            assert_eq!(error.code, 18);
            assert_eq!(error.offset, 0);
        }
    }

    #[test]
    fn c_text_codecs_are_exposed_without_rust_reimplementation() {
        let bytes = b"\0Solana wire\xff";
        let base58 = encode_base58(bytes).unwrap();
        assert_eq!(decode_base58(&base58).unwrap(), bytes);
        let base64 = encode_base64(bytes).unwrap();
        assert_eq!(decode_base64(&base64).unwrap(), bytes);
        assert_eq!(decode_base64("Zh==").unwrap_err().code, 3);
    }
}
