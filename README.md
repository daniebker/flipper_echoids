# Echoids

A creature collecting game for the **Flipper Zero**. You catch creatures that hide in
nearby WiFi access points and craft gear from shards that Bluetooth devices drop.

- **WiFi access points are wild echoids.** An access point's MAC address (BSSID) decides
  the creature's species, type, rarity, base stats and pixel sprite. The same MAC
  always gives the same echoid.
- **Bluetooth devices drop shards.** A device's MAC decides the shard's element and tier,
  so the same device always drops the same shard. After a device drops its shard it
  has to recharge before it drops it again (see [Shard cooldowns](#shard-cooldowns)).
- **Shards are crafted** into Echo Balls and other items.
- **Echo Balls catch echoids.** Weaken a wild echoid in battle, then throw a ball.
- **Your first echoid is born from your own Flipper.** On first launch you get a level 1
  echoid generated from your Flipper's Bluetooth MAC address.
- Your echoids **level up** (to a maximum of **100**) when they win battles and catch other
  echoids, and **learn new moves** as they go.

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
| **Collect** | Starts scanning. The top right shows how many echoids you've spotted (ball icon) and how many shards you've collected (diamond icon) since opening the app. Nearby access points are listed as echoids, showing species, level, type and signal strength. The panel at the bottom shows the selected echoid's rarity, type and stats (HP, ATK, DEF, SPD) at the level it would battle at. `*` = already caught (in your team or PAC), `x` = already fought this session. **OK** battles the selected echoid. **Back** stops scanning. Bluetooth shards are collected automatically and shown in a popup. |
| **Battle** | **←/→** choose **Hit**, **Swap**, **Ball** or **Run**, and **OK** confirms. **Hit** opens the fighting echoid's moves (up to 4): **↑/↓** picks one and **OK** uses it. Each row shows the move's name, `x2` / `x.5` if the type matchup helps or hurts against this wild echoid, the move's type and its power. **Swap** sends in another team member (`>` marks the one fighting). Switching uses your turn, so the wild echoid gets an attack on the newcomer. If your echoid faints and anyone on your team can still fight, you pick who goes next instead of losing; the battle is only lost when the whole team has fainted. A battle starts with your lead, or with the first healthy team member if the lead has fainted. XP is shared: see [Catching and levelling](#catching-and-levelling). **Ball** opens the throw menu, which shows each ball's catch chance. **Run** tries to escape. |
| **Team** | Two tabs, switched with **←/→**: **Team** (up to 6 echoids you carry) and **PAC** (Cold PAC Storage for the rest). **OK** opens an echoid's details, where **←/→** browses and **OK** opens its actions. For a team member: **Set as lead** (the echoid that fights) or **Send to PAC**. For a PAC echoid: **Move to team**. If the team is full, you pick which team member to swap out. The team always keeps at least one echoid. |
| **PAC** | Opens **Cold PAC Storage** straight away: browse every echoid you've stored with **↑/↓**, and press **OK** for its details and **Move to team**. The main menu shows how many echoids are stored. It's the same screen as Team's PAC tab, so **←/→** switches to the team. |
| **Craft** | `+` marks recipes you can afford. **OK** crafts one. |
| **Bag** | **←/→** switches between Items and Shards. **OK** on a Tonic or Echo Crystal opens your team list (PAC echoids aren't listed): pick the echoid to use it on with **↑/↓** and press **OK** (**Back** cancels). The list starts on your lead. |
| **Settings** | Board setup (see below), an in-app guide, and reset. |

### Types

Moves do 2× damage to the next type in the cycle and ½ damage to the type before it:

`Tide → Ember → Flora → Stone → Spark → Gale → Pulse → Void → Tide`

### Shards and crafting

Shards come in 8 elements (the same as the creature types) and three tiers:
**Dull** (70%), **Bright** (25%) and **Prismatic** (5%).

| Item | Recipe | Effect |
|---|---|---|
| Echo Ball | 3 Dull shards | Catch rate ×1.0 |
| Great Ball | 2 Bright + 1 Dull | Catch rate ×1.5 |
| Prism Ball | 1 Prismatic + 2 Bright | Catch rate ×2.5 |
| Tonic | 2 Tide + 1 Flora (any tier) | Fully heals the echoid you choose, including a fainted one |
| Echo Crystal | 5 shards of any kind | Gives the echoid you choose half a level of XP |
| PAC | 3 Dull + 2 Bright | Room for 10 more echoids in storage (no limit on PACs) |

### Shard cooldowns

Once a Bluetooth device has dropped its shard, it won't drop it again until its
cooldown has passed. Closing and reopening Collect, or the whole app, doesn't reset it.
If you pass a device that's still recharging, a popup shows how long is left, for example
`Bright Tide: 42m left` (Prismatic is shortened to `Prism` to fit). Each device's popup appears
once per Collect visit and never covers a `+1 … shard` popup.
Rarer shards take longer to recharge:

| Shard tier | Cooldown |
|---|---|
| Dull | 15 minutes |
| Bright | 1 hour |
| Prismatic | 4 hours |

Cooldowns use the Flipper's clock and are stored in the save file. The app remembers
the last 256 devices; when that list is full, the device collected longest ago is
dropped from it. If the clock is set backwards, affected cooldowns start again rather
than ending early.

When a recipe accepts any element, the app uses shards from the element you have the
most of. Cheaper tiers are used first.

### Team and PAC

- You carry up to **6** echoids in your team. Your lead is always one of them.
- New catches join the team while it has room. After that they go to the **PAC**, and the
  battle log says `Team full: sent to PAC`.
- Storage comes in **PACs** of 10 echoids each, and you craft more in **Craft**
  (3 Dull + 2 Bright shards). You start with one PAC, so you can own 6 + 10 = **16**
  echoids. Each new PAC adds 10, and there's **no limit** on how many PACs you craft.
- PAC echoids are stored on the SD card in `/ext/apps_data/echoids/pac.bin`, so the only
  real limit is free space on the card (each echoid takes 30 bytes).
- When the team and every PAC are full you can't throw balls, and the battle log says
  `No room: craft a PAC!`. The PAC tab shows how full your storage is (for example
  `12/20`).
- The PAC heals: an echoid that comes out of the PAC (**Move to team** or a swap) has full HP.
  Only team members regain HP over time.
- Older saves are upgraded automatically. A copy of the old file is kept as
  `save_old_version.bin` in the same folder. Your lead and up to five others stay in the
  team and the rest move to the PAC file. Saves from before PACs could be crafted get 4 PACs
  (room for 40 in storage), and you always get enough PACs to hold everything you own.

### Moves

The **[Types and Moves wiki page](docs/wiki/Types-and-Moves.md)** lists every type, the full
matchup chart, every move, which types can learn it, and each species' learnset.

Every move has a type, and the [type chart](#types) applies to all of them: a move does
2× damage to the type it beats and ½ damage to the type that beats it.

An echoid can **never learn moves of its two opposed types**: the type it beats and the type
that beats it (a Tide echoid never learns Ember or Void moves). Its Moves page says which.

Every echoid learns moves as it levels up and can use its **4 most recently learned** moves.
Later moves are stronger, so when a fifth is learned the oldest one is forgotten. Most moves
are of the echoid's own type. Two are **coverage moves** of other types:

- a **counter move**, of the type that beats whatever beats this echoid. For example, Tide
  is beaten by Void, and Void is beaten by Pulse, so Tide echoids learn Pulse moves;
- a **species move**, whose type depends on the species, so two echoids of the same type
  can cover different weaknesses.

| Level | Move learned | Power / accuracy |
|---|---|---|
| 1 | 1st own-type move (e.g. Splash Jab, Zap, Ping) | 45 / 100% |
| 1 | 1st counter move | 45 / 100% |
| 8 | 2nd own-type move | 55 / 95% |
| 16 | Species move | 70 / 90% |
| 25 | 3rd own-type move (e.g. Tidal Lash, Arc Bolt) | 70 / 90% |
| 38 | 4th own-type move | 85 / 85% |
| 52 | Strong counter move | 100 / 85% |
| 66 | 5th own-type move | 100 / 85% |
| 82 | Final own-type move (e.g. Tsunami, Megavolt, Event Horizon) | 120 / 75% |

The battle log shows `Learned …!` when a level-up teaches a move. An Echo Crystal can teach
one too. On an echoid's details screen, **↑/↓** switches to its **Moves** page, which lists its
current moves and the next one it will learn. Wild echoids use moves the same way, and they
usually pick the one that hurts your echoid most.

### Catching and levelling

- The catch chance depends on rarity (Common 60%, down to Legendary 8%), the ball, and
  how low the wild echoid's HP is.
- Wild echoids keep the species, type and stats their MAC gives them. Their **level** is
  set near your lead's level, from 2 below to 2 above, so encounters stay fair.
- Winning or catching earns XP, and catching gives 1.5× the XP of a win. The XP is
  **shared evenly by every echoid that fought** in the battle: the one that started, and
  every echoid you switched in, including any that fainted. Each gets at least 1 XP.
- After a battle that earned XP, pressing **OK** opens a **Results** screen. It lists each
  echoid that fought with its share of XP, any level-ups (for example `Lv12>13`) and any
  moves it learned. **↑/↓** scrolls, and **OK** goes back to Collect.
- HP comes back at 1 point every 5 seconds outside battle. A Tonic fully heals whichever echoid you choose.

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
| Nothing at all is received, and the app keeps re-sending `scanall` | The board isn't running. After flashing, ESP32 boards often stay in their flashing bootloader until reset. Unplug all power, take off any IO0–GND jumper, then press the board's RST/EN button while the UART terminal is open. Also check **5V power** isn't set to **Don't touch** unless the board has its own power. |
| Log shows `boot:0x3 (DOWNLOAD_BOOT…)` / `waiting for download` (app shows **Board is in flash mode!**) | IO0 is held low (BOOT button or jumper). Release it and press RST. |
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

### Wiki page

`docs/wiki/Types-and-Moves.md` is generated from the game's tables. After changing moves or
types, regenerate it (CI fails if it's out of date):

```bash
cc -std=c11 -Isrc src/game.c tools/gen_wiki.c -o gen_wiki && ./gen_wiki > docs/wiki/Types-and-Moves.md
```

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
src/save.[ch]          Save file (team, items, settings) and upgrades from old versions
src/pac_store.[ch]     PAC storage file on the SD card (unlimited stored echoids)
tests/test_game.c      Host-side unit tests
tools/gen_wiki.c       Generates docs/wiki/Types-and-Moves.md from the game tables
docs/wiki/             Wiki pages (Types and Moves)
```
