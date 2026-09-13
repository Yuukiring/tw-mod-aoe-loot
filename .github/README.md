# tw-mod-aoe-loot

Area-of-effect looting module for Tortoise-WoW.

This module lets players loot multiple nearby corpses by interacting with just one of them. Items and gold from corpses within the configured range are merged into a single loot window.

## Features

- **AOE looting**: loot nearby corpses with a single interaction.
- **Gold merging**: accumulates gold from all corpses into the selected corpse.
- **Quest items**: quest items needed by the player are added directly to inventory.
- **Stacking**: combines identical regular loot items up to the item's stack limit.
- **Group aware**: optional setting to allow or disallow AOE loot while grouped.
- **Configurable range**: server administrators can set the maximum loot radius.

## Requirements

- Tortoise-WoW source tree
- Compiler with C++17 support

## Installation

1. Place or clone the module into your Tortoise-WoW `modules` directory:

   ```bash
   cd <tortoise-wow>/modules
   # module should appear as modules/tw-mod-aoe-loot
   ```

2. Configure and build:

   ```bash
   cd <tortoise-wow>/build
   cmake ../ -DMODULES=static
   cmake --build . --target mangosd
   ```

3. Copy the default config and adjust it:

   ```bash
   cp <tortoise-wow>/modules/tw-mod-aoe-loot/conf/tw-mod-aoe-loot.conf.dist \
      <server-config>/modules/tw-mod-aoe-loot.conf
   ```

## Configuration

Edit `tw-mod-aoe-loot.conf`:

```conf
[AOELoot]

# Enable/disable the module globally
AOELoot.Enable = 1

# Maximum distance (in yards) to collect loot from nearby corpses
AOELoot.Range = 55.0

# Allow AOE looting while in a group
AOELoot.Group = 1

# Show an informational message on player login
AOELoot.Message = 1
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `AOELoot.Enable` | Boolean | 1 | Enable/disable module globally |
| `AOELoot.Range` | Float | 55.0 | Maximum loot collection radius (5.0 - 100.0) |
| `AOELoot.Group` | Boolean | 1 | Allow AOE loot in groups |
| `AOELoot.Message` | Boolean | 1 | Show login message |

## Usage

1. Kill multiple enemies near each other.
2. Right-click any lootable corpse.
3. Loot from nearby corpses appears in the same window.
4. Quest items for your active quests are added to your bags automatically.

## Corpse decay recommendation

For the cleanest experience, set looted corpses to despawn quickly in your `mangosd.conf`:

```conf
Rate.Corpse.Decay.Looted = 0.01
```

## Troubleshooting

### AOE loot is not working

- Verify `AOELoot.Enable = 1` in `tw-mod-aoe-loot.conf`.
- Ensure you are within range of other corpses.
- If grouped, check `AOELoot.Group = 1`.

### Corpses linger after looting

- Set `Rate.Corpse.Decay.Looted = 0.01` in `mangosd.conf`.

## Technical notes

- The module intercepts `CMSG_LOOT` via a `ServerScript` hook.
- It searches for dead, lootable creatures in range, validates tap rights with `Creature::IsTappedBy`, then merges eligible loot into the selected corpse's loot window before opening it.
- There is no SQL or database dependency.

## Credits

- Based on the AzerothCore module [mod-aoe-loot](https://github.com/azerothcore/mod-aoe-loot).
- Ported to Tortoise-WoW.

## License

This module is released under the [GNU Affero General Public License v3.0](https://www.gnu.org/licenses/agpl-3.0.html). See the included `LICENSE` file for the full text.
