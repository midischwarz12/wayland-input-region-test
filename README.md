# Wayland Input-Region Test

A small native Wayland client for testing transparent input-region holes,
compositor decorations, overview hit testing, and resizing. Extracted from the
client used to test [niri's input-region changes](https://github.com/niri-wm/niri/pull/4608).
It does not depend on ChatGPT, Electron, GTK, or a particular compositor.

## Run

Run inside an existing Wayland session:

```sh
nix run github:midischwarz12/wayland-input-region-test
```

The client starts at 560x360 logical pixels, subject to compositor configuration.
It paints a green 200x80 rectangle at `(24, 24)` and assigns exactly that rectangle
to `wl_surface.set_input_region`. Everything else is transparent and outside the
input region. Configure sizes, pointer enter/leave events, and button presses are
logged to stderr. Close using your compositor's close-window command or Ctrl+C.

The client accepts compositor-requested sizes, has a 240x160 minimum, and has no
maximum window size hint. It draws **no resize border** and does not request
client-side interactive resizing. A 128 MiB shared-buffer safety limit rejects
unreasonable configure sizes.

Two diagnostic controls are available:

```sh
nix run github:midischwarz12/wayland-input-region-test -- --empty-input
nix run github:midischwarz12/wayland-input-region-test -- --full-input
```

- `--empty-input`: no client pixels receive input; nothing is painted. Useful for
  isolating compositor decorations. Without decorations the window is invisible.
- `--full-input`: the same green rectangle, but the entire surface receives input,
  including transparent pixels. This is the negative control: transparency alone
  does not imply click-through.

`--help` does not require a Wayland connection. The application ID is
`wayland-input-region-test`; the window title is `Wayland input-region test`.

## Test With Niri

Use the rule in [examples/niri.kdl](examples/niri.kdl) in a **test** configuration
or include it in your own configuration. The client and flake never modify your
compositor configuration. These rules deliberately disable Niri's decoration
background, which would otherwise obscure the transparent area.

1. Open another window and position this floating client over it.
2. Click the green rectangle. The client should receive and log the button press.
3. Click elsewhere inside its bounds. Input should reach the window underneath.
4. Repeat in overview. The green region should select the test window; a hole
   should select the window or workspace underneath. Overview selection is handled
   by the compositor, so do not expect a client button event there.
5. Resize with your compositor's normal modifier-plus-button binding, starting
   inside the green rectangle. Niri's usual binding is Mod+right-drag. The window
   should retain its new size; the green region stays at the same logical offset.
6. Run with `--full-input` and confirm the transparent area no longer passes input
   through. Run with `--empty-input` to remove all client input targets.

For compositor-border testing, use [examples/niri-border.kdl](examples/niri-border.kdl)
**instead of** the borderless rule. Clicking the visible compositor border should
target this window while holes still pass through. Plain left-drag border resizing
depends on the compositor's implementation; this client does not synthesize resize
requests or assume that a clickable border necessarily supports that gesture.

For the inactive-focus-ring regression, use `border { off; }`,
`focus-ring { on; }`, and `draw-border-with-background true`. When the test window
is active, Niri's visible focus-ring background should intercept holes. Make it
inactive without placing another window above it: that background disappears and
the holes should become click-through again.

Niri rules and overview behavior are compositor-specific; the Wayland client itself
can also run on other compositors. It is an `xdg_toplevel`, not an `xdg_popup` or a
layer-shell surface.

## Build And Check

```sh
nix build --max-jobs 2 --cores 2
nix flake check --max-jobs 2 --cores 2
nix develop --command make -j2 check
```

The flake exports a package, app, checks, and development shell for `x86_64-linux`
and `aarch64-linux`. The lockfile pins the dependencies. No system rebuild, unfree
package permission, or NixOS module is required.

Without Nix, install a C compiler, Make, pkg-config, the Wayland client development
files, `wayland-scanner`, and `wayland-protocols`, then run:

```sh
make -j2 check
./build/wayland-input-region-test
make install PREFIX="$HOME/.local"
```

The unit tests check buffer-size bounds, transparency, rectangle placement,
clipping at small sizes, and command-line validation. `nix flake check` also starts
an isolated headless Weston and checks real configure events, buffer release,
and the three input-region modes. It does not open windows on your desktop.
These checks do not prove Niri's hit-testing behavior: use the manual steps above
for that. Weston is a check-only dependency, not a dependency of `nix run`.

Shared buffers are released only after `wl_buffer.release` (or shutdown). The
client commits new buffers on configure events, not in a continuous render loop.
The compositor chooses placement and sizes; there is no polling, desktop control,
or persistent state.

## License

[MIT](LICENSE).
