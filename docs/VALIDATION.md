# Native port validation — 23 October 2026

Upstream `master` was checked again after the port: `d35322c47d53903638558162a5e3153f32dfdc37` remains the latest revision. The port retains that commit in its history and records it in `UPSTREAM.json`.

Validated on Linux with Rust 1.93.1, GCC 15.2, CMake, DomeD's BDF-enabled FLTK 1.4.5 and MuPDF 0.8.0.

- `cargo check --locked --release`: passes.
- `cargo test --locked --release`: 13 tests pass. These cover valid/invalid configuration, legacy mouse aliases and settings, reserved Super bindings, multi-key chords, page layout geometry, UTF-8 and cross-page search, regex/cancellation, real PDF text/links/comments/content rendering, password retry, invalid input and clipped-tile consistency.
- CMake release build and CTest: pass.
- `scripts/test-ui.py`: 24 checks pass on a private Xvfb display and DBus session. Actual keyboard and DBusMenu events exercise tabs, user bindings, Super passthrough, chords, copying, search, live light/dark appearance, native 16px and enlarged 32px BDF text, PDF theme following, RPC, file reload, command enablement and clean shutdown/persistence. The screenshot assertion waits for the presented light frame and checks visible glyph height and pixels, rather than relying only on reported font metadata.
- `scripts/test-dome.py`: 7 checks pass against a private nested DomeD session. Native Wayland surface/menu registration, real desktop and XR menu pointer actions, visible rendered PDF pixels, Super+X desktop/XR focus switching and exporter/surface cleanup are checked.
- `ldd build/miro-pdf`: the executable and its resolved toolkit dependencies have no Pango dependency or missing libraries.
- No TTF/OTF interface font assets remain; the supplied fallback is raw Unifont BDF. Original font assets remain only in upstream Git history.

The port exposed a static-library interposition issue: MuPDF's private FreeType (built without BDF drivers) exported `FT_*` symbols into the executable and overrode FLTK/Fontconfig's system FreeType. This caused absent UI glyphs and invalid metadata while scaling fonts. The CMake linker option `--exclude-libs,ALL` now keeps static MuPDF dependencies private. Both the visible-glyph test and the live BDF enlargement test pass with this boundary in place.

GUI tests use public fixture PDFs and private HOME/config/cache/runtime directories. They do not change the running desktop or saved user settings. Screenshots and machine-readable check lists are generated under ignored `artifacts/ui` and `artifacts/dome`.

The nested compositor uses software GL under Xvfb; hardware DMA-BUF transfer and a physical headset were not exercised. Printing uses the system `lp` command; no physical print job was submitted. The bitmap-only rule applies to the application interface; PDF content retains its authored typography.
