# Types and Moves

> Generated from the game code by `tools/gen_wiki.c`. Don't edit by hand: run the tool again after changing moves or types.

## Contents

- [Types](#types)
- [Type matchups](#type-matchups)
- [Which move types each type can learn](#which-move-types-each-type-can-learn)
- [How echoids learn moves](#how-echoids-learn-moves)
- [All moves](#all-moves)
- [Species and their moves](#species-and-their-moves)

## Types

There are 8 types. Each one beats the next type in this cycle:

**Tide → Ember → Flora → Stone → Spark → Gale → Pulse → Void → Tide**

| Type | Short | Beats | Beaten by | Species |
|---|---|---|---|---|
| Tide | `TID` | Ember | Void | Drizlet, Wavern, Abyssurge |
| Ember | `EMB` | Flora | Tide | Cindrop, Flarix, Pyrovane |
| Flora | `FLO` | Stone | Ember | Sproutle, Thornet, Verdarch |
| Stone | `STN` | Spark | Flora | Pebblit, Graniton, Monolux |
| Spark | `SPK` | Gale | Stone | Zaplet, Voltusk, Arcwyrm |
| Gale | `GAL` | Pulse | Spark | Breezel, Cyclark, Tempestra |
| Pulse | `PLS` | Void | Gale | Pingle, Beaconix, Resonaut |
| Void | `VOI` | Tide | Pulse | Nullpup, Shadebit, Eclipson |

An echoid's type, species, rarity and base stats all come from the MAC address of the WiFi access point it was found on (or, for your starter, your Flipper's Bluetooth MAC). Rarer echoids are more likely to be the later species of their type.

## Type matchups

Damage multiplier when a move of the row's type hits an echoid of the column's type. Every move has a type, so this applies to all attacks.

| Move ↓ / Echoid → | Tide | Ember | Flora | Stone | Spark | Gale | Pulse | Void |
|---|---|---|---|---|---|---|---|---|
| **Tide** | 1× | **2×** | 1× | 1× | 1× | 1× | 1× | ½× |
| **Ember** | ½× | 1× | **2×** | 1× | 1× | 1× | 1× | 1× |
| **Flora** | 1× | ½× | 1× | **2×** | 1× | 1× | 1× | 1× |
| **Stone** | 1× | 1× | ½× | 1× | **2×** | 1× | 1× | 1× |
| **Spark** | 1× | 1× | 1× | ½× | 1× | **2×** | 1× | 1× |
| **Gale** | 1× | 1× | 1× | 1× | ½× | 1× | **2×** | 1× |
| **Pulse** | 1× | 1× | 1× | 1× | 1× | ½× | 1× | **2×** |
| **Void** | **2×** | 1× | 1× | 1× | 1× | 1× | ½× | 1× |

## Which move types each type can learn

Echoids can **never** learn moves of the two *opposed* types: the type they beat and the type that beats them. Of the other types, each echoid only learns the ones in its own learnset (see below).

| Echoid type | TID moves | EMB moves | FLO moves | STN moves | SPK moves | GAL moves | PLS moves | VOI moves |
|---|---|---|---|---|---|---|---|---|
| **Tide** | ✓ learns | ✗ never | · allowed | ✓ learns | ✓ learns | ✓ learns | ✓ learns | ✗ never |
| **Ember** | ✗ never | ✓ learns | ✗ never | · allowed | ✓ learns | ✓ learns | ✓ learns | ✓ learns |
| **Flora** | ✓ learns | ✗ never | ✓ learns | ✗ never | · allowed | ✓ learns | ✓ learns | ✓ learns |
| **Stone** | ✓ learns | ✓ learns | ✗ never | ✓ learns | ✗ never | · allowed | ✓ learns | ✓ learns |
| **Spark** | ✓ learns | ✓ learns | ✓ learns | ✗ never | ✓ learns | ✗ never | · allowed | ✓ learns |
| **Gale** | ✓ learns | ✓ learns | ✓ learns | ✓ learns | ✗ never | ✓ learns | ✗ never | · allowed |
| **Pulse** | · allowed | ✓ learns | ✓ learns | ✓ learns | ✓ learns | ✗ never | ✓ learns | ✗ never |
| **Void** | ✗ never | · allowed | ✓ learns | ✓ learns | ✓ learns | ✓ learns | ✗ never | ✓ learns |

✓ learns = at least one species of this type learns moves of that type · allowed = permitted but not in any current learnset · ✗ never = opposed type

## How echoids learn moves

Echoids learn moves at fixed levels (max level 100) and can use their **4 most recently learned** moves; when a new one is learned, the oldest is forgotten. Later moves are stronger. Most moves are of the echoid's own type, plus two kinds of coverage moves:

- **Counter moves**: the type that beats whatever beats this echoid (for example Tide is beaten by Void, and Void is beaten by Pulse, so Tide echoids learn Pulse moves).
- **Species move**: a type that depends on the species, so two echoids of the same type can cover different weaknesses.

| Level | Learns |
|---|---|
| 1 | Own-type move (power 45, accuracy 100%) |
| 1 | Counter move (power 45, accuracy 100%) |
| 8 | Own-type move (power 55, accuracy 95%) |
| 16 | Species move (power 70, accuracy 90%) |
| 25 | Own-type move (power 70, accuracy 90%) |
| 38 | Own-type move (power 85, accuracy 85%) |
| 52 | Counter move (power 100, accuracy 85%) |
| 66 | Own-type move (power 100, accuracy 85%) |
| 82 | Own-type move (power 120, accuracy 75%) |

## All moves

Each type has 6 moves. *Learned by* lists the types whose echoids can learn that move.

### Tide moves

| Move | Power | Accuracy | Learned by |
|---|---|---|---|
| Splash Jab | 45 | 100% | Tide, Flora |
| Bubble Shot | 55 | 95% | Tide |
| Tidal Lash | 70 | 90% | Tide, Stone (Monolux), Spark (Voltusk), Gale (Breezel) |
| Riptide | 85 | 85% | Tide |
| Maelstrom | 100 | 85% | Tide, Flora |
| Tsunami | 120 | 75% | Tide |

### Ember moves

| Move | Power | Accuracy | Learned by |
|---|---|---|---|
| Cinder Flick | 45 | 100% | Ember, Stone |
| Flame Lick | 55 | 95% | Ember |
| Cinder Burst | 70 | 90% | Ember, Spark (Arcwyrm), Gale (Cyclark), Pulse (Pingle) |
| Blaze Rush | 85 | 85% | Ember |
| Inferno | 100 | 85% | Ember, Stone |
| Supernova | 120 | 75% | Ember |

### Flora moves

| Move | Power | Accuracy | Learned by |
|---|---|---|---|
| Vine Tap | 45 | 100% | Flora, Spark |
| Seed Spit | 55 | 95% | Flora |
| Thorn Volley | 70 | 90% | Flora, Gale (Tempestra), Pulse (Beaconix), Void (Nullpup) |
| Root Crush | 85 | 85% | Flora |
| Bloom Storm | 100 | 85% | Flora, Spark |
| World Tree | 120 | 75% | Flora |

### Stone moves

| Move | Power | Accuracy | Learned by |
|---|---|---|---|
| Pebble Toss | 45 | 100% | Stone, Gale |
| Rock Throw | 55 | 95% | Stone |
| Rock Slam | 70 | 90% | Tide (Drizlet), Stone, Pulse (Resonaut), Void (Shadebit) |
| Quake | 85 | 85% | Stone |
| Landslide | 100 | 85% | Stone, Gale |
| Tectonic | 120 | 75% | Stone |

### Spark moves

| Move | Power | Accuracy | Learned by |
|---|---|---|---|
| Zap | 45 | 100% | Spark, Pulse |
| Shock Dart | 55 | 95% | Spark |
| Arc Bolt | 70 | 90% | Tide (Wavern), Ember (Cindrop), Spark, Void (Eclipson) |
| Thunderclap | 85 | 85% | Spark |
| Plasma Lance | 100 | 85% | Spark, Pulse |
| Megavolt | 120 | 75% | Spark |

### Gale moves

| Move | Power | Accuracy | Learned by |
|---|---|---|---|
| Breeze | 45 | 100% | Gale, Void |
| Air Slice | 55 | 95% | Gale |
| Gust Blade | 70 | 90% | Tide (Abyssurge), Ember (Flarix), Flora (Sproutle), Gale |
| Cyclone | 85 | 85% | Gale |
| Tempest | 100 | 85% | Gale, Void |
| Jetstream | 120 | 75% | Gale |

### Pulse moves

| Move | Power | Accuracy | Learned by |
|---|---|---|---|
| Ping | 45 | 100% | Tide, Pulse |
| Echo Beam | 55 | 95% | Pulse |
| Ping Wave | 70 | 90% | Ember (Pyrovane), Flora (Thornet), Stone (Pebblit), Pulse |
| Resonance | 85 | 85% | Pulse |
| Feedback | 100 | 85% | Tide, Pulse |
| Sonic Rupture | 120 | 75% | Pulse |

### Void moves

| Move | Power | Accuracy | Learned by |
|---|---|---|---|
| Shade Nip | 45 | 100% | Ember, Void |
| Null Bite | 55 | 95% | Void |
| Gloom Claw | 70 | 90% | Flora (Verdarch), Stone (Graniton), Spark (Zaplet), Void |
| Eclipse Fang | 85 | 85% | Void |
| Abyss Gaze | 100 | 85% | Ember, Void |
| Event Horizon | 120 | 75% | Void |

## Species and their moves

The full learnset of every species. Moves of another type are marked with their type.

### Tide

Can't learn: Ember or Void moves.

| Level | Drizlet | Wavern | Abyssurge |
|---|---|---|---|
| 1 | Splash Jab | Splash Jab | Splash Jab |
| 1 | Ping *(Pulse)* | Ping *(Pulse)* | Ping *(Pulse)* |
| 8 | Bubble Shot | Bubble Shot | Bubble Shot |
| 16 | Rock Slam *(Stone)* | Arc Bolt *(Spark)* | Gust Blade *(Gale)* |
| 25 | Tidal Lash | Tidal Lash | Tidal Lash |
| 38 | Riptide | Riptide | Riptide |
| 52 | Feedback *(Pulse)* | Feedback *(Pulse)* | Feedback *(Pulse)* |
| 66 | Maelstrom | Maelstrom | Maelstrom |
| 82 | Tsunami | Tsunami | Tsunami |

### Ember

Can't learn: Flora or Tide moves.

| Level | Cindrop | Flarix | Pyrovane |
|---|---|---|---|
| 1 | Cinder Flick | Cinder Flick | Cinder Flick |
| 1 | Shade Nip *(Void)* | Shade Nip *(Void)* | Shade Nip *(Void)* |
| 8 | Flame Lick | Flame Lick | Flame Lick |
| 16 | Arc Bolt *(Spark)* | Gust Blade *(Gale)* | Ping Wave *(Pulse)* |
| 25 | Cinder Burst | Cinder Burst | Cinder Burst |
| 38 | Blaze Rush | Blaze Rush | Blaze Rush |
| 52 | Abyss Gaze *(Void)* | Abyss Gaze *(Void)* | Abyss Gaze *(Void)* |
| 66 | Inferno | Inferno | Inferno |
| 82 | Supernova | Supernova | Supernova |

### Flora

Can't learn: Stone or Ember moves.

| Level | Sproutle | Thornet | Verdarch |
|---|---|---|---|
| 1 | Vine Tap | Vine Tap | Vine Tap |
| 1 | Splash Jab *(Tide)* | Splash Jab *(Tide)* | Splash Jab *(Tide)* |
| 8 | Seed Spit | Seed Spit | Seed Spit |
| 16 | Gust Blade *(Gale)* | Ping Wave *(Pulse)* | Gloom Claw *(Void)* |
| 25 | Thorn Volley | Thorn Volley | Thorn Volley |
| 38 | Root Crush | Root Crush | Root Crush |
| 52 | Maelstrom *(Tide)* | Maelstrom *(Tide)* | Maelstrom *(Tide)* |
| 66 | Bloom Storm | Bloom Storm | Bloom Storm |
| 82 | World Tree | World Tree | World Tree |

### Stone

Can't learn: Spark or Flora moves.

| Level | Pebblit | Graniton | Monolux |
|---|---|---|---|
| 1 | Pebble Toss | Pebble Toss | Pebble Toss |
| 1 | Cinder Flick *(Ember)* | Cinder Flick *(Ember)* | Cinder Flick *(Ember)* |
| 8 | Rock Throw | Rock Throw | Rock Throw |
| 16 | Ping Wave *(Pulse)* | Gloom Claw *(Void)* | Tidal Lash *(Tide)* |
| 25 | Rock Slam | Rock Slam | Rock Slam |
| 38 | Quake | Quake | Quake |
| 52 | Inferno *(Ember)* | Inferno *(Ember)* | Inferno *(Ember)* |
| 66 | Landslide | Landslide | Landslide |
| 82 | Tectonic | Tectonic | Tectonic |

### Spark

Can't learn: Gale or Stone moves.

| Level | Zaplet | Voltusk | Arcwyrm |
|---|---|---|---|
| 1 | Zap | Zap | Zap |
| 1 | Vine Tap *(Flora)* | Vine Tap *(Flora)* | Vine Tap *(Flora)* |
| 8 | Shock Dart | Shock Dart | Shock Dart |
| 16 | Gloom Claw *(Void)* | Tidal Lash *(Tide)* | Cinder Burst *(Ember)* |
| 25 | Arc Bolt | Arc Bolt | Arc Bolt |
| 38 | Thunderclap | Thunderclap | Thunderclap |
| 52 | Bloom Storm *(Flora)* | Bloom Storm *(Flora)* | Bloom Storm *(Flora)* |
| 66 | Plasma Lance | Plasma Lance | Plasma Lance |
| 82 | Megavolt | Megavolt | Megavolt |

### Gale

Can't learn: Pulse or Spark moves.

| Level | Breezel | Cyclark | Tempestra |
|---|---|---|---|
| 1 | Breeze | Breeze | Breeze |
| 1 | Pebble Toss *(Stone)* | Pebble Toss *(Stone)* | Pebble Toss *(Stone)* |
| 8 | Air Slice | Air Slice | Air Slice |
| 16 | Tidal Lash *(Tide)* | Cinder Burst *(Ember)* | Thorn Volley *(Flora)* |
| 25 | Gust Blade | Gust Blade | Gust Blade |
| 38 | Cyclone | Cyclone | Cyclone |
| 52 | Landslide *(Stone)* | Landslide *(Stone)* | Landslide *(Stone)* |
| 66 | Tempest | Tempest | Tempest |
| 82 | Jetstream | Jetstream | Jetstream |

### Pulse

Can't learn: Void or Gale moves.

| Level | Pingle | Beaconix | Resonaut |
|---|---|---|---|
| 1 | Ping | Ping | Ping |
| 1 | Zap *(Spark)* | Zap *(Spark)* | Zap *(Spark)* |
| 8 | Echo Beam | Echo Beam | Echo Beam |
| 16 | Cinder Burst *(Ember)* | Thorn Volley *(Flora)* | Rock Slam *(Stone)* |
| 25 | Ping Wave | Ping Wave | Ping Wave |
| 38 | Resonance | Resonance | Resonance |
| 52 | Plasma Lance *(Spark)* | Plasma Lance *(Spark)* | Plasma Lance *(Spark)* |
| 66 | Feedback | Feedback | Feedback |
| 82 | Sonic Rupture | Sonic Rupture | Sonic Rupture |

### Void

Can't learn: Tide or Pulse moves.

| Level | Nullpup | Shadebit | Eclipson |
|---|---|---|---|
| 1 | Shade Nip | Shade Nip | Shade Nip |
| 1 | Breeze *(Gale)* | Breeze *(Gale)* | Breeze *(Gale)* |
| 8 | Null Bite | Null Bite | Null Bite |
| 16 | Thorn Volley *(Flora)* | Rock Slam *(Stone)* | Arc Bolt *(Spark)* |
| 25 | Gloom Claw | Gloom Claw | Gloom Claw |
| 38 | Eclipse Fang | Eclipse Fang | Eclipse Fang |
| 52 | Tempest *(Gale)* | Tempest *(Gale)* | Tempest *(Gale)* |
| 66 | Abyss Gaze | Abyss Gaze | Abyss Gaze |
| 82 | Event Horizon | Event Horizon | Event Horizon |

