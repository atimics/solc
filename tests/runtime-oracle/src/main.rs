#![allow(deprecated)]

use mollusk_svm::{program::loader_keys, result::ProgramResult, Mollusk};
use sha2::{Digest, Sha256};
use solana_client::rpc_client::RpcClient;
use solana_instruction::Instruction as MolluskInstruction;
use solana_pubkey::Pubkey as MolluskPubkey;
use solana_sdk::{
    instruction::Instruction as AgaveInstruction,
    pubkey::Pubkey as AgavePubkey,
    signature::{read_keypair_file, Signer},
    transaction::Transaction,
};
use std::{env, fs, path::Path};

const PROGRAM_ID_BYTES: [u8; 32] = [7u8; 32];

fn usage() -> ! {
    eprintln!(concat!(
        "usage:\n",
        "  solc-runtime-oracle program-id\n",
        "  solc-runtime-oracle mollusk <artifact.so>\n",
        "  solc-runtime-oracle agave <rpc-url> <payer.json> <program-id> <genesis-hash> <artifact.so>"
    ));
    std::process::exit(2);
}

fn hash_artifact(path: &Path) -> Result<(Vec<u8>, String), String> {
    let bytes = fs::read(path).map_err(|error| format!("read {}: {error}", path.display()))?;
    let digest = Sha256::digest(&bytes);
    let hash = digest.iter().map(|byte| format!("{byte:02x}")).collect();
    Ok((bytes, hash))
}

fn run_mollusk(path: &Path) -> Result<(), String> {
    let (elf, hash) = hash_artifact(path)?;
    let program_id = MolluskPubkey::new_from_array(PROGRAM_ID_BYTES);
    let mut mollusk = Mollusk::default();
    mollusk.add_program_with_elf_and_loader(&program_id, &elf, &loader_keys::LOADER_V3);
    let instruction = MolluskInstruction::new_with_bytes(program_id, &[], vec![]);
    let result = mollusk.process_instruction(&instruction, &[]);
    if result.program_result != ProgramResult::Success {
        return Err(format!(
            "Mollusk execution failed: {:?}",
            result.program_result
        ));
    }
    println!(
        concat!(
            "{{\"engine\":\"mollusk\",\"engineVersion\":\"0.3.0\",",
            "\"agaveLine\":\"2.3\",\"programId\":\"{}\",",
            "\"artifactBytes\":{},\"artifactSha256\":\"{}\",",
            "\"success\":true,\"computeUnits\":{},\"returnDataBytes\":{}}}"
        ),
        program_id,
        elf.len(),
        hash,
        result.compute_units_consumed,
        result.return_data.len(),
    );
    Ok(())
}

fn run_agave(
    rpc_url: &str,
    payer_path: &Path,
    program_id_text: &str,
    expected_genesis_hash: &str,
    artifact_path: &Path,
) -> Result<(), String> {
    let client = RpcClient::new(rpc_url.to_owned());
    let actual_genesis_hash = client
        .get_genesis_hash()
        .map_err(|error| format!("get genesis hash: {error}"))?;
    if actual_genesis_hash.to_string() != expected_genesis_hash {
        return Err(format!(
            "genesis mismatch: expected {expected_genesis_hash}, got {actual_genesis_hash}"
        ));
    }
    let payer = read_keypair_file(payer_path)
        .map_err(|error| format!("read payer {}: {error}", payer_path.display()))?;
    let program_id = program_id_text
        .parse::<AgavePubkey>()
        .map_err(|error| format!("invalid program id: {error}"))?;
    let instruction = AgaveInstruction::new_with_bytes(program_id, &[], vec![]);
    let blockhash = client
        .get_latest_blockhash()
        .map_err(|error| format!("get latest blockhash: {error}"))?;
    let transaction = Transaction::new_signed_with_payer(
        &[instruction],
        Some(&payer.pubkey()),
        &[&payer],
        blockhash,
    );
    let simulation = client
        .simulate_transaction(&transaction)
        .map_err(|error| format!("simulate transaction: {error}"))?;
    if let Some(error) = simulation.value.err {
        return Err(format!("simulation failed: {error:?}"));
    }
    let compute_units = simulation
        .value
        .units_consumed
        .ok_or_else(|| "simulation omitted compute units".to_owned())?;
    let signature = client
        .send_and_confirm_transaction(&transaction)
        .map_err(|error| format!("send transaction: {error}"))?;
    let (artifact, hash) = hash_artifact(artifact_path)?;
    println!(
        concat!(
            "{{\"engine\":\"agave-validator\",\"engineVersion\":\"2.3.13\",",
            "\"genesisHash\":\"{}\",\"programId\":\"{}\",",
            "\"artifactBytes\":{},\"artifactSha256\":\"{}\",",
            "\"success\":true,\"computeUnits\":{},\"returnDataBytes\":0,",
            "\"signature\":\"{}\"}}"
        ),
        actual_genesis_hash,
        program_id,
        artifact.len(),
        hash,
        compute_units,
        signature,
    );
    Ok(())
}

fn main() {
    let mut arguments = env::args().skip(1);
    let command = arguments.next().unwrap_or_else(|| usage());
    let result = match command.as_str() {
        "program-id" => {
            if arguments.next().is_some() {
                usage();
            }
            println!("{}", MolluskPubkey::new_from_array(PROGRAM_ID_BYTES));
            Ok(())
        }
        "mollusk" => {
            let artifact = arguments.next().unwrap_or_else(|| usage());
            if arguments.next().is_some() {
                usage();
            }
            run_mollusk(Path::new(&artifact))
        }
        "agave" => {
            let rpc_url = arguments.next().unwrap_or_else(|| usage());
            let payer = arguments.next().unwrap_or_else(|| usage());
            let program_id = arguments.next().unwrap_or_else(|| usage());
            let genesis_hash = arguments.next().unwrap_or_else(|| usage());
            let artifact = arguments.next().unwrap_or_else(|| usage());
            if arguments.next().is_some() {
                usage();
            }
            run_agave(
                &rpc_url,
                Path::new(&payer),
                &program_id,
                &genesis_hash,
                Path::new(&artifact),
            )
        }
        _ => usage(),
    };
    if let Err(error) = result {
        eprintln!("solc-runtime-oracle: {error}");
        std::process::exit(1);
    }
}
