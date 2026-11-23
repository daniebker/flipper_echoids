# Battles

> Generated from the game code by `tools/gen_wiki.c`. Don't edit by hand: run the tool again after changing the game.

Pick a wild echoid in **Collect** and press **OK** to battle it.

See also: [Types](Types.md) · [Types and Moves](Types-and-Moves.md) · [Echoids](Echoids.md)

## Contents

- [Starting a battle](#starting-a-battle)
- [Your turn](#your-turn)
- [Damage](#damage)
- [Switching and fainting](#switching-and-fainting)
- [Catching](#catching)
- [Running away](#running-away)
- [XP](#xp)

## Starting a battle

- Your **lead** fights first. If it has fainted, the first healthy team member starts instead. If the whole team has fainted, you can't battle.
- The wild echoid's level is set near your lead's: from 2 below to 2 above, plus 1 for Rare and rarer.
- Echoids you catch or beat leave the Collect list for good and never show up again. You can't battle one that has just gone to sleep ([time of day](Echoids.md#time-of-day)).

## Your turn

The battle menu has four actions:

| Action | What it does |
|---|---|
| **Hit** | Pick one of your echoid's moves (up to 4). Each row shows `x2`/`x.5` against this foe, the move's type and its power. |
| **Swap** | Send in another team member. Uses your turn. |
| **Ball** | Throw an Echo Ball to try to catch the echoid. |
| **Run** | Try to escape. |

When you attack, the faster echoid (higher **SPD**) moves first; ties go to you. Wild echoids usually pick the move that does the most damage to your echoid, and sometimes a random one.

## Damage

```
damage = ((2 × level / 5 + 2) × power × ATK / DEF) / 26 + 2
       × type multiplier (2, 1 or ½)
       × random 85–100%
```

- Every move has a type, and the [type chart](Types.md#the-type-matchup-chart) applies. The battle log adds `x2!` or `x.5` to the damage.
- Each move has an **accuracy**. A miss does no damage and shows `missed` in the log.
- An attack always does at least 1 damage.

## Switching and fainting

- **Swap** sends in any healthy team member, not one that has fainted. The wild echoid gets a free attack on the newcomer.
- When your echoid **faints**, you choose who goes next, as long as anyone on your team can still fight. That switch is free. The battle is only lost when the whole team has fainted.
- Fainted echoids stay fainted until a Revive Crystal ([Items and Crafting](Items-and-Crafting.md#healing-and-reviving)).

## Catching

Catch chance = base rate for its rarity × ball multiplier × `(3 × maxHP − 2 × HP) / (3 × maxHP)`, kept between 1% and 95%. A weakened echoid is easier to catch: at full HP the last part is ⅓, and near 0 HP it's almost 1.

| Rarity | Ball | At full HP | At 1 HP |
|---|---|---|---|
| Common | Echo Ball | 20% | 59% |
|  | Great Ball | 30% | 89% |
|  | Prism Ball | 50% | 95% |
| Uncommon | Echo Ball | 15% | 44% |
|  | Great Ball | 22% | 67% |
|  | Prism Ball | 37% | 95% |
| Rare | Echo Ball | 10% | 29% |
|  | Great Ball | 15% | 44% |
|  | Prism Ball | 25% | 74% |
| Epic | Echo Ball | 6% | 17% |
|  | Great Ball | 9% | 26% |
|  | Prism Ball | 15% | 44% |
| Legendary | Echo Ball | 2% | 7% |
|  | Great Ball | 4% | 11% |
|  | Prism Ball | 6% | 19% |

A failed throw lets the wild echoid attack. A caught echoid joins your team, or goes to the PAC when the team is full. If there's no room anywhere, craft a PAC first.

## Running away

Run chance = `50% + 5% × (your SPD − its SPD)`, kept between 20% and 95%. If it fails, the wild echoid attacks. An echoid you ran from stays in the list and can be battled again.

## XP

Winning gives `(8 + 5 × wild level) × (1 + 0.25 × rarity step)` XP, and catching gives 1.5× that:

| Wild level | Common | Uncommon | Rare | Epic | Legendary | Legendary caught |
|---|---|---|---|---|---|---|
| 1 | 13 | 16 | 19 | 22 | 26 | 39 |
| 5 | 33 | 41 | 49 | 57 | 66 | 99 |
| 10 | 58 | 72 | 87 | 101 | 116 | 174 |
| 25 | 133 | 166 | 199 | 232 | 266 | 399 |
| 50 | 258 | 322 | 387 | 451 | 516 | 774 |
| 100 | 508 | 635 | 762 | 889 | 1016 | 1524 |

The XP is **shared evenly by every echoid that fought**: the starter and everyone you switched in, including any that fainted. Each gets at least 1. After the battle, a **Results** screen lists each echoid's XP, level-ups and new moves.
