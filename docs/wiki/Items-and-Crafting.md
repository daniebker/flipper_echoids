# Items and Crafting

> Generated from the game code by `tools/gen_wiki.c`. Don't edit by hand: run the tool again after changing the game.

Bluetooth devices drop **shards**, and shards are crafted into **items** in the **Craft** menu.

See also: [Echoids](Echoids.md) · [Battles](Battles.md)

## Contents

- [Shards](#shards)
- [Shard cooldowns](#shard-cooldowns)
- [Recipes](#recipes)
- [Healing and reviving](#healing-and-reviving)
- [Echo Balls](#echo-balls)
- [PACs](#pacs)
- [How shards are spent](#how-shards-are-spent)

## Shards

Every Bluetooth device's MAC address decides the **element** and **tier** of the shard it drops, so the same device always drops the same shard. Elements are the 8 [types](Types.md), all equally common.

| Tier | Chance |
|---|---|
| Dull | 70% |
| Bright | 25% |
| Prismatic | 5% |

The Bag's **Shards** tab lists your shards by element as Dull/Bright/Prismatic.

## Shard cooldowns

After a device drops its shard it has to **recharge** before it drops it again. The cooldown uses the Flipper's clock, so leaving Collect or the app doesn't reset it. Rarer shards take longer:

| Tier | Cooldown |
|---|---|
| Dull | 15m |
| Bright | 1h00m |
| Prismatic | 4h00m |

When you pass a recharging device, Collect shows how long is left (for example `Bright Tide: 42m left`), once per device per visit.

## Recipes

| Item | Recipe | Effect |
|---|---|---|
| Echo Ball | 3 Dull shards | Catch rate ×1.0 |
| Great Ball | 2 Bright + 1 Dull | Catch rate ×1.5 |
| Prism Ball | 1 Prismatic + 2 Bright | Catch rate ×2.5 |
| Tonic | 2 Tide + 1 Flora | Restores 20 HP (not on a fainted echoid) |
| Super Tonic | 2 Tide 2 Flora 1 Bright | Restores 50 HP (not on a fainted echoid) |
| Mega Tonic | 3 Tide 3 Flora 2 Bright | Restores 100 HP (not on a fainted echoid) |
| Max Tonic | 3 Tide 3 Flora 1 Prism | Fully restores HP (not on a fainted echoid) |
| Revive Crystal | 2 Pulse 1 Spark 1 Bright | Brings a fainted echoid back with 1 HP |
| Echo Crystal | 5 shards of any kind | Gives an echoid half the XP it needs for its next level |
| PAC (+10 room) | 3 Dull + 2 Bright shards | Room for 10 more echoids in storage |

**Prism** means a Prismatic shard. Element names (Tide, Flora…) accept that element's shards of any tier, and tier names (Dull, Bright…) accept that tier from any element.

## Healing and reviving

- Open the **Bag**, pick a Tonic, Revive Crystal or Echo Crystal and press **OK**, then choose the team member to use it on. PAC echoids can't be chosen.
- **Tonics don't work on a fainted echoid.** Use a **Revive Crystal** first. It comes back with 1 HP.
- Team members who haven't fainted regain 1 HP every 5 seconds outside battle. Fainted echoids don't regain HP until revived.
- Echoids that come out of the PAC have full HP, fainted or not.

## Echo Balls

Throw balls from the battle menu's **Ball** option. The ball menu shows each ball's catch chance against the echoid you're fighting.

| Ball | Catch rate |
|---|---|
| Echo Ball | ×1.0 |
| Great Ball | ×1.5 |
| Prism Ball | ×2.5 |

Full catch rules: [Battles → Catching](Battles.md#catching).

## PACs

Each PAC stores **10** echoids in Cold PAC Storage, on top of your team of 6. You start with one PAC, and there's **no limit** on how many more you craft. Stored echoids live in `/ext/apps_data/echoids/pac.bin` on the SD card, 30 bytes each.

## How shards are spent

Recipes that name an **element** use that element's cheapest shards first. After that, **tier** requirements take shards of that tier from whichever element you have the most of, and "any" shards take the cheapest. So a Bright shard isn't used up on a "2 Tide" requirement while a Dull Tide shard is available.
