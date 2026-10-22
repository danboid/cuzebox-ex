# Controller gameplay mapping

Open **CONFIG → INPUT → CONTROLLER MAPPING...** to assign physical gamepads
and configure their gameplay buttons. Up to eight SDL-compatible game controllers
can be connected at once. Controllers not recognized by SDL need an appropriate
`gamecontrollerdb.txt` mapping; its path is configurable under CONFIG → PATHS.

## Ports and multitaps

Each emulated port has its own **MULTITAP** checkbox. A normal port exposes one
destination; a multitap exposes four, for up to eight destinations with both taps
enabled. The screen labels them **PORT 1 / SLOT 1**, etc.; the game decides how
these correspond to player numbers. GUI slot labels are one-based; existing API
and advanced virtual-device slot indices remain zero-based.

Choose a destination, then choose its physical controller. Empty destinations get
a virtual joypad when assigned. Mouse and keyboard destinations remain available
through the existing layout / virtual-device editor and are not overwritten by
the gameplay mapper. Disabling a tap hides its extra slots without deleting their
assignments or mappings. Enabling it restores them. Games must support the
emulated UzeTap protocol to use the additional slots.

Mappings belong to the virtual joypad. Reassigning that virtual device in the
advanced **VIRT DEV** editor preserves its mappings. Assigning the same physical
controller or virtual device to multiple active destinations displays a warning;
intentional shared control is allowed. The advanced editor also supports a second
input binding, which the simpler screen preserves.

## Button setup

- Select a button row and press one controller input, or select **CONFIGURE ALL**
  to work through B, Y, Select, Start, directions, A, X, L, and R.
- Release controls before each step. Held inputs and simultaneous inputs are
  ignored until the controller returns to neutral.
- **CLEAR** removes a target's assignments. **SKIP** clears the current target
  and advances the wizard. **STOP LEARNING** or Escape cancels learning.
  On a controller, the usual menu chord also cancels learning: Start+Select on
  desktop builds, or L1+R1+Start on RGB30 builds. Release it to resume GUI control.
- D-pad directions, left-stick directions, face buttons, shoulders, Start/Back,
  triggers, and stick clicks can be assigned. SDL face-button labels describe
  Xbox-style positions: south A, east B, west X, north Y. The default mapping
  preserves the existing SNES-style physical arrangement.
- An asterisk highlights an emulated button when its assigned input is pressed.
- **LEFT STICK AS D-PAD** and **DEAD ZONE** configure analog-to-digital input for
  that physical controller. The default threshold is 50% of stick travel. These
  settings are shared by destinations using the same physical controller.
- **RESTORE DEFAULT BUTTONS** restores the selected virtual pad's first binding.

Changes remain a draft until **APPLY**. **CANCEL**, Escape (outside learning), or
closing the window discards the draft. Gameplay pauses while the mapping window
is open, even if the normal pause-while-GUI-open preference is disabled. Physical
controller gameplay input is suppressed while configuring; GUI navigation retains
its existing controls, and is suspended during input learning. Mouse and keyboard
remain available for cancellation. Use the shoulder buttons to scroll the GUI.

Applied settings use the existing `config.cfg` persistence and shutdown-save
behavior. CONFIG also provides SAVE NOW. No separate controller JSON file is
required. Fully unassigned mappings remain unassigned after restarting.

## Reconnection

Physical controller slots remember SDL GUIDs and, when supplied by SDL, serial
numbers. Reconnecting an identifiable controller restores its slot despite changes
in device enumeration order. Disconnecting a controller supplies neutral input.
Identical controllers that do not report unique serial numbers cannot reliably be
distinguished across restarts; check their assignments after reconnecting them.

Disconnected slots stay reserved so another device does not silently take their
assignments. If replacing old controllers exhausts the eight remembered slots,
use **CONFIG → INPUT → SDL DEVS → FORGET DISCONNECTED**, then assign
the replacement controllers. This clears disconnected identities and rescans the
connected devices; it leaves virtual button mappings intact.

## Testing haptic feedback

Open **CONFIG → INPUT → VIRT DEV**, select the virtual device, then choose
**HAPTICS**. Each host binding has a **TEST HAPTIC** button. It sends a
250 ms pulse to both motors on that binding's controller without changing the
game's haptic routing settings. Keyboard/mouse bindings, disconnected
devices and controllers reported as unsupported have a greyed-out button.
SDL 2.0.18 and newer can query rumble capability; older builds may leave the
button available to try when support is unknown. A driver reporting support
does not guarantee that a physical motor will respond. Explicit bindings never
fall back to an unrelated haptic device.

New host-controller assignments enable game haptics, the binding flag, and its
output route automatically when SDL reports rumble support. This applies to
Controller Mapping, joypad presets, and the advanced binding selector. Existing
assignments keep their saved settings, including an explicit opt-out.

For an existing assignment, turn on **ENABLE GAME HAPTICS**, select **BIND A**,
**BIND B**, or **BOTH** under **OUTPUT**, and enable **SEND GAME HAPTICS TO THIS
BINDING** for the matching controller. Leave **HAPTIC ACCEPT ANY ID** checked
unless a specific UzeBus ID is needed. The virtual device must be assigned to
the game's controller port/slot on the **PORTS** page.

The virtual-device editor separates **DEVICE** creation and naming, **PORTS**
assignments, **BINDINGS** and expandable button remapping, and **HAPTICS**.
The Input window is resizable; long pages use a single vertical scroll area.

## Regression check

`make TSYS=linux -f tests/preferences/Makefile controller-mapping-test` runs with
SDL's dummy video driver and eight virtual controllers (SDL 2.0.14 or newer).
It exercises separate host inputs and emulated packets, GUI learning and
Apply/Cancel, tap slot retention, configuration round-trips, and hotplug handling.
Physical hardware and firmware-specific controller drivers still need device testing.
