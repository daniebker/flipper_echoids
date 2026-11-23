# Echoids

A creature collecting game for the **Flipper Zero**. You catch creatures that hide in
nearby WiFi access points and craft gear from shards that Bluetooth devices drop.

- **WiFi access points are wild echoids.** An access point's MAC address (BSSID) decides
  the creature's species, type, rarity, base stats and pixel sprite. The same MAC
  always gives the same echoid.
- **Bluetooth devices drop shards.** A device's MAC decides the shard's element and tier,
  so the same device always drops the same shard. Each device gives its shard once.
- **Shards are crafted** into Echo Balls and other items.
- **Echo Balls catch echoids.** Weaken a wild echoid in battle, then throw a ball.
- **Your first echoid is born from your own Flipper.** On first launch you get a level 1
  echoid generated from your Flipper's Bluetooth MAC address.
- Your echoids **level up** when they win battles and catch other echoids.

## Hardware

The Flipper Zero has no WiFi radio, and its stock firmware can't scan for Bluetooth
devices. Echoids gets both from an ESP32 board on the GPIO header that runs
[ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder):

| Board | WiFi echoids | Bluetooth shards |
|---|---|---|
| Official WiFi Devboard (ESP32-S2) | ✅ | ❌ The ESP32-S2 has no Bluetooth |
| ESP32 / ESP32-S3 boards (e.g. WROOM-based Marauder boards) | ✅ | ✅ |

- The default UART is **USART on pins 13 (TX) and 14 (RX)**, which is what the WiFi
  Devboard uses. You can switch to LPUART on pins 15 and 16 in **Settings → Board setup**.
- The default baud rate is 115200. You can change it in Board setup.
- The app turns on 5V on the GPIO header while scanning and turns it off again when
  you leave Collect mode.

The app sends Marauder's `scanall` and `sniffbt` commands, switching between them
(20 s of WiFi, then 12 s of Bluetooth), with `stopscan` in between. It reads the
MAC addresses from Marauder's output, which looks like this:

```
-61 Ch: 6 aa:bb:cc:dd:ee:ff ESSID: MyNetwork 31 04
-71 Device: JBL Flip 5
-68 Device: 5c:f3:70:aa:bb:cc
```

Current Marauder firmware no longer has a `scanap` command. If your board runs older
Marauder firmware that still has it, choose it under **Board setup → WiFi cmd**. If the
board answers `Bluetooth not supported`, the app stops trying Bluetooth scans until you
leave Collect mode.

If Marauder prints a Bluetooth device's name but not its MAC, the app makes a stable
ID from the name, so that device still always drops the same shard.

Custom firmware can also send this simple line format:

```
AP,aa:bb:cc:dd:ee:ff,-60,MyNetwork
BT,11:22:33:44:55:66,-70,MyHeadphones
```

**No board?** Set **Settings → Board setup → Source → Demo**. The app then makes simulated sightings
from a fixed pool of fake devices, so you can play and test without hardware.

## How to play

| Screen | Controls |
|---|---|
| **Collect** | Starts scanning. The top right shows how many echoids you've spotted (ball icon) and how many shards you've collected (diamond icon) since opening the app. Nearby access points are listed with their level, type and signal strength. `*` = already in your team, `x` = already fought this session. **OK** battles the selected echoid. **Back** stops scanning. Bluetooth shards are collected automatically and shown in a popup. |
| **Battle** | **←/→** choose an action, **OK** confirms. **Hit** is a plain attack. **Skill** is a stronger, less accurate attack that uses type matchups. **Ball** opens the throw menu, which shows each ball's catch chance. **Run** tries to escape. |
| **Team** | Lists your echoids. **OK** opens one's details, **←/→** browses, and **OK** on the details screen makes it your lead (the echoid that fights). |
| **Craft** | `+` marks recipes you can afford. **OK** crafts one. |
| **Bag** | **←/→** switches between Items and Shards. **OK** uses a Tonic or Echo Crystal on your lead. |
| **Settings** | Board setup (see below), an in-app guide, and reset. |

### Types

Skills do 2× damage to the next type in the cycle and ½ damage to the type before it:

`Tide → Ember → Flora → Stone → Spark → Gale → Pulse → Void → Tide`

### Shards and crafting

Shards come in 8 elements (the same as the creature types) and three tiers:
**Dull** (70%), **Bright** (25%) and **Prismatic** (5%).

| Item | Recipe | Effect |
|---|---|---|
| Echo Ball | 3 Dull shards | Catch rate ×1.0 |
| Great Ball | 2 Bright + 1 Dull | Catch rate ×1.5 |
| Prism Ball | 1 Prismatic + 2 Bright | Catch rate ×2.5 |
| Tonic | 2 Tide + 1 Flora (any tier) | Fully heals your lead, including a fainted lead |
| Echo Crystal | 5 shards of any kind | Gives your lead half a level of XP |

When a recipe accepts any element, the app uses shards from the element you have the
most of. Cheaper tiers are used first.

### Catching and levelling

- The catch chance depends on rarity (Common 60%, down to Legendary 8%), the ball, and
  how low the wild echoid's HP is.
- Wild echoids keep the species, type and stats their MAC gives them. Their **level** is
  set near your lead's level, from 2 below to 2 above, so encounters stay fair.
- Winning or catching gives your lead XP. Catching gives 1.5× the XP of a win.
- HP comes back at 1 point every 5 seconds outside battle. A Tonic heals your lead fully.

Progress is saved to `/ext/apps_data/echoids/save.bin`.

## Board setup and troubleshooting

**Settings → Board setup** lists the connection options. Use **←/→** (or **OK**) to change
the selected value. Changes are saved straight away.

| Option | What it does |
|---|---|
| Source | ESP32 board or Demo (simulated) |
| Pins | `USART 13/14` (WiFi Devboard) or `LPUART 15/16` |
| Baud | 115200 (Marauder default), 230400, 460800, 921600, 57600, 38400, 9600 |
| 5V power | **Auto** turns 5V on while scanning and off afterwards. **Keep on** leaves it on. **Don't touch** never changes it (use this if the board has its own USB power) |
| Boot wait | How long to wait after turning on 5V before sending commands (the ESP32 needs about 1–2 s to boot) |
| Line end | `LF` or `CR+LF` after each command |
| WiFi cmd / BT cmd | The Marauder commands used for scanning: `scanall` (current Marauder) or `scanap` (older Marauder), and `sniffbt` |
| Bluetooth scan | Turns the Bluetooth half of the scan cycle on or off |
| Log UART to SD | Appends all raw traffic to and from the board to `/ext/apps_data/echoids/uart_log.txt` (stops at 256 KB). Use this to capture what your board prints |
| **UART terminal** | A live view of the raw UART traffic, for debugging |
| **Auto-calibrate** | Tries every pin and baud combination and saves the one the board answers on |

**UART terminal** controls:

- The header shows the current pins and baud, and whether 5V is on.
- The second line counts bytes received (`rx`), unreadable bytes (`bad`), and the WiFi
  (`ap`) and Bluetooth (`bt`) sightings the parser understood.
- The log shows the last lines received. Lines starting with `>` were sent by the
  Flipper and lines starting with `#` are status messages.
- **←/→** chooses a command (`help`, `scanall`, `sniffbt`, `stopscan`, `scanap`,
  `list -a`, `clearlist -a`, `reboot`) and **OK** sends it.
- **↑** switches the pins and **↓** steps through the baud rates. The UART reopens
  straight away and the change is saved.
- **Hold OK** turns 5V on or off by hand.

When Collect has found nothing yet, the screen shows what's happening instead of a list:
a status line (`Board is silent`, `Garbled data: wrong baud?`, `Board talks, no networks`),
the byte/line/AP/BT counters, and the last line the board sent.

**What the terminal tells you:**

| What you see | Likely cause |
|---|---|
| `rx0` after sending `help` | No connection. Board TX must go to Flipper RX (pin 14 for USART, 16 for LPUART), board RX to Flipper TX, and GND to GND. Also check the pins setting and that the board has power. |
| `bad` keeps rising, log is full of `.` | Wrong baud rate. Press ↓ until the text is readable, or run Auto-calibrate. |
| Readable text, but `ap0` during `scanall` | Marauder prints a format the parser doesn't recognise. Note what the lines look like (please open an issue with an example). |
| Log repeats `ets Jul 29 2019 … rst:0x3 (SW_RESET) … entry 0x4008…` over and over (the app shows **Board keeps rebooting!** / `REBOOT LOOP`) | The ESP32 crashes as soon as its firmware starts, so Marauder never runs. This is a firmware or power problem on the board, not something the app can fix. Power the board from USB and watch it in a serial monitor at 115200: if it still loops, re-flash Marauder using the build made for your exact board (erase the flash first). If it only loops on the Flipper, it isn't getting enough power, so check it's on the 5V pin (1) and GND, or power it from USB and set **5V power → Don't touch**. |
| Works in the terminal, but nothing in Collect | Try **5V power → Keep on** or a longer **Boot wait**. The app now also re-sends the scan command if the board stays silent for 4 seconds. |

Close any other app that uses the GPIO UART (for example the Marauder companion app or
the GPIO USB-UART bridge) before using Echoids.

## Building

With [ufbt](https://github.com/flipperdevices/flipperzero-ufbt):

```bash
ufbt
```

To build and launch on a connected Flipper:

```bash
ufbt launch
```

GitHub Actions also builds the `.fap` for the release and dev firmware channels, and the
built files can be downloaded from each workflow run.

### Tests

The game rules and the Marauder output parser don't depend on the Flipper SDK, so they
can be tested on a PC:

```bash
cc -std=c11 -Wall -Wextra -Isrc src/game.c src/scanner_parse.c tests/test_game.c -o test_game && ./test_game
```

## Project layout

```
application.fam        App manifest
echoids.png            10x10 app icon
src/echoids.c          App entry, screens, input, game loop
src/game.[ch]          Generation from MACs, stats, crafting, battle rules (no SDK)
src/scanner.[ch]       UART link to the ESP32 board, scan cycling, demo source
src/scanner_parse.c    Marauder / CSV line parser (no SDK)
src/save.[ch]          Save file
tests/test_game.c      Host-side unit tests
```
