# File picker regression

Run from the repository root with the normal SDL2/MicroUI configuration:

```sh
make -j4 -f tests/preferences/Makefile filedialog-test
```

The test uses production MicroUI, dialog, and integration code without creating
a native window. It creates an isolated 200-file directory fixture under
`_obj_/filedialog-run` and navigates from its small child directory using `..`.
It checks immediate scrollbar visibility, full-directory scroll extent,
synthetic SDL click routing for all three dialog types, scrollbar
arrows and track, thumb focus across consecutive drag frames, mouse-wheel
scrolling, selection of the last file, and reset on returning to a small folder.

Also verify the Game > Load dialog manually with a real mouse, especially after
moving the window or changing display scaling. The automated test does not
exercise resized native windows, display scaling, or native mouse capture.

## Uzenet identities

With OpenSSL development libraries and the `openssl` command installed:

```sh
make -j4 -f tests/preferences/Makefile FLAG_OPENSSL=1 \
  OBD=_obj_/identity-tests uzenet-identity-test
python3 tools/test_config_path.py /path/to/cuzebox
```

The identity test creates temporary certificates under its object directory,
checks validation, persistence, reset and failed-save rollback, and renders the
Devices settings into `identity-run/identity-settings.bmp`. It verifies that the
selected identity is displayed and the controls scroll without horizontal
overflow. The config test launches two instances from one working directory and
checks separate config paths, paths containing spaces, reset/save isolation and
invalid path errors.
