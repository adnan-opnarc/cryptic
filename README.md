# CCRP Language and IDE

A small interpreted language (CCRP) with a GTK IDE. Supports integers, strings, control flow, input/output, math ops, and simple library loading.

## Build

```
make install-lang
make
```

- `cride_modern`: IDE with dark mode and syntax highlighting
- `cride_interpreter`: CLI interpreter for `.crp` files

## Run

```
./cryptic     # launch IDE
./cride_interpreter file.crp
```

## Language Overview

- Comments: lines starting with `#`
- Print: `print "Hello"`, `print x + 1`, `print "A:", a, ", B:", b`
- Input:
  - Integer: `input age "Enter age:"`
  - Text: `input_text name "Enter name:"`
- Variables: integers or strings
  - `x = 10`
  - `name = "Alice"`
- Control flow:
  - `if cond ... else ... endif`
  - `while cond ... endwhile`
- Functions (library/crypton style):
  - Rust-like alias: `fn add(a, b) { ... }`
  - Classic: `function add(a, b) { ... }`
  - Note: user-defined functions are parsed and stored; built-in return is stubbed in this revision.

## Libraries

- You can import libraries in two ways:
  - Old style: `[src] NAME` → loads `src/NAME.crh`
  - New pragma: `#[NAME]` on its own line → also loads `src/NAME.crh`

Example:

```
#[string]
#[math]
```

Example: `src/math.crh` and `src/string.crh` are provided. A stub `src/gtk.crh` is also available for UI-oriented experiments.

Example: `src/math.crh` (already provided). Loader accepts both `function` and `fn` headers:

```
fn max(a, b) {
    if a > b
        return a
    else
        return b
    endif
}
```

Math built-ins (require `[src] math`): `sqrt, abs, sin, cos, tan, log, exp, pow, mod, max, min`

## Examples

See `test.crp`, `simple_test.crp`, `test_input.crp`, `test_library.crp`.

```
[src] math
print "sum:", 2 + 3
input_text name "Name: "
print "Hello,", name
```

## Notes

- String printing supports comma-separated concatenation.
- `sin/cos/tan` are integer-scaled (×1000) in current build.
- Library functions are parsed; extend `call_function` to execute their bodies and support parameters/return fully.

## Troubleshooting

- Highlighting: run `make install-lang`.
- If terminal doesn’t open from IDE, fallback command runs interpreter directly. 
