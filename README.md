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
  Devboard uses. You can switch to LPUART on pins 15 and 16 in Settings.
- The baud rate is 115200.
- The app turns on 5V on the GPIO header while scanning and turns it off again when
  you leave Collect mode.

The app sends Marauder's `scanap` and `sniffbt` commands, switching between them
(20 s of WiFi, then 12 s of Bluetooth), with `stopscan` in between. It reads the
MAC addresses from Marauder's output.

If Marauder prints a Bluetooth device's name but not its MAC, the app makes a stable
ID from the name, so that device still always drops the same shard.

Custom firmware can also send this simple line format:

```
AP,aa:bb:cc:dd:ee:ff,-60,MyNetwork
BT,11:22:33:44:55:66,-70,MyHeadphones
```

**No board?** Set **Settings → Source → Demo**. The app then makes simulated sightings
from a fixed pool of fake devices, so you can play and test without hardware.

## How to play

| Screen | Controls |
|---|---|
| **Collect** | Starts scanning. Nearby access points are listed with their level, type and signal strength. `*` = already in your team, `x` = already fought this session. **OK** battles the selected echoid. **Back** stops scanning. Bluetooth shards are collected automatically and shown in a popup. |
| **Battle** | **←/→** choose an action, **OK** confirms. **Hit** is a plain attack. **Skill** is a stronger, less accurate attack that uses type matchups. **Ball** opens the throw menu, which shows each ball's catch chance. **Run** tries to escape. |
| **Team** | Lists your echoids. **OK** opens one's details, **←/→** browses, and **OK** on the details screen makes it your lead (the echoid that fights). |
| **Craft** | `+` marks recipes you can afford. **OK** crafts one. |
| **Bag** | **←/→** switches between Items and Shards. **OK** uses a Tonic or Echo Crystal on your lead. |
| **Settings** | Scan source, UART pins, Bluetooth on/off, an in-app guide, and reset. |

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
