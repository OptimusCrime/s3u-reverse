# Computer players (re/computer_players.md)

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

## Computer players (re/computer_players.md)
- **Package A2: small READY helpers** of the AI range (`python
  tools/frontier.py --range 54cbd0 59fcd0 --library-done`). Done in
  sessions 48, 51, 52, 53, 54 (see the LOGBOOK; session 54: CEvaLand cell
  evaluation, resource counts, CheckSite, the army group bookkeeping,
  military-building searches, CheckGroupTargets, thieves/geologists,
  economy actions, woodcutter/stonecutter). Next: small ones 0x58ae90
  FirstUnassignedMilitaryBuilding, 0x599760 AreaInDirection, 0x59a260 /
  0x59a7a0 (trees / stones near own fields), 0x573670
  (EmergencyPowersActWood), then FillAreaSiteLists 0x556630. x87 code
  (CLAUDE.md "Floating point"): residences 0x568890 (curves at
  0x7e1380..; same shape as ConsiderCutters in ai_economy_conditions.cpp
  probably). The military side is done (session 55: RunMilitary 0x58f990
  and T_ATTACK_AGENT::Run with all their callees, the safety agent).
- **Table names to revisit:** g_aiWantedGoldMines 0x65a8c8 /
  g_aiWantedWeaponsmiths 0x65a8d2 and g_aiWantedGoldMineSlopes 0x7e12b0 /
  g_aiWantedWeaponsmithSlopes 0x7e1290 are rows (Roman, Asian) of the
  stonecutter's per-race tables (session 54); the mine/smelter conditions
  use just those rows. Maybe rename once the static initializers 0x566xxx
  are read.
- **Who creates the AiPlayerData nodes** (one per economy area; list
  +0x200 is the deletion queue, session 48); the constructor part
  0x550a30 (calls AddAgent) is next to 0x550f80. AiPlayerData offsets are
  list-node offsets (value from +8): documented in ai_helpers.cpp
  (session 51), the fields are not re-based yet.
- **SendPrioritizeSite 0x58e510 (left out of package A4, session 52):**
  the PrioritizeSite (0xf5) handler takes the player from message byte 6,
  which this sender never writes (stack garbage); the handler does
  nothing unless that byte equals the site's owner. So whether a computer
  player's site prioritisation works depends on the original's stack
  contents. Session 55 confirmed it statically: the message is built on
  the sender's own stack frame (bytes 3, 6, 7 never written), so byte 6
  is whatever COMP_EmergencyPowersActStone 0x573560's earlier callees
  left there; it can't be settled without running the game. Next: a
  pass-through on 0x58e510 (or on MessageQueue::Push for id 0xf5 from the
  AI) that logs byte 6 and the site's owner in a real game with computer
  players, then decide (reproduce the observed value, or document a
  known difference). The other 18 senders are verified (ai_commands.cpp).
- **Package A5: Cevaland** (land evaluation) and **A6: the agents' run
  functions** (slot 8), smallest first; then 0x550f80 and the
  constructor 0x550900 / 0x550a30.
- **Open:** a world-field list of the AI range with
  `tools/world_fields.py`.
- **Left from the naming-pass corrections (session 51 did the code and
  layout ones):** the AI code ends at 0x59e0a0 (0x59e6f0.. is Credits
  code): fix re/source_files.csv.
- **Original bugs to keep** (found in the naming pass): 0x58af60 always
  returns 0 (best distance starts at 0); `AI::FewUnownedTilesNear`
  0x59c2c0 always returns 1 (kept in the replacement, session 54); `AI::CastBattleSpells` 0x5953b0 pairs the
  Amazons' mana checks with the wrong casters.
