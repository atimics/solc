use crate::{decode_base58, decode_base64, encode_base58, encode_base64, inspect, roundtrip};
use serde::Deserialize;
use serde_json::{json, Value};

pub const REQUEST_SCHEMA: &str = "solc-process-request/v1";
pub const RESPONSE_SCHEMA: &str = "solc-process-response/v1";

#[derive(Deserialize)]
#[serde(rename_all = "camelCase", deny_unknown_fields)]
struct Request {
    schema: String,
    id: u64,
    operation: Operation,
    encoding: Encoding,
    transaction: String,
}

#[derive(Clone, Copy, Deserialize)]
#[serde(rename_all = "kebab-case")]
enum Operation {
    Inspect,
    Verify,
    Roundtrip,
    RpcSendRequest,
}

#[derive(Clone, Copy, Deserialize)]
#[serde(rename_all = "lowercase")]
enum Encoding {
    Base58,
    Base64,
}

fn response(id: Value, ok: bool, result: Value, error: Value) -> String {
    serde_json::to_string(&json!({
        "schema": RESPONSE_SCHEMA,
        "id": id,
        "ok": ok,
        "result": result,
        "error": error,
    }))
    .expect("JSON values are serializable")
}

fn request_error(id: Value, message: impl Into<String>) -> String {
    response(
        id,
        false,
        Value::Null,
        json!({
            "kind": "invalid-request",
            "code": "INVALID_REQUEST",
            "offset": Value::Null,
            "message": message.into(),
        }),
    )
}

pub fn oversized_request_response() -> String {
    request_error(Value::Null, "request line exceeds 16384 bytes")
}

pub fn invalid_utf8_response() -> String {
    request_error(Value::Null, "request line is not UTF-8")
}

fn decode_transaction(request: &Request) -> Result<Vec<u8>, crate::Error> {
    match request.encoding {
        Encoding::Base58 => decode_base58(&request.transaction),
        Encoding::Base64 => decode_base64(&request.transaction),
    }
}

fn encode_transaction(encoding: Encoding, transaction: &[u8]) -> Result<String, crate::Error> {
    match encoding {
        Encoding::Base58 => encode_base58(transaction),
        Encoding::Base64 => encode_base64(transaction),
    }
}

pub fn process_request_line(line: &str) -> String {
    let value: Value = match serde_json::from_str(line) {
        Ok(value) => value,
        Err(error) => return request_error(Value::Null, format!("invalid JSON: {error}")),
    };
    let id = value.get("id").cloned().unwrap_or(Value::Null);
    let request: Request = match serde_json::from_value(value) {
        Ok(request) => request,
        Err(error) => return request_error(id, format!("invalid request shape: {error}")),
    };
    if request.schema != REQUEST_SCHEMA {
        return request_error(
            json!(request.id),
            format!(
                "unsupported schema: expected {REQUEST_SCHEMA}, got {}",
                request.schema
            ),
        );
    }
    let transaction = match decode_transaction(&request) {
        Ok(transaction) => transaction,
        Err(error) => {
            return response(
                json!(request.id),
                false,
                Value::Null,
                json!({
                    "kind": "codec",
                    "code": error.code,
                    "offset": error.offset,
                    "message": error.message,
                }),
            );
        }
    };
    let result = match request.operation {
        Operation::Inspect => inspect(&transaction)
            .and_then(|summary| {
                serde_json::from_str::<Value>(&summary.to_json()).map_err(|_| crate::Error {
                    code: 19,
                    offset: 0,
                    message: "internal JSON serialization failure".to_owned(),
                })
            })
            .map(|summary| json!({ "summary": summary })),
        Operation::Verify => crate::verify_transaction(&transaction)
            .and_then(|verification| {
                serde_json::from_str::<Value>(&verification.to_json()).map_err(|_| crate::Error {
                    code: 19,
                    offset: 0,
                    message: "internal JSON serialization failure".to_owned(),
                })
            })
            .map(|verification| json!({ "verification": verification })),
        Operation::Roundtrip => roundtrip(&transaction).and_then(|roundtripped| {
            encode_transaction(request.encoding, &roundtripped).map(|encoded| {
                json!({
                    "encoding": match request.encoding {
                        Encoding::Base58 => "base58",
                        Encoding::Base64 => "base64",
                    },
                    "transaction": encoded,
                })
            })
        }),
        Operation::RpcSendRequest => crate::rpc::send_transaction_request(request.id, &transaction)
            .map_err(|error| match error {
                crate::rpc::RpcError::Codec(error) => error,
                other => crate::Error {
                    code: 19,
                    offset: 0,
                    message: other.to_string(),
                },
            })
            .and_then(|rpc| {
                serde_json::from_str::<Value>(&rpc).map_err(|_| crate::Error {
                    code: 19,
                    offset: 0,
                    message: "internal RPC JSON serialization failure".to_owned(),
                })
            })
            .map(|rpc| json!({ "rpcRequest": rpc })),
    };
    match result {
        Ok(result) => response(json!(request.id), true, result, Value::Null),
        Err(error) => response(
            json!(request.id),
            false,
            Value::Null,
            json!({
                "kind": "codec",
                "code": error.code,
                "offset": error.offset,
                "message": error.message,
            }),
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn vector(name: &str) -> Vec<u8> {
        let text = match name {
            "legacy" => include_str!("../../../tests/vectors/legacy.hex"),
            "signed-legacy" => include_str!("../../../tests/vectors/signed-legacy.hex"),
            _ => unreachable!(),
        };
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

    fn request(operation: &str, transaction: &[u8]) -> String {
        json!({
            "schema": REQUEST_SCHEMA,
            "id": 7,
            "operation": operation,
            "encoding": "base64",
            "transaction": encode_base64(transaction).unwrap(),
        })
        .to_string()
    }

    #[test]
    fn strict_inspect_and_roundtrip_contract() {
        let legacy = vector("legacy");
        let inspected: Value =
            serde_json::from_str(&process_request_line(&request("inspect", &legacy))).unwrap();
        assert_eq!(inspected["schema"], RESPONSE_SCHEMA);
        assert_eq!(inspected["id"], 7);
        assert_eq!(inspected["ok"], true);
        assert_eq!(inspected["result"]["summary"]["version"], "legacy");
        assert_eq!(inspected["result"]["summary"]["wireBytes"], legacy.len());

        let roundtripped: Value =
            serde_json::from_str(&process_request_line(&request("roundtrip", &legacy))).unwrap();
        assert_eq!(
            roundtripped["result"]["transaction"],
            encode_base64(&legacy).unwrap()
        );
    }

    #[test]
    fn verifies_and_builds_network_free_rpc_request() {
        let signed = vector("signed-legacy");
        let verified: Value =
            serde_json::from_str(&process_request_line(&request("verify", &signed))).unwrap();
        assert_eq!(verified["ok"], true);
        assert_eq!(verified["result"]["verification"]["verified"], true);

        let rpc: Value =
            serde_json::from_str(&process_request_line(&request("rpc-send-request", &signed)))
                .unwrap();
        assert_eq!(rpc["ok"], true);
        assert_eq!(rpc["result"]["rpcRequest"]["method"], "sendTransaction");
    }

    #[test]
    fn rejects_schema_drift_unknown_fields_and_bad_encoding() {
        let bad_schema = json!({
            "schema": "solc-process-request/v2",
            "id": 8,
            "operation": "inspect",
            "encoding": "base64",
            "transaction": "AA==",
        });
        let response: Value =
            serde_json::from_str(&process_request_line(&bad_schema.to_string())).unwrap();
        assert_eq!(response["ok"], false);
        assert_eq!(response["error"]["code"], "INVALID_REQUEST");

        let unknown = json!({
            "schema": REQUEST_SCHEMA,
            "id": 9,
            "operation": "inspect",
            "encoding": "base64",
            "transaction": "AA==",
            "surprise": true,
        });
        let response: Value =
            serde_json::from_str(&process_request_line(&unknown.to_string())).unwrap();
        assert_eq!(response["id"], 9);
        assert_eq!(response["error"]["code"], "INVALID_REQUEST");

        let bad_encoding = json!({
            "schema": REQUEST_SCHEMA,
            "id": 10,
            "operation": "inspect",
            "encoding": "base64",
            "transaction": "Zh==",
        });
        let response: Value =
            serde_json::from_str(&process_request_line(&bad_encoding.to_string())).unwrap();
        assert_eq!(response["error"]["kind"], "codec");
        assert_eq!(response["error"]["code"], 3);
    }

    #[test]
    fn trebuchet_golden_process_exchange_is_stable() {
        let fixture = include_str!("../../../tests/vectors/migrations/trebuchet-process.jsonl");
        let mut lines = fixture.lines();
        let request = lines.next().unwrap();
        let expected = lines.next().unwrap();
        assert_eq!(process_request_line(request), expected);
        assert_eq!(lines.next(), None);
    }
}
