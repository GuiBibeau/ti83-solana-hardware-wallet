use std::env;

fn main() {
    // Build the C project via CMake (from the repo root, one level up).
    let dst = cmake::Config::new("../..")
        .define("ENABLE_VENDOR_TESTS", "OFF")
        .build_target("cwallet")
        .build();

    let build_dir = dst.join("build");

    // Static libraries produced by CMake (cwallet + solana)
    println!("cargo:rustc-link-search=native={}", build_dir.display());
    println!("cargo:rustc-link-lib=static=cwallet");
    println!("cargo:rustc-link-lib=static=solana");

    // tilibs are built as shared libraries (.dylib / .so) in subdirectories
    println!(
        "cargo:rustc-link-search=native={}",
        build_dir.join("tilibs/libticables/trunk").display()
    );
    println!(
        "cargo:rustc-link-search=native={}",
        build_dir.join("tilibs/libticalcs/trunk").display()
    );
    println!(
        "cargo:rustc-link-search=native={}",
        build_dir.join("tilibs/libticonv/trunk").display()
    );
    println!(
        "cargo:rustc-link-search=native={}",
        build_dir.join("tilibs/libtifiles/trunk").display()
    );
    println!("cargo:rustc-link-lib=dylib=ticalcs2");
    println!("cargo:rustc-link-lib=dylib=tifiles2");
    println!("cargo:rustc-link-lib=dylib=ticables2");
    println!("cargo:rustc-link-lib=dylib=ticonv");

    // System dependencies
    let glib = pkg_config::Config::new()
        .probe("glib-2.0")
        .expect("glib-2.0 not found via pkg-config");
    for path in &glib.link_paths {
        println!("cargo:rustc-link-search=native={}", path.display());
    }
    for lib in &glib.libs {
        println!("cargo:rustc-link-lib={}", lib);
    }

    println!("cargo:rustc-link-lib=curl");
    println!("cargo:rustc-link-lib=pthread");

    // macOS frameworks
    if env::consts::OS == "macos" {
        println!("cargo:rustc-link-lib=framework=IOKit");
        println!("cargo:rustc-link-lib=framework=CoreFoundation");
    }

    // Re-run if any C source changes
    println!("cargo:rerun-if-changed=../../CMakeLists.txt");
    println!("cargo:rerun-if-changed=../../calc_session.c");
    println!("cargo:rerun-if-changed=../../calc_string_store.c");
    println!("cargo:rerun-if-changed=../../wallet_crypto.c");
    println!("cargo:rerun-if-changed=../../solana/");
    println!("cargo:rerun-if-changed=../../keypair/");
}
