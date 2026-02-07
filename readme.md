# TI-83+ Solana Hardware Wallet

This repository contains a proof-of-concept hardware wallet that runs on a TI-83 Plus graphing calculator. The calculator hosts the signing logic, communicates with a connected computer over the TI Link cable, and produces Solana-compatible signatures without exposing private keys.

The project blends calculator-side firmware written in C with vendored cryptography and TI connectivity libraries. Together they deliver an end-to-end pipeline: capturing user intent, deriving key material, framing Solana transactions, and returning signed payloads for broadcasting.

## Repository Layout

- `main.c`: Entry point that boots the calculator app, wires up session state, and drives the polling loop.
- `calc_session.c/.h`: Abstractions for the TI Link stack, including session lifecycle, cable detection, and message polling.
- `calc_string_store.c/.h`: Lightweight helpers for storing and retrieving calculator string variables.
- `wallet_crypto.c/.h`: High-level wallet primitives such as key derivation and signature orchestration built on the vendored Ed25519 stack.
- `examples.c`: Reference snippets that exercise the link layer and signing flow for development and testing.
- `solana/`: Modules specific to Solana encoding and client operations.
- `keypair/`: Vendored Ed25519 implementation used for key generation, hashing, and signature creation.
- `tilibs/`: Vendored TI connectivity libraries providing cable drivers, calculator protocol helpers, and file format utilities.

### Desktop UI (`ui/`)

A Rust/Dioxus desktop application that wraps the C library via FFI, providing a graphical interface for all wallet operations.

- `cwallet-sys/`: FFI bindings crate — manual declarations for all C functions (no bindgen).
- `cwallet-ui/`: Dioxus 0.6 desktop app with sidebar navigation, keypair management, and Solana operations.

Features:
- Connect/disconnect TI-83+ calculator
- Create and load keypairs across 10 slots (Str0–Str9)
- Check SOL balance, request devnet airdrops, send SOL transfers
- Adaptive light/dark theme following system appearance

### Solana Modules (`solana/`)

- `solana_client.c/.h`: Handles message framing between the calculator and host, translating inbound requests into wallet actions and packaging outbound responses.
- `solana_encoding.c/.h`: Provides Solana-specific serialization, including base58-style layouts and transaction message encoding used before hashing and signing.

### Vendored Cryptography (`keypair/`)

The `keypair` directory is imported wholesale to keep verification consistent with audited upstream code. The upstream implementation targeted a 64-byte private key layout; this fork carries minimal patches so the serialization and key derivation align with Solana expectations while leaving the core curve arithmetic untouched:

- `ed25519.h`: Public API exposing key generation, signing, and verification entry points.
- `fe.*`, `ge.*`, `sc.*`: Finite-field, group-element, and scalar arithmetic that back the curve operations.
- `sha512.*`: Hashing primitives sourced from the same upstream project, ensuring deterministic seed expansion.
- `sign.c`, `verify.c`, `keypair.c`: High-level routines that wrap the arithmetic layers to deliver Ed25519 keypair generation and signature workflows.
- Supplementary helpers (`add_scalar.c`, `seed.c`, `key_exchange.c`, `precomp_data.h`) provide advanced operations such as hierarchical key derivation and precomputed tables.

### Vendored TI Connectivity Libraries (`tilibs/`)

The `tilibs` folder tracks the official TilEm project libraries with minimal local modification. Each subproject builds into static libraries consumed by this calculator application:

- `libticables`: Hardware cable backends for USB SilverLink and other TI link adapters, handling low-level transport details.
- `libticalcs`: Calculator protocol logic that sits on top of `libticables`, managing device discovery, command dispatch, and data framing.
- `libticonv`: Character set and encoding utilities used when exchanging strings or filenames with the calculator OS.
- `libtifiles`: Parsers and writers for TI calculator file formats, enabling structured transfer of variables or application data.

Because both `keypair` and `tilibs` are vendored, keep changes limited and document any deviations from upstream sources. The Solana-specific adjustments in `keypair/` are tracked locally to avoid regressions.

## Build and Run Workflow

All development tasks are orchestrated through the top-level `Makefile`:

- `make configure`: Runs CMake configuration, creates the `build/` directory, and refreshes the `compile_commands.json` symlink for tooling.
- `make build`: Compiles the calculator application alongside all required vendored libraries.
- `make run`: Launches the compiled `main` executable, starting the interactive polling loop. Requires a TI-83 Plus connected via USB SilverLink or equivalent.
- `make clean`: Removes the `build/` directory and clears generated binaries for a fresh rebuild.
- `make menu`: Enables vendor regression tests by configuring CMake with `-DENABLE_VENDOR_TESTS=ON`, builds the test harness, and executes `test_ticalcs_2` against a SilverLink cable for protocol verification.

For deeper validation of the vendored TI libraries, review `TI_TILibs_Cheatsheet.md` and the test programs under `tilibs/libticalcs/trunk/tests/`.

## Getting Started

### CLI

1. Install a modern C toolchain with CMake support (C11 compatible compiler and make).
2. Connect a TI-83 Plus via a TI USB SilverLink cable or simulator that exposes the same protocol.
3. Run `make configure` followed by `make build` to compile the project.
4. Execute `make run` to start the calculator-host interaction loop.

### Desktop UI

1. Install [Rust](https://rustup.rs/) and ensure `pkg-config` and `glib-2.0` are available.
2. From the `ui/` directory, run `cargo run -p cwallet-ui`.
3. The desktop window will open — connect your calculator using the top-right button, then select a keypair slot from the dropdown.
