# Computer players (re/computer_players.md)

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

## Computer players (re/computer_players.md)
- **Package A2: small READY helpers** of the AI range (`python
  tools/frontier.py --range 54cbd0 59fcd0`). Done in session 48: the 25
  T_ECONOMY_AGENT conditions 0x568250..0x569d20 and the map queries
  0x599510..0x5999e0, 0x58ecb0, 0x58ed30, 0x58ed70, 0x59dbb0, 0x59dc70,
  0x593140, and the site queries 0x571d00..0x5720c0, 0x5930f0, 0x593f50.
  Left: 0x591e00 (sets up g_aiGroupOrders, vectors at 0x7e3690), 0x572420
  (fills AiPlayerData::building_counts), the larger economy conditions
  0x567e00, 0x568420, 0x568bc0, 0x568d80, 0x568f40, the snapshot 0x567910
  (needs 0x59dcc0), and the other small READY ones of the frontier (e.g.
  0x597110..0x597260, 0x59cf20, 0x58f8c0, 0x58f940).
  The ~60 identical 104-instruction SEH functions on the READY list are
  probably static std::string-array initializers (check before counting
  them as work).
- **Package A3 follow-ups** (A3 done in session 48): rename the
  deletion-queue adders in ai_helpers.cpp / ai_small_helpers.cpp, which
  add a note id to CComputerPlayer::doomed_notes: `AI::List32Add`
  0x551910 -> `CComputerPlayer::DoomNote`, `AI::List32AddValue_551780`
  -> `CComputerPlayer::DoomNoteId`, `AI::List32AddWord8_551760` ->
  `CComputerPlayer::DoomNoteOf` (the note's id); left out of A3 because
  package A2 worked in those files (both merged now). Also: who creates
  the AiPlayerData nodes; the constructor part 0x550a30 (calls AddAgent)
  is next to 0x550f80.
- **Package A4: command senders** 0x58e220..0x58ec33 and
  0x590990..0x590d86: need `MessageQueue::Push` (pass-through hook) and
  `GameSetup_sub_4318a0` (0x7a7a18 +0x7c flag; name it) as probed callees.
- **Package A5: Cevaland** (land evaluation) and **A6: the agents' run
  functions** (slot 8), smallest first; then 0x550f80 and the
  constructor 0x550900 / 0x550a30.
- **Open:** who creates the AiPlayerData nodes (one per
  economy area; list +0x200 is the deletion queue, session 48); `AiList50Owner` in
  ai_layout.h is the same object as ComputerPlayer (merge them when
  ai_helpers.cpp is next touched); a world-field list of the AI range with
  `tools/world_fields.py`.
- **Corrections from the naming pass (session 50,
  `re/computer_players_naming.md`)**, each touching code or layouts:
  unify `AI::EvaLand::*` /
  `AI::CEvaLand_*` under `AI::CEvaLand::`; `AI::BlockCentreByte8` reads
  the centre tile's owner. Layouts: `g_aiGroupOrders` is 0x20f8 bytes
  (not 0x898), `g_aiArmyState` only 0x51c (its fields from +0x630 belong
  to the census at 0x7e5ef8; savegame chunks 0x15..0x17);
  `g_aiUnk7e3134` is the CEvaLand object (`g_aiLandEvaluation`);
  CComputerPlayer +0x774 is that pointer, not "settlement_lists";
  AiPlayerData offsets are list-node offsets (value from +8).
  re/computer_players.md: "the AI never writes the world directly" is
  wrong (the military code edits player_lists, ProcessWorldEvents 0x590da0
  consumes the event ring); the "list changed" flag is +0xf0 + 4*p; the AI
  code ends at 0x59e0a0 (0x59e6f0.. is Credits code; also in
  re/source_files.csv).
- **Original bugs to keep** (found in the naming pass): 0x58af60 always
  returns 0 (best distance starts at 0); `AI::FewUnownedTilesNear`
  0x59c2c0 always returns 1; `AI::CastBattleSpells` 0x5953b0 pairs the
  Amazons' mana checks with the wrong casters.
