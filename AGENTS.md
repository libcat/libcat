# Libcat development rules

These rules apply to this repository. They supplement global instructions with
Libcat's language, compatibility, dependency and validation requirements.

## Project boundaries

- Libcat is a C library, including a C API consumed outside its C++ tests.
  `CMakeLists.txt` currently uses C11 for the library and C++20 for tests.
  Do not introduce C++ requirements into production code or change C declarations
  merely to satisfy the test compiler.
- `include/` contains headers; `src/` contains library implementations;
  `deps/` contains embedded dependencies; `tests/` contains GoogleTest cases and
  shared helpers in `test.h` and `test.cc`. Build settings belong in
  `CMakeLists.txt`; platform CI settings belong in `.github/workflows/`.
- Preserve existing supported platforms, older systems and dependency-version
  branches. Updating a CI runner or its packages does not authorize raising the
  library's minimum requirements or deleting compatibility paths.

## Language and existing style

- Write new comments, documentation, diagnostics and commit messages in English.
  Preserve existing comments, including historical non-English comments, unless
  their modification is explicitly authorized.
- Read the relevant implementation, callers and nearby Git history before changing
  behavior. Existing thresholds, private headers, cleanup order and fallback paths
  may encode tested constraints; appearance alone is not grounds for replacement.
- Follow nearby code: four-space indentation in project C/C++, function opening
  braces on a separate line, control-flow opening braces on the same line,
  `cat_*` functions and types, and `CAT_*` macros. Preserve LF line endings and
  third-party files' own formatting. Avoid unrelated reformatting.
- Reuse existing allocation, error, coroutine and synchronization helpers.
  Respect their thread restrictions; an API callable from a worker thread does
  not necessarily permit updating coroutine-local error state there.
- Comments should explain a constraint, ownership rule or behavior boundary.
  Do not restate the syntax or invent an unverified design rationale.

## Third-party source: preserve upstream code

- libuv is a third-party library copied into this repository, not Libcat-owned
  source. Keep its upstream source unchanged by default. Embedding it does not
  authorize refactoring, reformatting or replacing its declarations and algorithms.
- If an upstream warning causes CI to fail, suppress that specific warning in
  Libcat's build configuration; do not edit libuv's source to remove the warning.
  Use the existing `check_add_cflags` mechanism to probe compiler support and
  apply the specific flag to the affected dependency target, such as
  `uv_cflags` / `cat_uv`. Keep project warnings and `-Werror` or `/WX` effective.
- Example: newer Clang warns about an upstream array declaration. Suppress the
  exact warning for `cat_uv`; do not replace the declaration with an enum merely
  to make CI pass.
- Modify third-party source only when a real defect or integration requirement
  cannot be addressed without a source change. For libuv, put every necessary
  Libcat-specific change behind `HAVE_LIBCAT`. Keep the original upstream
  implementation in the non-Libcat branch: without `HAVE_LIBCAT`, the patch must
  not change behavior. Do not overwrite the original implementation with a
  permanently modified version.
- Before such a patch, read the existing `HAVE_LIBCAT` branches in
  `deps/libuv/src/uv-common.h` and the affected source. Follow their organization,
  keep the patch local, and make the original code and Libcat-only difference
  easy to identify when reviewing or updating the dependency.
- Do not remove private-header dependencies or assertions about libuv's internal
  state just to make compilation pass. First establish why they exist; preserve
  the checks and adapt only the incompatible inclusion or build condition.

## Timing and resource lifetime

- A timing-sensitive test can fail under scheduling, debugger or coverage-tool
  interference without proving the production design is wrong. Investigate the
  recorded behavior before changing either the test or production implementation.
- When a test requires an ordering guarantee, use existing synchronization
  primitives. A short sleep does not guarantee that a worker runs after a timeout.
- Reuse `co`, `work`, `wait_group` and `DEFER` patterns in existing tests. Trace
  cleanup on successful completion and assertion-return paths: release blocked
  workers, close resources needed to unblock them, join their completion, then
  destroy captured stack state and synchronization objects.
- Watchdog tests must distinguish transient sampling alerts from sustained
  stalls using the actual `quantum` and `threshold`. Preserve the peak observation
  before recovery resets counters, and stop/join the observer before reading it.
  Keep intentional-blocking detection covered; do not increase production limits,
  skip the test or add retries merely to obtain a passing result.

## CI investigation and local validation

- Inspect the actual failing job and log. Separate package installation, compiler
  diagnostics, sanitizer failures, test assertions and external network failures.
  Do not treat every platform failure as the same defect.
- For CI repairs, build and run the affected existing tests locally before
  recommending a push. Repeat timing-sensitive cases and run the full available
  suite. Compilation alone is not runtime validation. Do not add new cases or
  files without authorization.
- `./run-tests.sh` builds and runs the existing suite; it forwards GoogleTest
  options, for example `--gtest_filter='cat_async.*'` and `--gtest_repeat=50`.
  The built executable is `build/cat_tests`. Use the workflow and script to select
  matching features, build type and sanitizer settings.
- If a sanitizer fails before tests start, check a minimal standalone program to
  distinguish a toolchain failure from a library failure. Use an isolated build
  directory when changing toolchains; do not silently alter the user's global
  configuration or reuse an incompatible CMake cache.
- Report compiler versions, relevant build options and any local warning
  exceptions. A run with a downgraded warning does not prove strict CI compilation
  passes. Distinguish unavailable platform coverage from locally verified results.
- Remote-site tests depend on both the endpoint and the network path. Capture
  Libcat's error code and message before cleanup overwrites them; use existing
  diagnostics such as `CAT_SHOW_LAST_ERROR=1` where appropriate. Compare repeated
  library runs with an independent client. Later success alone does not establish
  the cause of an earlier failure.
- Suppression or ignoring is appropriate only for a diagnosed nonfunctional
  issue, within the smallest applicable compiler, platform, target or diagnostic
  scope. Do not hide memory errors, broken assertions or sustained stalls.

## Delivery

- Keep Git history linear; do not create merge commits.
- Group commits by one coherent behavior, defect or development rule. Keep the
  implementation and its regression tests together. Separate independent changes,
  but avoid splitting a complete change into mechanical micro-commits or bundling
  unrelated changes into one large commit. Do not rewrite shared history to adjust
  commit grouping.
- Keep changes limited to the approved repair or request. Preserve unrelated code
  and comments; clean temporary diagnostic hooks and artifacts after recording
  useful evidence.
- Follow the repository's English imperative commit titles, such as `Fix ...`,
  `Add ...` or `Handle ...`. Stage explicit task paths only. Local plans remain
  in an ignored `docs/plans/` directory and are never committed.
- Local commit authorization does not authorize a push. Report local test
  results separately from remote CI status; claim CI is green only after checking
  the relevant run for the pushed commit.
- Keep this file for durable project rules. Do not add current run IDs, temporary
  failures, machine-specific paths, task progress or secrets.
