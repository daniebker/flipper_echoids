# Dungeons

Every WiFi network you can connect to hides a dungeon. To enter one you have to **jack in**:
be on the network itself. On the Flipper, the ESP32 board joins it; on Android, you use the
network the phone is already connected to. A dungeon is a row of rooms with a **boss** at the end.

[Home](Home.md) · [Echoids](Echoids.md) · [Types](Types.md) · [Battles](Battles.md) · [Items and Crafting](Items-and-Crafting.md)

## Jacking in

**On the Flipper:**

1. Start Collect and find the network's echoid in the list.
2. Press **OK** on the echoid and choose **Jack in**.
3. Enter the network's password (leave it empty for an open network).
4. Once the board has connected, you're in. Hidden networks can't be joined.

**On Android:**

1. Connect the phone to a WiFi network as usual, in Android's settings.
2. A **Jack in** tab appears at the bottom. It shows that network's dungeon: its name,
   element, rooms and boss.
3. Tap **Jack in**. Leaving the network (or switching to another) throws you out.

Android only tells the game which network you're on with location access, and with location
turned on. Any network works, enterprise and WEP included, since the game doesn't connect.

You need a team member that can still fight.

## One dungeon per network

Everything about a dungeon comes from the network's MAC address, so the same network always
gives the same dungeon:

| | |
|---|---|
| **Name** | The network's own echoid name plus a place for its element, such as *Cindle Forge* |
| **Element** | The type of the network's own echoid |
| **Rooms** | 4 to 6 before the boss |
| **Room types** | The first room is always a fight. After that, each room is a fight, a shard cache (1 in 4) or a rest (at most one per dungeon) |
| **Foes** | Each fight has its own echoid, usually of the dungeon's element (about 4 in 5) |
| **Boss** | The rarest echoid of the dungeon's element found for the boss room |

| Place | Element |
|---|---|
| Reef | Tide |
| Forge | Ember |
| Thicket | Flora |
| Caverns | Stone |
| Grid | Spark |
| Spire | Gale |
| Relay | Pulse |
| Abyss | Void |

## Rooms

| Room | What happens |
|---|---|
| **Fight** | A battle against the room's echoid |
| **Shard cache** | 2 to 4 shards of the dungeon's element (Dull, or 1 in 5 Bright) |
| **Rest** | Every team member that hasn't fainted recovers half its max HP |
| **Boss** | A battle against the boss |

Clear a room, then go deeper to the next one. You can't skip rooms.

## Levels

Dungeon echoids match your team: they fight at your **team's average level**, rounded up.
The boss fights at **3 levels above** it (at most level 100). A team with a lead at level 30 and
five echoids at level 5 meets level 10 foes, so bring a balanced team.

## Inside a dungeon

- **Only your team and bag** come with you. You can't scan, craft, open the PAC or change
  settings. You can use Tonics, Revive Crystals and Echo Crystals between fights, and change
  your lead.
- **No catching:** dungeon echoids can only be fought. The Ball option is closed.
- **No running:** every fight is to the end. Switching works as usual.
- **XP** from wins is shared by everyone who fought, as in any battle.

## Getting thrown out

- **Your whole team faints:** you're ejected when the battle ends.
- **The connection drops** (Android): you're ejected straight away, or at the end of the
  current fight.
- **Jack out** (Android: the Jack out button; Flipper: **Jack out** in the menu, press OK
  twice) leaves by choice.

Leaving any way means the dungeon starts again from the first room next time.

## Rewards

Beating the boss clears the dungeon and gives:

- 1 **Prismatic** shard of the dungeon's element
- 2 **Great Balls**
- 1 **Revive Crystal**

The shards from caches and the XP from fights are yours as soon as you get them, even if
you're ejected later.
