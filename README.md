English | [Русский](README.ru.md)

# IL-2 Contact VR

Control the switches, buttons and levers of the **IL-2 Sturmovik: Great Battles** cockpit in VR **with your fingers, no controllers**.
The game is not modified: the program tracks your hands through OpenXR, and when a fingertip enters a cockpit zone, it sends the game the key that is already assigned to that action.

> Status: in development. Bugs are possible. Windows 10/11.

https://youtu.be/rRFfpf-cne0

## Features

**Cockpit control**
- Press switches, buttons and levers with a finger: the fingertip enters a zone and the program sends the key assigned in the game.
- The key is held while the finger stays in the zone (flaps, radiator and the like) and is released when the finger leaves, the hand is lost, the game is paused or the program stops.
- Any key combinations, including left and right `Ctrl/Alt/Shift` and the `Win` key.
- Each button has its own zone size (10–80 mm diameter), with an adjustable delay before activation and a delay after a press.

**Aircraft profiles**
- Every aircraft has its own profile: a set of zones and keys. The right profile is selected automatically from the aircraft name in the game's telemetry.
- Pinning zones: point at a button in the cockpit and fix it (hold the finger still or press a key), and the program remembers the zone.
- Aircraft card in the program window: list of zones, editing keys and sizes, "Pin" and "Re-pin", copying zones between aircraft, favorites, search.
- Save and load profiles; share profiles between players.

**Calibration and headset centering**
- The cockpit position is tied to the headset centering (centering key, NUM5 by default, or a joystick button): after centering, the zones snap into place.
- Calibration from three pinned zones and a quick correction by touching a single zone.
- Undo the last pin; voice and sound prompts (Windows voice), so you do not have to look at the screen inside the headset.

**Feedback in the headset**
- A colored marker on the fingertip: the color shows the state (normal, calibration needed, pinning needed, correction).
- A fill ring before a zone fires and while a point is being fixed.
- Showing the zones of the current aircraft in the headset (by a key), so you can see where they are.

**Game menu**
- When there is no telemetry (menu, pause), the index finger moves the mouse cursor over the game window, and holding the finger still makes a click.

**Game state detection**
- In pause and in the menu, cockpit control is switched off, and it resumes on return with no new setup.

**Window and settings**
- Window: tabs "Aircraft", "Profiles", "Settings", "About", the "First steps" guide, tooltips for every parameter.
- Russian and English interface, add your own languages with a `language/*.lang` file, light and dark themes.
- Install and remove the OpenXR layer from the window, edit the game's `startup.cfg` (with a backup copy), check for a new version on GitHub.
- A work log for troubleshooting.

## How it works
```
Headset → OpenXR runtime → IL-2 Contact layer (head and hand poses) → UDP → Python program
                                                                      ↓ zones, gestures, profiles, game telemetry (UDP)
                                                                      ↓ SendInput (scancode)
                                                                    IL-2
```

- A thin OpenXR layer written in C++ (`XR_APILAYER_IL2CONTACT`) captures head and hand poses and draws the marker. The layer contains no logic.
- All the logic is in Python: calibration, zones, gestures, profiles, game state, sending keys, voice.
- A crash of the Python part does not affect the game.

## Requirements
- Windows 10 or 11.
- A VR headset with **hand tracking** that is passed through to the PC (tested: Meta Quest 3 via Virtual Desktop / VDXR; other headsets have not been tested).
- IL-2 Great Battles running through **OpenXR** (for a native OpenVR game, OpenComposite is needed).
- Telemetry enabled in the game's `data/startup.cfg` (the program can fix the file itself).
- Keys assigned in the game to the actions you want.

## Installation and launch
**Ready-made build (recommended).** Download the archive from the Releases section, unpack it and run `IL2_Contact.exe`. The program offers to install the layer itself (Settings → OpenXR layer → Install); administrator permission is required. The "First steps" guide opens on the first launch.

**Quick start**
1. Start the headset and IL-2, enable telemetry in the program settings.
2. On the "Aircraft" tab open the aircraft card, create a profile, add buttons and pin the zones.
3. Press the centering key, check the zones, and if they have shifted, make a correction with a double click of your fingers.

## Limitations
- Hand tracking accuracy is about 1–2 cm: zones should not be too small. There is no tactile feedback; sound and the marker help.
- Hands are lost when one covers the other and during fast movements.
- The layer is not digitally signed, so Windows and antivirus software may show a warning.
- The program does not know the state of a switch in the game: a repeated activation may flip it back.

## Support the project
The program is developed in spare time. If it turned out to be useful, you can [support](https://github.com/Jazz-747/IL2-Contact-VR/blob/main/donate.md) the author. A donation is voluntary and does not affect anything in the program.
