# Repository Guidelines

## Project Structure & Module Organization
Keep gameplay and tooling code in `src/`, grouped by system (e.g., `texture_manager.h`, `database.cpp`). Place tests in `tests/` as `test_*.cpp` Catch2 specs. Vendor code lives in `external/`. Runtime assets (`assets/`, `images/`, `thumbnails/`) and generated data (`db/`, `logs/`) are ignored by git. Build artifacts belong under `build/`, such as `build/Debug` or `build/Release`, and Windows helper scripts reside in `scripts/`.

Reusable Dear ImGui widgets (icon buttons, fancy text inputs, toggle chips, wrapped text rows, etc.) live in `src/ui/components.h/.cpp`; add new generic UI pieces there so panels can share them without duplication.

## Build, Test, and Development Commands
Configure the project once from the repo root:
```
cmake --preset windows
```
Build the Debug target whenever changes land:
```
cmake --build --preset windows --config Debug
```
Run the full verification suite before handing work back:
```
ctest --preset windows
```
Always finish by building the Debug target (`cmake --build --preset windows --config Debug`) to ensure changes compile.
Set `VCPKG_ROOT` (e.g., `/c/vcpkg`) so dependency resolution succeeds, and clean stale builds with `rm -rf build/ vcpkg_installed/` when toolchains drift.

## Coding Style & Naming Conventions
Use C++17 with 2-space indentation and UTF-8 encoding. Name files in `lower_snake_case`, types in `PascalCase`, functions in `lower_snake_case`, and constants in `UPPER_SNAKE_CASE`. Prefer `#include "..."` for local headers, keep diffs narrow, and only add clarifying comments for non-obvious logic.

## Testing Guidelines
Author unit tests with Catch2, naming files `tests/test_*.cpp`. Structure specs with sections and isolate fixtures via temporary resources. Build tests alongside the main target and execute them through `ctest --preset windows` or the generated binaries (e.g., `./build/DbTest`). Investigate failures before running again.

## Commit & Pull Request Guidelines
Write focused commits with short, imperative subjects (e.g., “Fix bulk delete bugs”). For pull requests, include the rationale, platforms exercised (Windows/macOS), test evidence, linked issues, and screenshots for UI-affecting changes. Update docs such as `readmes/` whenever behavior shifts.

## Security & Configuration Tips
Never commit `db/`, `logs/`, secrets, or credentials. Normalize paths to UTF-8 and route asset changes through the `EventProcessor`; avoid mutating the database directly. Lock the asset-map mutex before cross-thread access and coordinate any user-facing visuals with stakeholders before merging.
