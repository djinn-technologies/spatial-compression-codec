// studio/agent/build.rs
//
// Build-time codegen:
//   1. bindgen libscc.h -> Rust FFI declarations.
//   2. prost-build src/proto/control.proto -> Rust types.
//   3. Tell cargo where libscc.{so,dll,dylib} lives at link time.
//
// Both bindgen and prost-build need external tooling (libclang for
// bindgen, protoc for prost-build). CI runners provision those. Local
// dev: see README.

use std::env;
use std::path::PathBuf;

fn main() {
    let manifest_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    // The agent lives at studio/agent/; the codec lives at the repo root.
    let repo_root = manifest_dir.join("..").join("..");
    let cabi_include = repo_root.join("cabi").join("include");

    // ---- bindgen libscc.h ----------------------------------------------
    let header = cabi_include.join("libscc.h");
    if !header.exists() {
        panic!(
            "scc-studio-agent build.rs: libscc.h not found at {} -- \
             ensure the parent repo's cabi/include/libscc.h is on disk.",
            header.display()
        );
    }
    println!("cargo:rerun-if-changed={}", header.display());

    let bindings = bindgen::Builder::default()
        .header(header.to_str().unwrap())
        .clang_arg(format!("-I{}", cabi_include.display()))
        // Limit the FFI surface to scc_* + libscc result codes; pulling in
        // libc + stdint would generate redundant types.
        .allowlist_function("scc_.*")
        .allowlist_type("scc_.*")
        .allowlist_var("SCC_.*")
        // Use libstd's c_void / c_char rather than ::std::os::raw::*; cleaner.
        .use_core()
        .ctypes_prefix("::core::ffi")
        .generate_comments(true)
        .layout_tests(false)
        .generate()
        .expect("bindgen libscc.h failed");

    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_dir.join("scc_sys.rs"))
        .expect("write scc_sys.rs");

    // ---- prost-build the protobuf schema --------------------------------
    let proto = manifest_dir.join("src").join("proto").join("control.proto");
    println!("cargo:rerun-if-changed={}", proto.display());
    let mut config = prost_build::Config::new();
    config.out_dir(&out_dir);
    config
        .compile_protos(&[proto], &[manifest_dir.join("src").join("proto")])
        .expect("prost-build control.proto");

    // ---- libscc link search path ---------------------------------------
    //
    // Production builds set SCC_LIBSCC_DIR to the directory holding
    // libscc.{so,dll,dylib}. Local dev with the parent repo built into
    // <root>/build/cabi/ picks that up automatically.
    let libscc_dir = env::var("SCC_LIBSCC_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|_| repo_root.join("build").join("cabi"));
    println!("cargo:rustc-link-search=native={}", libscc_dir.display());
    // The library is named "libscc" on Unix-likes (CMake adds the lib prefix)
    // and "scc" on Windows (CMake's OUTPUT_NAME=scc -> scc.dll). Rust's
    // dylib link kind handles both: passing "scc" maps to libscc.{so,dylib}
    // on Unix, scc.lib on Windows.
    println!("cargo:rustc-link-lib=dylib=scc");
    println!("cargo:rerun-if-env-changed=SCC_LIBSCC_DIR");
}
