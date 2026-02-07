# Repository Guidelines

## Project Structure & Module Organization
Core application sources live in the repository root. `main.c` orchestrates runtime, `calc_session.c/.h` encapsulate cable setup and polling, and `examples.c` holds reference snippets. Vendored cryptography code sits in `keypair/`, which includes `ed25519.h`, curve arithmetic (`fe.*`, `ge.*`), hashing (`sha512.*`), and signing helpers (`sign.c`, `verify.c`); keep local changes minimal and well documented. TI-Libraries are vendored under `tilibs/` with discrete modules: `libticables` (cable backends), `libticalcs` (calculator protocol), `libticonv` (encoding utilities), and `libtifiles` (file formats). Avoid editing these directories unless syncing upstream patches. Generated artifacts such as `build/` and the `compile_commands.json` symlink should never be committed.

## Build, Test, and Development Commands
- `make configure`: run CMake configuration and materialize `build/` plus `compile_commands.json`.
- `make build`: compile the `main` executable and all linked TI libraries.
- `make run`: launch the interactive poll loop; requires a calculator (or simulator) connected to `PORT_1`.
- `make clean`: delete the CMake build tree for a fresh rebuild.
- `cmake -S . -B build -DENABLE_VENDOR_TESTS=ON && cmake --build build --target test_ticalcs_2 && ./build/tilibs/libticalcs/trunk/tests/test_ticalcs_2 -c SilverLink`: opt into the vendor regression tests when validating link protocols.

## Coding Style & Naming Conventions
Code targets C11 with the standard library only; prefer `<stdint.h>` types for fixed widths. Follow the existing four-space indentation and Allman brace placement (`int main(void)` then newline and opening brace). Module APIs use snake_case prefixes (`calc_session_*`) mirrored in filenames. Header guards should stay uppercase (`CALC_SESSION_H`) and shared structs live next to their implementations. Keep vendor directories pristine and add new headers under the root or a dedicated subfolder.

## Testing Guidelines
Vendored tests live inside `tilibs/.../tests` and are disabled by default. Enable them with `-DENABLE_VENDOR_TESTS=ON` when touching transport logic, and document any required hardware adapters. Place project-specific tests alongside the module they cover, naming files `test_<module>.c`. Aim to keep the interactive poll loop covered by at least a smoke test or mocked harness before merging.

## Commit & Pull Request Guidelines
Write commits in the imperative mood (`calc: add polling timeout`) and keep diffs focused. Reference related issues in the commit body when available. Pull requests should summarize intent, list manual testing (including calculator models exercised), and call out any vendor library changes. Add screenshots or logs if behavior differs from prior releases and request hardware validation when relevant.
