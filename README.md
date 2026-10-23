# Miro for DomeD

A native FLTK PDF viewer forked from [vincent-uden/miro](https://github.com/vincent-uden/miro), based on the latest upstream `master` at the start of this port: `d35322c47d53903638558162a5e3153f32dfdc37` (19 October 2026). The maintained fork is [jebot-git/miro](https://github.com/jebot-git/miro).

The interface uses DomeD's native FLTK toolkit, FreeType and Fontconfig. Interface text uses raw BDF bitmap fonts, including system and user fonts, native strikes and integer 1×/2× enlargement. Family, face, size, scale, colours and theme follow the live DomeD appearance settings. A raw Unifont 16 BDF is supplied for use outside DomeD. There are no bundled TTF interface fonts, Iced or Pango dependencies. MuPDF renders the PDF's own text and embedded fonts as authored; the session font does not replace document content.

The viewer supports multiple tabs, continuous single/double/title-page layouts, overview, presentation, fullscreen, page and document rotation, zoom, links, outlines, annotation comments, text copying, plain/regex search, bookmarks, recent files, reload on file changes, URL/stdin input, saving copies and system printing. Bookmarks and recent files retain upstream's JSON format. It reads the existing `miro.conf` binding format, including modes, multi-key chords and mouse bindings. The native menu exports the actual commands, enabled states, toggle/radio states and configured shortcuts to DomeD over DBusMenu and the Wayland appmenu protocol (X11 properties on X11). An in-window menu is available outside DomeD.

PDF tiles render on a dedicated MuPDF worker and upload to GL textures. Panning and presentation composite those textures on the GPU; normal drawing does not read the framebuffer back. Search and PDF loading do not run on the UI thread.

## Build and install

Linux with the **DomeD BDF-enabled FLTK 1.4.5** toolkit is required. Stock FLTK does not provide the bitmap-only native text path used here. Install/build the toolkit from [fltk-vr](https://github.com/jebot-git/fltk-vr) first. The upstream Windows/Iced and Nix packaging does not apply to this frontend.

Build dependencies: CMake 3.22+, C++17 compiler, Rust 1.88+ with edition 2024 support, Cargo, Clang/libclang development headers for MuPDF's bindgen, pkg-config, nlohmann-json, FreeType/Fontconfig, zlib, libpng, OpenGL, GLib/GIO, X11, Wayland development files and `wayland-scanner`. Python 3 and `unzip` are also needed for MuPDF’s bundled output templates. A C/C++ toolchain is needed to build MuPDF. This checkout was validated with Rust 1.93.1.

```sh
scripts/build.sh
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$HOME/.local"
fc-cache -f "$HOME/.local/share/fonts"
update-desktop-database "$HOME/.local/share/applications"
build/miro-pdf document.pdf
```

`FLTK_DIR` defaults to `/opt/fltk-vr/toolkit/share/fltk`; override it for a different toolkit prefix. `CARGO_HOME`, `CARGO_TARGET_DIR`, `MIRO_BUILD_DIR`, `MIRO_BUILD_JOBS` and `BINDGEN_EXTRA_CLANG_ARGS` are supported. CMake runs `cargo check` before building the Rust library. It derives the compiler's builtin include directory when bindgen needs it; an installed Clang resource directory can instead be supplied through `BINDGEN_EXTRA_CLANG_ARGS`.

The installed application is `miro-pdf`, with the same desktop ID and PDF MIME type as upstream. Installation does not change your default PDF application or overwrite `miro.conf`.

## Controls and configuration

- Ctrl+O opens PDFs; Ctrl+W closes a tab; Ctrl+Tab / Ctrl+Shift+Tab switch tabs.
- Ctrl+F opens Find; Enter searches; F3 / Shift+F3 select matches. The Regex checkbox selects regex search.
- Ctrl+G goes to a page; PgUp / PgDn scroll; Ctrl+8 fits a page; Ctrl+9 fits width; Ctrl+0 selects actual size; Ctrl++ / Ctrl+- zoom.
- F11 toggles fullscreen; F10 toggles presentation; Enter opens overview. Click a thumbnail to navigate.
- Left-drag pans; Shift+left-drag selects text; Ctrl+C copies; Ctrl+A copies the current page's text; Ctrl+wheel zooms. Clicking a link follows it.
- Ctrl+B adds a bookmark; Ctrl+Shift+B opens the sidebar. The sidebar shows Contents, Bookmarks, Search or Comments. Select a comment to read its full text.
- Ctrl+1 / Ctrl+2 / Ctrl+3 select page layouts. `[` / `]` rotate a page; `{` / `}` rotate the document.
- Vim keys and multi-key bindings remain available; see [assets/default.conf](assets/default.conf).
- Super shortcuts belong to the compositor. Super+X switches desktop/XR input focus; Miro does not bind or consume it.

Configuration lives in `$XDG_CONFIG_HOME/miro-pdf` (normally `~/.config/miro-pdf`). User bindings replace the same key and mode from the defaults. Invalid lines are reported and later valid lines still load. For example:

```text
Bind Ctrl+f Normal OpenSearch
Bind "g g" Normal ZoomHome
MouseBind Shift+MouseLeft Normal Selection
Set Rpc False
```

DomeD owns the interface appearance; `ToggleDarkModeUi` directs DomeD users to its Appearance menu. Outside DomeD it toggles the local interface palette. `ToggleDarkModePdf` is a per-document rendering override; otherwise the document background follows the session's light/dark palette. The legacy `DarkModeUi` and `DarkModePdf` settings are accepted for configuration compatibility; session appearance takes precedence. Application positions/layouts are saved in `fltk-state.json`, separately from `miro.conf`. Print uses `lp` and the configured system default printer; remote PDFs should be saved locally first.

RPC is off by default. With `Set Rpc True`, the upstream JSON POST interface listens on loopback port 7890 (`RpcPort` to change it). Supported messages are `OpenFile`, `CloseFile`, `ToggleDarkModeUi`, and `ToggleDarkModePdf`; file messages use `{"message":{"type":"OpenFile","data":{"path":"/path/file.pdf"}}}`. `RpcAllowLan True` explicitly enables network binding. Requests and the queue are bounded.

## Development and licensing

The Rust library exposes document metadata, tiled rendering, text, search, links, comments, configuration and persistence through a small C ABI. Every MuPDF document lives entirely on the render worker thread. Static MuPDF symbols are hidden at link time so its private FreeType cannot override FLTK/Fontconfig’s bitmap-capable FreeType. Native code is in `fltk/`; toolkit-independent upstream geometry, layout and search algorithms remain in `src/`.

Run `cargo check --locked --release`, `cargo test --locked --release`, the CMake build and [scripts/test-ui.py](scripts/test-ui.py) / [scripts/test-dome.py](scripts/test-dome.py) after a rendering or integration change. The UI test uses private displays/configuration and does not alter the active desktop. Diagnostic `--report`, `--capture`, `--script` and `--quit-after-ms` options support repeatable integration tests.

Miro and the new frontend are AGPL-3.0. Shared DomeD integration helpers retain their MIT license; see [fltk/LICENSE.MIT](fltk/LICENSE.MIT) and [NOTICE](NOTICE). Unifont's license and font embedding exception are in [assets/fonts/COPYING.unifont](assets/fonts/COPYING.unifont). Upstream application history remains in Git; [UPSTREAM.json](UPSTREAM.json) records the port's base.
