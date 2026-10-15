# Emulator shortcuts

Open **TOOLS → SHORTCUTS** to change emulator commands independently of gameplay
controller mappings. Each action has one keyboard binding and one controller
binding. Applied bindings are saved in `config.cfg` using the normal configuration
save/shutdown behavior; CONFIG → SAVE NOW writes them immediately.

1. Choose an **ACTION**.
2. Select the keyboard binding, then press a key with optional Ctrl, Shift, Alt,
   or GUI/Windows/Command modifiers. Left and right modifiers are equivalent;
   Caps Lock and Num Lock do not change a shortcut. Modifier combinations must
   match exactly: Ctrl+F9 does not execute a plain F9 binding.
3. For a controller, choose **ANY PAD** or a specific saved controller slot, then
   select **CAPTURE PAD**. Release any held controls, hold the desired button or
   buttons together, then release them. The screen collects overlapping button
   presses into one combination. Any of the eight physical controllers can be
   used. Device slot numbers match the controller-mapping screen.
4. **CLEAR** removes the corresponding binding. **RESTORE THIS ACTION** and
   **RESTORE ALL DEFAULTS** affect the draft. **APPLY** commits all changes;
   **CANCEL** or closing the window discards them. Escape cancels capture first.

Duplicate key combinations or controller combinations targeting overlapping
devices block Apply and identify the conflicting actions. Different specific
controllers may use the same combination for different actions. Overlapping
controller combinations of different lengths are allowed; the most specific
matching combination wins. Releasing part of that combination does not trigger
an already-held smaller combination. Release and press again to trigger it.

Keyboard shortcuts run once per press, without keyboard-repeat toggles.
Controller shortcuts also run once per matching press. When multiple controllers
trigger the same action in one host iteration, the action runs once.

Controller source labels describe physical positions: SOUTH, EAST, WEST, NORTH,
Select/Back, Start, directions, L1/R1, triggers L2/R2, and stick clicks L3/R3.
These use SDL's normalized controls before virtual gameplay button remapping.
Left-stick directions follow the physical controller's stick-enable/dead-zone
settings. Prefer combinations to avoid using a normal gameplay button for a
global command. A complete recognized shortcut combination is removed from
gameplay input while held; individual buttons pressed before the combination is
complete can still reach the game.

## Actions and defaults

The original Escape and F1–F12 shortcuts remain the defaults, including separate
Shift+F6 through Shift+F10 actions. See the README's Controls table. Additional
actions include **OPEN / CLOSE GUI**, **RESET ROM**, **SAVE SCREENSHOT**, and
**SAVE/LOAD CURRENT STATE SLOT**. These additional keyboard bindings start
unassigned. State commands use the currently selected save-state slot.

The default controller GUI combination is Start+Select on desktop builds and
L1+R1+Start on RGB30 builds. RGB30 also defaults Quit to Start+Select. Both can be
changed here. The GUI gamepad-enable preference still controls whether a
controller can open/close the GUI. The fixed controller combination used to cancel
gameplay button learning remains available in the controller-mapping screen.

Actions omitted by build flags are labeled disabled; their bindings are retained
for configurations that include those features. Shortcuts remain available in
lean gameplay builds. Without MicroUI, edit the `ShortcutN` entries in `config.cfg`;
each entry has a comment naming its action and stores four comma-separated values:
SDL keycode, normalized modifier mask (Ctrl=1, Shift=2, Alt=4, GUI=8), controller
button mask, and controller slot (0–7, or 8 for any). Zero keycode/button mask
means unassigned. Existing configs without these entries receive defaults.

## GUI and guest keyboard input

The shortcut editor pauses gameplay, even when the normal pause-on-menu option
is off. Keys and controller presses used during capture do not execute emulator
commands. Other menus and text fields own keyboard input while open. Controller
commands run during gameplay; opening/closing the GUI is also available while
menus are open. Closing a menu using a controller binding waits for its buttons
to be released before GUI navigation resumes.

A guest keyboard dongle retains priority over ordinary host keyboard shortcuts.
**Left Ctrl+F1** remains its fixed emergency escape, and Escape still cancels
capture or closes GUI windows. Window-manager commands such as the window close
button remain available independently of these bindings.

## Regression check

`make TSYS=linux -f tests/preferences/Makefile shortcuts-test` covers keyboard
modifier matching and repeats, controller combination precedence and release
behavior, GUI capture/Apply/Cancel/conflicts, guest keyboard priority, controller
eight routing and gameplay suppression, legacy defaults, and config round-trips.
It uses SDL's dummy video driver and a virtual controller, without a visible
window or live audio output.
