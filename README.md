# CCRP Language and Cryptic IDE

A small interpreted language (CCRP) with a GTK-based IDE. Supports integers, strings, control flow, input/output, math ops, a simple library system, and native GTK UI building with dot syntax and CSS-like styling.

## Build

```
make install-lang
make
```

- `cryptic_ide`: GUI IDE with dark scheme, syntax highlighting, file tree, and external Run
- `cride_interpreter`: CLI interpreter for `.crp` files

## Run

```
./cryptic_ide           # launch IDE
./cride_interpreter file.crp
```

## Language Overview

- Comments: `// this is a comment`
- Print:
  - `print "Hello"`
  - `print x + 1`
  - `print "A:", a, ", B:", b`
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
  - Note: user-defined functions are parsed and stored; execution semantics are minimal in this build.

## Libraries

Import libraries in two ways (both load `src/NAME.crh`):

- Old style: `[src] NAME`
- New pragma: `#[NAME]`

Example:

```
#[string]
#[math]
#[gtk]
```

Math built-ins (require `#[math]`): `sqrt, abs, sin, cos, tan, log, exp, pow, mod, max, min`

## Native GTK UI (require `#[gtk]`)

Create and manipulate widgets either with direct commands or dot syntax. The interpreter contains a lightweight GTK runtime; no external process is spawned.

- Creation (either form):
  - Direct: `gtk create window win`, `gtk create button ok`, `gtk create entry name`, `gtk create vbox root`, `gtk create image logo`
  - Dot: `gtk.window(win)`, `gtk.button(ok)`, `gtk.entry(name)`, `gtk.vbox(root)`, `gtk.image(logo)`
- Properties (dot syntax maps internally):
  - `win.title("My App")`
  - `win.size(400x600)` or `win.size(400,600)`
  - `ok.text("OK")`
- Layout:
  - `root.add(ok)`
  - Direct equivalent: `gtk add root ok`
- Showing and main loop:
  - `win.show()`
  - `gtk.run()`
- Shortcut alias:
  - `class NAME TYPE` → same as `gtk create TYPE NAME` (e.g., `class root vbox`)
- Grouping braces are allowed as no-ops: `{ ... }`

Example:

```
#[gtk]

gtk.window(win)
win.title("Styled App")
win.size(400x300)
class root vbox
win.add(root)

gtk.entry(name)
gtk.button(ok)
ok.text("OK")
root.add(name)
root.add(ok)

win.show()
gtk.run()
```

### CSS-like Styling

Apply GTK CSS with `style TARGET { ... }` blocks:

- TARGET can be a widget name you created (e.g., `win`, `ok`), a type selector (`button`, `entry`, `label`, `window`, etc.), or `*` for global.

Examples:

```
style win {
  background-color: #222;
  color: #eee;
}

style button {
  font-weight: bold;
}

style * {
  font-family: Sans;
}
```

## GTK Quick Reference

Requires `#[gtk]`.

- Create widgets (choose one syntax):
  - Dot: `gtk.window(NAME)`, `gtk.button(NAME)`, `gtk.entry(NAME)`, `gtk.vbox(NAME)`, `gtk.hbox(NAME)`, `gtk.label(NAME)`, `gtk.image(NAME)`
  - Direct: `gtk create window NAME`, `gtk create button NAME`, `gtk create entry NAME`, `gtk create vbox NAME`, `gtk create hbox NAME`, `gtk create label NAME`, `gtk create image NAME`
  - Alias: `class NAME TYPE` (TYPE = `window`, `button`, `entry`, `vbox`, `hbox`, `label`, `image`)
- Set properties (dot syntax):
  - Title: `NAME.title("Text")` (window)
  - Size: `NAME.size(WxH)` or `NAME.size(W, H)` (any widget; windows set default size)
  - Text: `NAME.text("Text")` (button, label, entry)
- Layout:
  - Add child to container: `PARENT.add(CHILD)`
  - Direct: `gtk add PARENT CHILD`
- Show and run:
  - `NAME.show()` (typically the window)
  - `gtk.run()`
- Styling:
  - Per widget: `style NAME { /* CSS */ }`
  - By type: `style button { /* CSS */ }`
  - Global: `style * { /* CSS */ }`

Minimal window with button and entry:

```
#[gtk]

gtk.window(win)
win.title("My App")
win.size(400x300)
class root vbox
win.add(root)

gtk.entry(name)
name.text("type here")

gtk.button(ok)
ok.text("OK")

root.add(name)
root.add(ok)

win.show()
gtk.run()
```

## IDE Features

- Dark syntax highlighting for CCRP (`make install-lang` once, then restart IDE)
- File tree sidebar with “Open Folder”
  - Recursively lists files/folders
  - Double-click a file to open it in the editor
- External terminal execution (“Run”)
- Client-side header bar (system title bar hidden)
- Interpreter picker: click “Interpreter” to choose a custom interpreter binary (saved in `~/.config/cryptic-ide/config.ini`)

## Utilities & Dependencies

- Build/runtime libraries:
  - GTK+ 3.0, GLib 2.0, GtkSourceView 3.0, Cairo, Pango, GDK-Pixbuf
  - pkg-config, gcc/clang, make
- IDE run-time helpers (optional, for external terminal):
  - One of: `gnome-terminal`, `xterm`, or `konsole`
- Syntax highlighting install:
  - `make install-lang` copies `ccrp.lang` to `~/.local/share/gtksourceview-3.0/language-specs/`
- File type association:
  - `make install-mime` registers `.crp` as `text/x-ccrp` in the user MIME database
- Packaging (optional):
  - AppImage: `make appimage` (requires `appimagetool`)
  - Debian package: `make deb` (requires `dpkg-deb`)

## Packaging

- AppImage
  - Install `appimagetool` (for example to `~/.local/bin`) and ensure it is in `PATH`.
  - Optional: place a `logo.png` (256×256) at the project root to be used as the AppImage icon.
  - Build:
    - `make clean && make`
    - `make appimage`
  - Output: `pkg/CrypticIDE-<version>-x86_64.AppImage`
- Debian (.deb)
  - Requires `dpkg-deb`.
  - Build: `make clean && make && make deb`
  - Output: `pkg/cryptic-ide_<version>_amd64.deb`

## Troubleshooting

- No colors? Run `make install-lang`, then restart `cryptic_ide`. The IDE also adds the current workspace and user language paths for `ccrp.lang`.
- External terminal missing? The IDE tries `gnome-terminal`, then `xterm`, then `konsole`.
- GTK commands say library missing? Ensure `#[gtk]` is at the top of your `.crp` file. 