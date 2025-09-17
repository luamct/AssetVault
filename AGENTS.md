# Repository Guidelines

## Project Structure & Module Organization
Source code resides in `src/`, organized by systems such as `database.cpp` or `texture_manager.h`. Tests live in `tests/` and follow the `test_*.cpp` pattern with Catch2 helpers. Third-party dependencies are under `external/`, while runtime resources (`assets/`, `images/`, `thumbnails/`) and generated data (`db/`, `logs/`) remain outside version control. Build artifacts belong in `build/` (e.g., `build/Debug`, `build/Release`), and Windows utility scripts ship from `scripts/`.

## Build, Test, and Development Commands
Configure CMake presets from the repo root: `cmake --preset windows`. Build the Debug configuration with `cmake --build --preset windows --config Debug`, and rebuild after every set of changes before handing work back. Run the full test suite through `ctest --preset windows`. Clean local builds via `rm -rf build/ vcpkg_installed/` and ensure `VCPKG_ROOT` (e.g., `/c/vcpkg`) is set for dependency resolution.

## Coding Style & Naming Conventions
Write C++17 with 2-space indentation and UTF-8 encoding. Name files in lower_snake_case (`file_watcher_windows.cpp`), types in PascalCase (`AssetDatabase`), functions in lower_snake_case (`create_tables`), and constants in UPPER_SNAKE_CASE (`SVG_THUMBNAIL_SIZE`). Prefer `#include "..."` for project headers and avoid broad reformatting.

## Testing Guidelines
Use Catch2 for unit tests. Place new specs in `tests/test_*.cpp`, structure them with sections, and keep fixtures hermetic using temporary files or directories. Build tests alongside the main target and execute them via `ctest --preset windows` or the generated binaries (`./build/DbTest`).

## Commit & Pull Request Guidelines
Keep commits focused with short, imperative summaries (e.g., “Fix bulk delete bugs”). For pull requests, supply a clear rationale, outline testing performed, state the platform (Windows/macOS), and attach screenshots for UI changes. Link related issues and update docs such as `readmes/` when behavior changes.

## Security & Configuration Tips
Do not commit `db/`, `logs/`, or secrets. Normalize paths to UTF-8 and respect the event-driven architecture by routing asset changes through the `EventProcessor` rather than mutating the database directly. Maintain asset-map thread safety by locking its mutex before cross-thread access.

## Agent-Specific Notes
Avoid git commands unless requested. Coordinate with the user for visual changes, and when in doubt about requirements, ask for clarification before modifying code to keep diffs minimal and targeted.
