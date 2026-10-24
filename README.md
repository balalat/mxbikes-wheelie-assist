# Wheelie Assist for MX Bikes

Hold clean wheelies without looping out. Wheelie Assist watches your bike's angle in real time and helps you
balance it, using the same three things a real rider uses: **body weight (lean front/back)**, **throttle** and
**rear brake**.

It works with **your own controls**. No special controller profile and no rebinding: it reads your bindings
from the game and only touches those inputs, only while it is actually helping. The rest of the time your
inputs go through untouched.

Tested on MX Bikes beta21 (64-bit).

---

## Features

- **Works with any control setup.** Xbox-style pads, PlayStation pads through Steam Input, plus DirectInput devices
  (keyboard, controllers without Steam Input, wheels, handlebars).
- **Acts like a rider would:**
  - **cuts throttle** as you approach the balance point. It never adds throttle you didn't give.
  - adds **rear brake** to bring the front down when needed. Small corrections come from the brake first, for a
    smoother wheelie
  - leans you **forward** when the front is coming up well too far, and **back** when it's dropping a lot
- **5 assist modes**, switchable while riding:

  | # | Mode | Holds | Feel |
  |---|------|-------|------|
  | 1 | Gentle | 25° | Low wheelies, cuts early and brakes hard. Almost impossible to loop |
  | 2 | Normal | 35° | Default |
  | 3 | High | 50° | Tall wheelies, close to the balance point |
  | 4 | Protection | – | You do everything; it only steps in near 60° to stop a loop-out |
  | 5 | Custom | your values | Set in `wheelie_assist.ini` |

- **Small on-screen gauge** shows your wheelie angle, the target and the cut-off.
- **Off on online servers by default**, so it can't be used in races unless you enable it.

---

## Installation

1. Download `WheelieAssist_v1.1.zip` from the [latest release](https://github.com/balalat/mxbikes-wheelie-assist/releases/latest).
2. Extract the zip into your MX Bikes folder, usually
   `C:\Program Files (x86)\Steam\steamapps\common\MX Bikes`.
   The zip contains a `plugins` folder, so the files land in `MX Bikes\plugins\`:
   - `wheelie_assist64.dlo`
   - `wheelie_assist64.dli`
   - `wheelie_assist.ini`
3. Start the game and ride. That's it.

Make sure **throttle**, **rear brake** and **rider lean front/back** are bound in *Options → Controls*. The assist
works through whatever you bound there.

> Tip: keep the game's **auto rider front/back lean** option **off**. With it on, the game moves the rider by
> itself and the assist loses its most effective tool.

---

## Controls

| Action | Xbox-style pad | Keyboard |
|---|---|---|
| Assist on / off | Left stick click | F6 |
| Next mode (higher) | D-pad up | F8 |
| Previous mode (lower) | D-pad down | F7 |

All of these can be changed in `wheelie_assist.ini` (`[input]` section). If your own bindings already use
one of these buttons, pick another one there.

---

## On-screen gauge

A thin vertical bar on the right edge of the screen:

- **Fill:** your current wheelie angle (0–90°)
- **Yellow tick:** target angle of the current mode
- **Red tick:** cut-off angle (full throttle cut, lean forward and rear brake above it)
- **Yellow squares below the bar:** current mode number (1–5)
- **Colour of the bar and the square above it:**
  - grey = assist off (or disabled on an online server)
  - green = armed
  - orange = assist acting
  - red = none of your throttle / rear brake / lean inputs detected yet

You can move it or hide it in the `[hud]` section of the ini.

---

## How to ride with it

1. 1st or 2nd gear, give it throttle (or clutch it up) to lift the front.
2. Keep the throttle open. The assist trims it, shifts your weight and taps the rear brake to hold the mode's angle.
3. Steer with lean as usual.
4. Close the throttle or tap the front brake to come down.

Start with **Gentle**, then move up to Normal and High as you get used to it.

---

## Configuration (`plugins\wheelie_assist.ini`)

Most players never need to touch it. Useful settings:

| Setting | What it does |
|---|---|
| `enabled` | Assist on or off when the game starts |
| `start_preset` | Mode at start: `gentle`, `normal`, `high`, `protection`, `custom` |
| `allow_online` | `1` = also work on online servers (only where the server allows it) |
| `target_angle`, `max_angle`, ... | Values of the **Custom** mode |
| `lean_invert` | Set to `1` if the assist leans the rider the wrong way with your lean binding |
| `toggle_button`, `toggle_key`, ... | Hotkeys |
| `[log] enabled` | `1` writes ride logs (CSV) to `Documents\PiBoSo\MX Bikes\wheelie_logs` for tuning |
| `[hud]` | Gauge position, size, on/off |

`[assist]` values are re-read every time you go on track. `[input]` values need a game restart.

---

## Good to know

- **Same stick for rear brake and lean.** On the game's default Xbox layout, rear brake and rider lean are on the
  same stick, so the assist can't brake and lean forward at once. In that case lean wins, and the rear brake only
  takes over past the cut-off angle. Putting rear brake on another input (for example left stick down) gives
  the assist more control.
- **Keyboard throttle and brake are on/off keys.** The assist pulses them quickly to approximate partial throttle.
  It works, but an analog trigger is smoother.
- **Online:** the assist is off on servers unless you set `allow_online = 1`. Please respect server and league
  rules: in a race, this is an advantage.

---

## Troubleshooting

`plugins\wheelie_assist.log` records what the mod found at startup: the bindings it read and the devices it saw.

| Problem | Fix |
|---|---|
| No gauge on screen | Check that both `.dlo` and `.dli` files are in `MX Bikes\plugins`. Look in the log for startup lines |
| Gauge is red | Throttle, rear brake or lean F/B isn't bound, or isn't on a device the mod can see. Check *Options → Controls*. The log lists the bindings it read |
| Gauge stays grey | The assist is off. Press the toggle (left stick click / F6). On online servers it stays off unless `allow_online = 1` |
| Rider leans the wrong way | Set `lean_invert = 1` |
| A hotkey clashes with one of my bindings | Change it in `[input]` |

---

## Uninstall

Delete these files from `MX Bikes\plugins`:
`wheelie_assist64.dlo`, `wheelie_assist64.dli`, `wheelie_assist.ini`, `wheelie_assist.log`.
Your controls are never modified, so nothing else needs restoring.

---

## How it works (for the curious)

The mod uses MX Bikes' official plugin interfaces. The telemetry plugin (`.dlo`) reads the bike's pitch and wheel
contact at 100 Hz and works out the wheelie angle relative to the ground slope. The input plugin (`.dli`) reads your
bindings from the active profile's `controls.txt`. It then adjusts only those inputs as the game reads them, from
Xbox-style pads through the game's XInput plugin and from DirectInput devices. The control law predicts where the
nose is heading about 0.3 s ahead. It shifts your weight, caps the throttle and adds rear brake to keep the angle on
target, and it adapts to the bike's power while you ride.

---

## Build from source

The release zip is built from this repository. To build the plugins yourself you need the MSVC 2022 Build Tools:

```
build.bat
```

This writes `wheelie_assist64.dlo` and `wheelie_assist64.dli` to `build\`. Copy them, plus `plugins\wheelie_assist.ini`
(the exact config shipped in the zip), into `MX Bikes\plugins`.

`include/` holds the plugin structures from PiBoSo's official examples
([output plugin](https://www.mx-bikes.com/downloads/mxb_example.c), [input plugin](https://www.kartracing-pro.com/downloads/input_int.c)).

---

## License

The source is public so you can see exactly what the plugin does, and it will keep improving. For now
**redistribution is not allowed**: please don't re-upload or mirror it. Link to this repository or
[bala.lat](https://bala.lat) instead, so everyone gets the latest version. See [LICENSE](LICENSE) for the full terms.
