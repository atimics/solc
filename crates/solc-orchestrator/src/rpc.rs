use crate::{decode_base58, decode_base64, encode_base58, encode_base64, inspect, Error};
use serde_json::{json, Value};
use std::fmt;

#[derive(Debug)]
pub enum RpcError {
    Codec(Error),
    Json(serde_json::Error),
    InvalidShape(&'static str),
    InvalidSignature,
    Remote { code: i64, message: String },
}

impl fmt::Display for RpcError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Codec(error) => write!(formatter, "{error}"),
            Self::Json(error) => write!(formatter, "invalid RPC JSON: {error}"),
            Self::InvalidShape(message) => write!(formatter, "invalid RPC response: {message}"),
            Self::InvalidSignature => {
                formatter.write_str("invalid canonical transaction signature")
            }
            Self::Remote { code, message } => write!(formatter, "RPC error {code}: {message}"),
        }
    }
}

impl std::error::Error for RpcError {}

impl From<Error> for RpcError {
    fn from(value: Error) -> Self {
        Self::Codec(value)
    }
}

impl From<serde_json::Error> for RpcError {
    fn from(value: serde_json::Error) -> Self {
        Self::Json(value)
    }
}

fn validate_signature(signature: &str) -> Result<(), RpcError> {
    let decoded = decode_base58(signature)?;
    if decoded.len() != 64 || encode_base58(&decoded)? != signature {
        return Err(RpcError::InvalidSignature);
    }
    Ok(())
}

pub fn get_transaction_request(id: u64, signature: &str) -> Result<String, RpcError> {
    validate_signature(signature)?;
    Ok(serde_json::to_string(&json!({
        "jsonrpc": "2.0",
        "id": id,
        "method": "getTransaction",
        "params": [
            signature,
            {
                "commitment": "confirmed",
                "encoding": "base64",
                "maxSupportedTransactionVersion": 1
            }
        ]
    }))?)
}

pub fn send_transaction_request(id: u64, transaction: &[u8]) -> Result<String, RpcError> {
    inspect(transaction)?;
    let encoded = encode_base64(transaction)?;
    Ok(serde_json::to_string(&json!({
        "jsonrpc": "2.0",
        "id": id,
        "method": "sendTransaction",
        "params": [
            encoded,
            {
                "encoding": "base64"
            }
        ]
    }))?)
}

fn remote_error(value: &Value) -> Option<RpcError> {
    let error = value.get("error")?;
    Some(RpcError::Remote {
        code: error.get("code").and_then(Value::as_i64).unwrap_or(0),
        message: error
            .get("message")
            .and_then(Value::as_str)
            .unwrap_or("unknown remote error")
            .to_owned(),
    })
}

pub fn decode_transaction_response(response: &str) -> Result<Option<Vec<u8>>, RpcError> {
    let value: Value = serde_json::from_str(response)?;
    if let Some(error) = remote_error(&value) {
        return Err(error);
    }
    let Some(result) = value.get("result") else {
        return Err(RpcError::InvalidShape("missing result"));
    };
    if result.is_null() {
        return Ok(None);
    }
    let transaction = result
        .get("transaction")
        .ok_or(RpcError::InvalidShape("missing transaction"))?;
    let fields = transaction.as_array().ok_or(RpcError::InvalidShape(
        "transaction is not an encoded tuple",
    ))?;
    if fields.len() != 2 || fields[1].as_str() != Some("base64") {
        return Err(RpcError::InvalidShape(
            "transaction tuple is not [data, \"base64\"]",
        ));
    }
    let encoded = fields[0]
        .as_str()
        .ok_or(RpcError::InvalidShape("transaction data is not text"))?;
    let decoded = decode_base64(encoded)?;
    inspect(&decoded)?;
    Ok(Some(decoded))
}

pub fn decode_signature_response(response: &str) -> Result<String, RpcError> {
    let value: Value = serde_json::from_str(response)?;
    if let Some(error) = remote_error(&value) {
        return Err(error);
    }
    let signature = value
        .get("result")
        .and_then(Value::as_str)
        .ok_or(RpcError::InvalidShape("result is not a signature"))?;
    validate_signature(signature)?;
    Ok(signature.to_owned())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn legacy_vector() -> Vec<u8> {
        let text = include_str!("../../../tests/vectors/legacy.hex");
        let compact: String = text
            .lines()
            .flat_map(|line| line.split('#').next().unwrap_or_default().chars())
            .filter(|value| !value.is_whitespace())
            .collect();
        compact
            .as_bytes()
            .chunks_exact(2)
            .map(|pair| u8::from_str_radix(std::str::from_utf8(pair).unwrap(), 16).unwrap())
            .collect()
    }

    #[test]
    fn request_and_response_preserve_wire_bytes() {
        let transaction = legacy_vector();
        let request = send_transaction_request(7, &transaction).unwrap();
        let request: Value = serde_json::from_str(&request).unwrap();
        assert_eq!(request["method"], "sendTransaction");
        assert_eq!(request["params"][1]["encoding"], "base64");
        let response = json!({
            "jsonrpc": "2.0",
            "id": 7,
            "result": {
                "transaction": [encode_base64(&transaction).unwrap(), "base64"]
            }
        });
        assert_eq!(
            decode_transaction_response(&response.to_string()).unwrap(),
            Some(transaction)
        );
    }

    #[test]
    fn rejects_remote_errors_and_noncanonical_payloads() {
        let remote = r#"{"jsonrpc":"2.0","error":{"code":-32000,"message":"nope"}}"#;
        assert!(matches!(
            decode_transaction_response(remote),
            Err(RpcError::Remote { code: -32000, .. })
        ));
        let bad = r#"{"result":{"transaction":["Zh==","base64"]}}"#;
        assert!(matches!(
            decode_transaction_response(bad),
            Err(RpcError::Codec(Error { code: 3, .. }))
        ));
    }

    #[test]
    fn signature_requests_require_exact_canonical_signatures() {
        let signature = encode_base58(&[7u8; 64]).unwrap();
        let request = get_transaction_request(3, &signature).unwrap();
        let value: Value = serde_json::from_str(&request).unwrap();
        assert_eq!(value["params"][0], signature);
        assert_eq!(value["params"][1]["maxSupportedTransactionVersion"], 1);
        assert!(matches!(
            get_transaction_request(3, "111"),
            Err(RpcError::InvalidSignature)
        ));
    }
}
