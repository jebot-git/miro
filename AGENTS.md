# AGENTS.md - Miro PDF Viewer Development Guide

## Build/Test Commands
- `cargo check --locked --release` - Check the toolkit-independent Rust core before every build
- `scripts/build.sh` - Build the Rust static library and native FLTK frontend with CMake
- `build/miro-pdf file.pdf` - Run the viewer
- `cargo test --locked --release` - Run document, configuration, search, geometry and layout tests
- `ctest --test-dir build --output-on-failure` - Run registered core tests
- `uv run --with python-xlib --with pillow --with dbus-next scripts/test-ui.py` - Private-display native integration tests
- `cargo fmt --check` - Rust formatting

This fork's frontend is Linux FLTK (DomeD's BDF-only 1.4.5 toolkit); Iced,
Windows and upstream Nix packaging are not used. Preserve upstream provenance
in UPSTREAM.json when rebasing. Never access a MuPDF handle off its worker
thread. Super chords are owned by DomeD. UI fonts must remain raw BDF.

## Code Style Guidelines
- Use `snake_case` for variables, functions, and modules
- Use `PascalCase` for types, structs, enums, and traits
- Prefer explicit types over `auto`/inference when clarity improves
- Use `anyhow::Result` for error handling with context
- Import organization: std first, external crates, then local modules
- Use `#[derive(Debug)]` on all structs and enums
- Prefer `match` over `if let` for complex pattern matching
- Use `const` for compile-time constants (e.g., `const MOVE_STEP: f32 = 40.0`)
- Use `static` with `LazyLock` for global state (e.g., `CONFIG`)
- Implement `Default` trait where appropriate
- Use `EnumString` derive for string-to-enum conversion
- Prefer `PathBuf` over `&str` for file paths
- Use the existing worker queue for native asynchronous communication
- Structure modules with `mod.rs` files for organization
- Use the `debug!`, `info!` and `error!` from `tracing` for printing and logging

## Development Process Guidelines
- Prefer `cargo check` as a first layer of validating your code
- Run `cargo test` after you are done with each task to ensure that you haven't introduced any regressions
- Dont write tautological tests
