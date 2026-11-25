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
  economy actions, woodcutter/stonecutter; session 55 (ai-econ): the
  residences, AreaInDirection, trees / stones near own fields, forced
  production and tools, the request Execute chain and slots; session 55
  (ai-mil): RunMilitary 0x58f990 and T_ATTACK_AGENT::Run with all their
  callees, the safety agent; session 56: the area lists and the
  CComputerPlayer construction). Next: the READY ones below.
  AiGroupTarget holds two patrol buildings and a prey settler;
  AiGroupOrders +0x1c48 is `patrol_steps` (session 55).
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
- **Package A5/A6: what is left of the CComputerPlayer.** Session 56
  did the area lists (wip/ai-areas: AddArea, Fill/RefillAreaSiteList(s),
  ProcessPendingEvents, COMP_FoodForMinesOptimum, the list ctor/dtor and
  bitset ctor (manual function starts), Init and the constructor) and the
  expand / map worker agents, OnBuildingFinished, HasBuildingTypeNear,
  AdjustSoldierShares, CCentrumCheckAgent::Run (wip/ai-agents). Session
  57 (wip/ai-step2) did CComputerPlayer::Step 0x550f80 and
  COMP_FoodForMines 0x5724e0. Not in Ghidra's list (no rows): 0x596540
  (a vector's free).
- **Open (session 55 round 2):** the woodcutter /
  stonecutter finders pick the cell with the FEWEST trees / stones when the
  area stocks more than 10 (an ascending SiteTree): looks like an original
  quirk or intent (thinning); confirm when the agents are done. (The
  Area copy's tree pointers are settled: AddArea resets them, session 56.)
- **Who reads the site trees:** the map worker's MakeBuildOrders 0x57ce90
  and the economy's site search take candidates from
  `site_tree_of_type[type]`; check that nobody reads it for types 0, 15,
  16, 18 (uninitialized there, session 56) and whether RefillAreaSiteList's
  workshop quirk (ratings into tree 34) matters in a game.
- **Open:** a world-field list of the AI range with
  `tools/world_fields.py`.
- **Left from the naming-pass corrections (session 51 did the code and
  layout ones):** the AI code ends at 0x59e0a0 (0x59e6f0.. is Credits
  code): fix re/source_files.csv.
- **Original bugs to keep** (found in the naming pass): 0x58af60 always
  returns 0 (best distance starts at 0); `AI::FewUnownedTilesNear`
  0x59c2c0 always returns 1 (kept in the replacement, session 54); `AI::CastBattleSpells` 0x5953b0 pairs the
  Amazons' mana checks with the wrong casters.
- **Open (session 56):** the map worker's castle order needs another own
  castle within 60 (HasBuildingNear's result tested the other way round
  than everywhere else) and the residences 27 / 28 use the farm's site
  tree pointer: confirm in a game whether computer players ever build
  castles through this path (they also get them from the safety agent).
- **Session 56 (wip/ai-agents) also did** AdjustSoldierShares 0x56a840,
  OnBuildingFinished 0x573890, CCentrumCheckAgent::Run 0x560e00 and the
  credits' debug / timer helpers 0x59e6f0..0x59f2a0. Left in the AI range
  (session 57): COMP_EmergencyPowersActStone 0x573560 (needs
  SendPrioritizeSite).

