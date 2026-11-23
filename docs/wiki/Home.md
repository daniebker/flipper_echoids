# Echoids wiki

Echoids is a creature-collecting game for the Flipper Zero. Wild echoids live in WiFi
access points and come out at certain hours of the day. Bluetooth devices drop the shards
you craft with. See the [README](../../README.md) for setup, controls and board
troubleshooting.

## Pages

| Page | What's in it |
|---|---|
| **[Echoids](Echoids.md)** | What the MAC decides, rarity, species, stats, the star rating, levels and XP, names, time of day, team and PAC |
| **[Types](Types.md)** | The 8 types, the type cycle, the matchup chart, strengths and weaknesses, names by type, battle tips |
| **[Types and Moves](Types-and-Moves.md)** | Every move, which types can learn it, and each species' full learnset |
| **[Battles](Battles.md)** | Turn order, damage, switching and fainting, catch chances, running away, XP and sharing |
| **[Items and Crafting](Items-and-Crafting.md)** | Shards and cooldowns, every recipe, tonics and revives, Echo Balls, PACs |
| **[Dungeons](Dungeons.md)** | Jacking in to a WiFi network, rooms and bosses, foe levels, rules inside, rewards |

## Keeping the wiki up to date

Everything except this page and Dungeons is generated from the game code by `tools/gen_wiki.c`, so the
numbers always match the game, and CI fails if a page is out of date. After changing the game,
regenerate the pages:

```bash
cc -std=c11 -Isrc src/game.c tools/gen_wiki.c -o gen_wiki
```

```bash
./gen_wiki types > docs/wiki/Types.md && ./gen_wiki moves > docs/wiki/Types-and-Moves.md && ./gen_wiki echoids > docs/wiki/Echoids.md && ./gen_wiki items > docs/wiki/Items-and-Crafting.md && ./gen_wiki battles > docs/wiki/Battles.md
```

## Publishing to the GitHub wiki

These pages live in `docs/wiki/` in the repository. To also show them on GitHub's **Wiki**
tab, create a first wiki page on github.com, then copy each file in as a page with the same
name: `Home`, `Echoids`, `Types`, `Types-and-Moves`, `Battles`, `Items-and-Crafting` and `Dungeons`.
