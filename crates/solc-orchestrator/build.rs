use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn run(tool: &str, args: &[&Path]) {
    let printable = args
        .iter()
        .map(|arg| arg.display().to_string())
        .collect::<Vec<_>>()
        .join(" ");
    let status = Command::new(tool)
        .args(args)
        .status()
        .unwrap_or_else(|error| panic!("failed to run {tool} {printable}: {error}"));
    assert!(status.success(), "{tool} {printable} failed with {status}");
}

fn main() {
    let manifest = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("manifest directory"));
    let root = manifest.join("../..");
    let out = PathBuf::from(env::var_os("OUT_DIR").expect("build output directory"));
    let sources = [
        root.join("src/builder.c"),
        root.join("src/crypto.c"),
        root.join("src/encoding.c"),
        root.join("src/programs.c"),
        root.join("src/rati_bridge.c"),
        root.join("src/sbf.c"),
        root.join("src/wire.c"),
    ];
    let include = root.join("include");
    let objects = [
        out.join("builder.o"),
        out.join("crypto.o"),
        out.join("encoding.o"),
        out.join("programs.o"),
        out.join("rati_bridge.o"),
        out.join("sbf.o"),
        out.join("wire.o"),
    ];
    let archive = out.join("libsolc_wire.a");
    let cc = env::var("CC").unwrap_or_else(|_| "cc".to_owned());
    let ar = env::var("AR").unwrap_or_else(|_| "ar".to_owned());

    let include_arg = PathBuf::from(format!("-I{}", include.display()));
    let c_std = Path::new("-std=c11");
    let optimize = Path::new("-O2");
    let compile = Path::new("-c");
    let output = Path::new("-o");
    for (source, object) in sources.iter().zip(objects.iter()) {
        run(
            &cc,
            &[
                c_std,
                optimize,
                include_arg.as_path(),
                compile,
                source,
                output,
                object,
            ],
        );
    }
    run(
        &ar,
        &[
            Path::new("crus"),
            &archive,
            &objects[0],
            &objects[1],
            &objects[2],
            &objects[3],
            &objects[4],
            &objects[5],
            &objects[6],
        ],
    );

    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=solc_wire");
    for source in &sources {
        println!("cargo:rerun-if-changed={}", source.display());
    }
    println!(
        "cargo:rerun-if-changed={}",
        root.join("include/solc/builder.h").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("include/solc/wire.h").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("include/solc/encoding.h").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("include/solc/crypto.h").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("include/solc/sbf.h").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("include/solc/programs.h").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("include/solc/rati_bridge.h").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("schemas/program_instructions.def").display()
    );
}
