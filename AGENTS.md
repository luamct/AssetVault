# Repository Guidelines

## Project Structure & Module Organization
- `src/` C++17 sources (e.g., `database.cpp`, `texture_manager.h`).
- `tests/` Catch2 unit tests (`test_*.cpp`) and helpers.
- `external/` third‑party code (glad, imgui, fonts).
- `assets/`, `images/`, `thumbnails/` runtime resources; `db/` local data; `logs/` output logs.
- `build/` CMake build output (multi‑config on Windows: `build/Debug`, `build/Release`).
- `scripts/` Windows utilities (crash logs, cleanup).

## Build, Test, and Development Commands
- Use Git Bash on Windows; run from repo root.
- Presets: configure `cmake --preset windows`; build `cmake --build --preset windows --config Debug`; test `ctest --preset windows`.
- Clean: `rm -rf build/ vcpkg_installed/`. Ensure `VCPKG_ROOT` is set (e.g., `/c/vcpkg`).

## Architecture Overview
- Event‑driven: route all asset changes through `EventProcessor`; do not mutate the DB directly.
- Batch first: prefer `queue_events(...)` over per‑event calls for throughput.
- Thread safety: guard the shared assets map with its mutex when accessing it across threads.
- Thumbnails: SVGs are pre‑rasterized (max 240px). TextureManager caches failed models to avoid retry loops.

## Coding Style & Naming Conventions
- Indentation: 2 spaces; UTF‑8 source.
- Files: lower_snake_case (`file_watcher_windows.cpp`).
- Types: PascalCase (`AssetDatabase`). Functions/methods: lower_snake_case (`create_tables`).
- Constants/macros: UPPER_SNAKE_CASE (`SVG_THUMBNAIL_SIZE`).
- Includes: prefer `#include "..."` for project headers under `src/`.
- No repository‑wide formatter is enforced; match existing style. Avoid mass reformatting.

## Testing Guidelines
- Framework: Catch2 (`find_package(Catch2 CONFIG REQUIRED)`).
- Test files: `tests/test_*.cpp`; tag with sections where helpful.
- Build tests with the main target or via presets; run with `ctest` or directly (e.g., `./build/DbTest`, `./build/SearchTest`).
- Aim to cover DB ops, file watching (platform‑specific), search, and utils. Keep tests hermetic (use temp dirs/files).

## Commit & Pull Request Guidelines
- Commits: short, imperative summaries (e.g., “Fix bulk delete bugs”). Group related changes; keep diffs focused.
- PRs: include description, rationale, testing steps, platform tested (Windows/macOS), and screenshots for UI changes. Link issues when applicable. Update docs (`readmes/`, this file) as needed.

## Security & Configuration Tips
- Do not commit `db/`, `logs/`, or secrets. Normalize paths to UTF‑8.
- Windows: ensure MSVC toolchain is available for the `windows` preset.

## Assistant Workflow
- Don’t run git commands unless explicitly requested.
- Confirm visual/UI changes with a user run; don’t assume appearance.
- When the request is ambiguous, ask before changing code; answer questions without modifying code.

## Agent‑Specific Notes
- Minimize diffs, follow structure above, and do not reformat unrelated files. Add tests when changing logic.
