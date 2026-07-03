# Changelog

## Unreleased

### Fixed

- Stricter TOON spec conformance:
  - accept a literal HTAB inside quoted strings and keys (§7.1);
  - reject malformed bracket lengths in array headers, e.g. `[03]` and `[|]`
    (§6, §14.2);
  - trim spaces around split inline-array / tabular tokens and keep trailing
    empty tokens (§11.2);
  - error on blank lines inside arrays (§12, §14.2);
  - restrict the bare `[]` token to root and key-colon positions (§9.1).

### Documentation

- Add `TOON-SPEC-DISCREPANCIES.md` recording known, unresolved differences
  between the formal spec, the reference implementation, and yatl, linked from
  a new "Spec conformance" section in the README.

## 1.0.0

Initial release.

- Streaming, event-driven (SAX) parser for TOON, with a public API modeled on
  yajl's stream parser (`yatl_alloc` / `yatl_parse` / `yatl_complete_parse` /
  callbacks / `yatl_config` / `yatl_get_error`).
- Full TOON shape coverage: objects, root scalars, inline / tabular / expanded
  list arrays, root arrays, the empty document, and all three delimiters
  (comma, tab, pipe).
- yajl-faithful number handling (integer / double, with a `yatl_number`
  verbatim override and a `yatl_error` out-of-range hook).
- Options: `yatl_dont_validate_strings`, `yatl_allow_trailing_garbage`,
  `yatl_lenient_scalars`, `yatl_dont_validate_length`, `yatl_max_depth`.
- Handwritten `./configure` + Makefile with per-target build directories,
  static + shared libraries, pkg-config, and `strict` / `test-leaks` lanes.
- libFuzzer harness (`tests/fuzz.c`) with a chunk-independence oracle and a
  portable standalone replay driver, plus a seed corpus (`tests/corpus/`).
- GitHub Actions CI across clang/gcc on Linux and macOS, mingw64 cross to
  Windows, musl/static, emscripten/WASM, sanitizers, strict warnings, and fuzz.
