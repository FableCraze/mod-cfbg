# Cross-faction Battlegrounds for Skyfire 5.4.8

This is a port of the AzerothCore MIT module, integrated through Skyfire's generic hooks. It allows battleground teams to include players from both factions, including when only one faction queues. It also supports Wintergrasp, player backfilling, balance by population/level/gear, appearance preferences, and keeping premade groups together. Arenas continue to use the original core behavior.

Combat faction, team, and display model are temporary. The character's actual race, racials, and persisted data remain native. Druid forms and transformation auras take precedence over the module's display model.

## Installation

1. Use this module with a core that includes this port's `PvPScript` hooks.
2. Configure CMake with `MODULES=ON` and `MOD_CFBG=ON` (the module's default), then build `worldserver`. The existing module loader discovers the module and `CFBG.conf.dist` automatically.
3. Manually apply `data/sql/db-characters/cfbg_preferences.sql` to the **characters** database and `data/sql/db-world/cfbg_commands.sql` to the **world** database. SQL is not imported automatically.
4. Copy `CFBG.conf.dist` to `CFBG.conf` beside the main server configuration file, then set `CFBG.Enable = 1`. Without this file or an equivalent option in `worldserver.conf`, the module remains disabled.
5. Start the server. To apply configuration changes while running, use `.reload config`.

Options in `CFBG.conf` override options with the same names in `worldserver.conf`.

Cross-faction queues continue to work without the preference table; race choices are stored only for the current session. Restart the server after installing the table so it can be detected.

## Commands

- `.cfbg race <race|random>`: available to players with the existing `COMMAND_COMMANDS` permission (496). Requires `CFBG.RandomRaceSelection = 0`. The choice is validated against class, gender, opposing faction, and available models, and takes effect on the next PvP entry. An invalid choice leaves the previous preference unchanged.
- `.cfbg debug [online player]`: requires `COMMAND_DEBUG_BG` (304); shows native/current team, battleground team, actual race, display model, and Wintergrasp assignment. Without a name, it uses the selected target or the player. The console requires a name.
- `.bf list <battleId>`: added to the existing `.bf` command tree without replacing its commands. Requires `COMMAND_BF` (257) and shows zone population, queue, invitations, and war status. Wintergrasp's battle ID is 1.

Accepted race names: `human`, `orc`, `dwarf`, `nightelf`, `undead`, `tauren`, `gnome`, `troll`, `goblin`, `bloodelf`, `draenei`, `worgen`, `pandaren_alliance`, and `pandaren_horde`. Neutral Pandaren do not represent a war team and are not offered as an appearance. Monk combinations use the options available in Skyfire.

## Queues and lifecycle

Groups are selected whole from the four existing queues. Before invitations are issued, each selected group's team assignment and queue position are updated together. Pending invitations reserve slots; declines, expirations, and leaves continue through core events. Entry-time and gate-opening corrections keep groups committed to the same match together. Gate-opening corrections also transfer the raid, counts, reservations, and starting position.

Wintergrasp keeps the minority on its native faction and assigns the majority's surplus in acceptance order, subject to capacity. When enabled, team locking lasts until the war ends, including after leaving the zone or relogging. Assignments are kept in memory and reset when the server restarts. Classes excluded by configuration remain native. Transfers move zone, queue, invitation, war, and kick records while preserving their deadlines.

When disabled, new selections return to the original algorithm. Groups that have not yet been invited return to their native team; issued invitations and current participants keep their assigned team until they leave or the event ends. Rewards are processed before restoration. Team chat uses universal language; say/yell and normal channels retain their native behavior.

## Alterac Valley and databases

The original AzerothCore SQL uses a `faction` column that does not exist in this database. The sixteen commander entries were checked against the local `SFDB_full_548_26.002_2026_008_18_Release.sql` dump, which uses `faction_A`/`faction_H`. This port adjusts only the creatures in an Alterac Valley instance before it starts; it does not modify global templates. Frostwolf/Stormpike reputation is redirected to the native faction, preserving gains and losses without duplicate credit.

## Compatibility and tests

Use `MODULES=OFF` to build without modules, or `MODULES=ON MOD_CFBG=OFF` to keep other modules while excluding CFBG. The core does not reference CFBG symbols, configuration files, or tables. Without a registered script, queue selection, invitations, chat, reputation, and team hooks use their original defaults. Normal channels use the native team, which matches the original team when no temporary team is assigned.

With `SKYFIRE_BUILD_TESTS=ON`, run the relevant tests with:

```text
ctest --test-dir <build> -C Debug -R "^(cfbg_balance_tests|pvp_team_state_tests|pvp_script_hooks_tests)$" --output-on-failure
```

They cover formation with a single faction, indivisible groups, odd populations, capacity and pending invitations, Wintergrasp native-team priority, record transfer and rollback, empty hooks, registration/unregistration, and enabling/disabling the module with committed invitations.

Final validation with a 5.4.8 client should cover combat and healing between teams, objectives, graveyards, scoreboard, models, druid forms, resurrection, disconnect/relog, and rewards at battleground/Wintergrasp completion. Automated tests do not replace in-game verification.

## Credits

Winfidonarleyan (Andrei Guluaev), Viste, Irancore, and AzerothCore contributors. The original MIT license is preserved in `LICENSE`.
