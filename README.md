# yatl

[![ci](https://github.com/liquidaty/yatl/actions/workflows/ci.yml/badge.svg)](https://github.com/liquidaty/yatl/actions/workflows/ci.yml)

**yatl** is a small, streaming, event-driven (SAX) parser for
[TOON](https://github.com/toon-format/spec) (Token-Oriented Object Notation),
written in portable C11.

Its public API is modeled directly on [yajl](https://lloyd.github.io/yajl/) — if
you have used yajl's stream parser, yatl is the same shape with a `yatl_` prefix
and TOON-appropriate options. TOON encodes the JSON data model, so the callback
set is the JSON one: a tabular array `users[2]{id,name}:` is reported as an array
of two objects, exactly as the equivalent JSON would be.

## Why an event parser

TOON is indentation-structured, so the parser is a line-oriented pushdown
machine: input is buffered only up to the current newline, and events are
emitted as soon as each line is understood. **Peak memory is a function of
nesting depth and the widest single line** (e.g. one tabular row) — never of
total input length. Input may be fed in arbitrary chunks.

## Example

```c
#include <yatl/yatl_parse.h>
#include <stdio.h>

static int on_key(void *c, const unsigned char *s, size_t n)  { printf("key %.*s\n", (int)n, s); return 1; }
static int on_int(void *c, long long v)                       { printf("int %lld\n", v);        return 1; }

int main(void) {
    yatl_callbacks cb = {0};
    cb.yatl_map_key = on_key;
    cb.yatl_integer = on_int;

    yatl_handle h = yatl_alloc(&cb, NULL, NULL);
    const unsigned char *toon = (const unsigned char *)"port: 8080\n";
    yatl_parse(h, toon, 11);
    yatl_complete_parse(h);
    yatl_free(h);
    return 0;
}
```

A complete, runnable demo lives in `app/main.c` (the `yatl` CLI), which prints
the event stream for any TOON file or stdin.

## API (`include/yatl/yatl_parse.h`)

The same surface as yajl's parser:

| yatl | role |
|------|------|
| `yatl_alloc` / `yatl_free` | create / destroy a parser handle |
| `yatl_parse` | feed a chunk of TOON |
| `yatl_complete_parse` | flush the final line and close open containers |
| `yatl_config` | toggle options after allocation |
| `yatl_swap_callbacks` | swap the callback table / context |
| `yatl_get_error` / `yatl_free_error` | render / free a (verbose) error string |
| `yatl_get_bytes_consumed` | bytes consumed / error offset |
| `yatl_status_to_string`, `yatl_version` | helpers |

Callbacks mirror yajl: `yatl_null`, `yatl_boolean`, `yatl_integer`,
`yatl_double`, `yatl_number`, `yatl_string`, `yatl_start_map`, `yatl_map_key`,
`yatl_end_map`, `yatl_start_array`, `yatl_end_array`, and the optional
`yatl_error` (number out of range). Number dispatch mirrors yajl: if
`yatl_number` is set, every number is delivered to it verbatim; otherwise
integers go to `yatl_integer` and the rest to `yatl_double`. yatl does accept a
wider range of numeric input than yajl — notably `LLONG_MIN`, and out-of-range
integers widened to `yatl_double` — see
[`YAJL-DIFFERENCES.md`](YAJL-DIFFERENCES.md).

### Options (`yatl_config`)

| option | effect |
|--------|--------|
| `yatl_dont_validate_strings` | skip UTF-8 validation of strings/keys (on by default) |
| `yatl_allow_trailing_garbage` | do not error on content after the document |
| `yatl_lenient_scalars` | accept any unquoted token as a string instead of rejecting ones the encoder would have quoted |
| `yatl_dont_validate_length` | skip checking an array's declared `[N]` count |
| `yatl_max_depth` | set the max container-nesting depth (takes an `unsigned` argument, default `YATL_MAX_DEPTH`; a zero argument is rejected) |

## TOON coverage

- objects (`key: value`, nested `key:` blocks) and root scalars
- inline arrays `key[N]: a,b,c`
- tabular arrays `key[N]{f1,f2}:` with one object per row
- expanded list arrays `key[N]:` with `- item` lines (scalars or objects)
- root arrays in all three forms, and the empty document → `{}`
- all three delimiters: comma (default), tab, and pipe, declared in the bracket
  segment (`[N|]`, `[N<tab>]`) and applied to fields, rows and inline elements
- string quoting & escapes (`\" \\ \n \r \t \uXXXX`, including surrogate pairs),
  with strict-mode rejection of values that should have been quoted
- declared `[N]` length validation

## Spec conformance

There remain unresolved differences between this implementation, the formal
[TOON specification](https://github.com/toon-format/spec), and the
[reference implementation](https://github.com/toon-format/toon). Some are cases
where yatl is stricter than the spec, some where the reference is more lenient,
and some are genuine spec ambiguities where the two implementations diverge.
See [`TOON-SPEC-DISCREPANCIES.md`](TOON-SPEC-DISCREPANCIES.md) for the full comparison.

## Building

```sh
./configure           # writes config.mk and yatl.pc (handwritten; no autoconf)
make build            # static + shared libyatl and the yatl CLI
make test             # run the unit suite
make install          # under $PREFIX (default /usr/local)
```

Useful flags and targets: `ASAN=1` (AddressSanitizer + UBSan), `DEBUG=1`,
`make strict` (warnings-as-errors compile), `make test-leaks`, `make help`.
Artifacts go under `build/<target-triple>/<cc>-<version>/<variant>/`, so
native, cross (mingw64), and emscripten builds coexist.

## Fuzzing

```sh
make fuzz             # libFuzzer build (needs an LLVM clang with libFuzzer)
make fuzz-standalone  # portable replay driver (any toolchain), e.g. to replay a crash
make docker-fuzz      # run libFuzzer in a Linux container (needs only Docker)
```

`make docker-fuzz` exists because some hosts — notably Apple-Silicon macOS —
can't link libFuzzer natively. It builds a small Linux clang image
(`infra/docker/Dockerfile.fuzz`), bind-mounts the repo, and runs `make fuzz`
inside it; build output and any crash files land under `build/` on the host (new
corpus in `build/fuzz/corpus`, crashes in `build/fuzz/artifacts`). It needs only
Docker — no external repo, `yq`, or compose. Tune with `FUZZ_SECONDS=N`.

The harness (`tests/fuzz.c`) drives the parser with a **chunk-independence
oracle**: because the parser is line-oriented, feeding an input whole and in
arbitrary chunks must yield the identical event stream and status, so any
divergence (compared via a bounded rolling hash) `abort()`s as a real
chunk-boundary bug. Seed inputs live in `tests/corpus/`.

## Continuous integration

`.github/workflows/ci.yml` builds and tests on every push/PR across clang and
gcc (Linux + macOS, x86-64 and ARM64), mingw64 cross to Windows, musl/static on
Alpine, and emscripten to WebAssembly, plus an ASan+UBSan run, a strict-warnings
lane on both compilers, and a short libFuzzer session.

## License

MIT.
