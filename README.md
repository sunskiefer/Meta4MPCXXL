# Meta4msXXL

**Meta4msXXL** and **Meta4msXXL FX** play 4ms MetaModule patches on the Akai Force (MPC OS VST2, armv7).

The 4ms MetaModule brings VCV Rack patches to a standalone Eurorack module; Meta4msXXL brings them to the Force. You
build a patch in VCV Rack around the MetaModule hub, export it (or send it over Wi-Fi), and play it on a Force track
with every parameter of every module on the Q-Links.

The patch player is 4ms's own code (their open-source firmware's patch player, the code their headless simulator
runs), so a patch plays the way it does on the MetaModule. 215 modules are built in: 4ms, Befaco, Mutable Instruments
(AudibleInstruments), HetrickCV, Nonlinear Circuits, Valley, eightfold and VCV's core modules.

- **Meta4msXXL** is an instrument: pads, keyboard, the track's sequence and MIDI go into the patch.
- **Meta4msXXL FX** is an effect: the track's audio goes into the hub's In 1 / In 2, and MIDI still reaches the patch.

Vendor: L'Cronx. Licence: GPL-3.0 (the 4ms code it is built on is GPL-3.0).

## Pages and Q-Links

Every parameter is reachable, in pages of eight on the Force's first Q-Link bank:

1. the patch's **knob sets** first (the knobs you mapped on the hub, with the names you gave them; one knob can move
   several parameters, as on the module),
2. then **every module** in the patch, every parameter it has, in the module's own order ("BRAIDS 1/2", ...).

Knob names and values come from the module itself ("62.0Hz", "Saw", "35%"). Q-Link 9 turns the page; the tabs and
the arrows next to the page number do too. Each Q-Link slot has its own knob colour, so knob 3 is always yellow.

## Getting patches onto the Force

The plugin lists the factory patches it ships with, plus every `.yml` patch in a folder named **Meta4msXXL** at the
top of the Force's internal storage, an SD card or a USB drive (subfolders are fine). Three ways to get them there:

1. **Wi-Fi from VCV Rack.** On the SETUP page switch **Wi-Fi Receive** on; it shows an address like
   `http://192.168.1.40:8045`. Type that into the MetaModule hub's Wi-Fi address in VCV Rack and press the hub's
   Wi-Fi send button: the patch is saved into the Meta4msXXL folder (internal storage, or the card if the hub is set
   to Card/USB) and loads at once. Off by default; it only accepts patch uploads, up to 1 MB.
2. **USB from a computer**: copy the files into the Meta4msXXL folder, then press **RESCAN** on the SETUP page.
3. **SD card / USB stick** with a Meta4msXXL folder on it, then RESCAN.

A project remembers its patch by name and every parameter you changed, and finds it again even when more patches
have been added.

## MIDI, clock and CV

- **MIDI from the track** reaches the hub's MIDI sources exactly as on the MetaModule: notes, gates, velocity,
  aftertouch, pitch bend, CCs, and the patch's MIDI maps. The Force's tempo and play/stop arrive as MIDI clock,
  start and stop, so clocked patches follow the Force.
- **MIDI to hub inputs** (SETUP page), for patches made to be played from CV and gates:
  - *Keys* (default): Gate In 1 = gate while a note is held, Gate In 2 = a trigger on every note, In 3 = pitch
    (1 V/oct, 0 V = C4), In 4 = velocity.
  - *Pads*: notes 36-43 (the first pad row) gate Gate In 1, Gate In 2, In 3, In 4, In 5, In 6, In 1, In 2
    (the last two on the instrument only).
- **CV out** (SETUP page): the patch's Out 3-8 go out on a MIDI port the plugin opens, named "Meta4msXXL"
  ("CV Out"). *Notes + CC*: Out 3 pitch + Out 4 gate as notes, Out 5-8 as CC 16-19. *CC only*: Out 3-8 as CC 16-21.
  In MPC's MIDI preferences enable the port, then point a CV track at it to drive the Force's CV/Gate jacks.
- Out 1 / Out 2 are the plugin's stereo audio (a patch using only one of them plays it on both sides).

## Building

`./build.sh` builds both plugins for the Force (Zig cross-compiler, no Docker) into `build/package/`.
`test/run_tests.sh` runs the offline tests on x86: every factory patch loaded, played and every knob of every page
moved, a state round trip, a Wi-Fi upload as the hub sends it, and the mpc-vst-plugins wrapper's host test on both
plugins. See `TESTING.md`.

Built on [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (submodule, with the Q-Link/automation
UpdateDisplay fix #486-#489). 4ms code: see `third_party/metamodule/VENDORED.md`. Fonts: Saira Stencil One (the name
on the panel) and Titillium Web, SIL OFL 1.1 (`art/fonts`).

Not affiliated with or endorsed by 4ms Company; "MetaModule" is used only to say which patch format this plays.
