# Computer players (re/computer_players.md)

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

## Computer players (re/computer_players.md)
- **Package A2: small READY helpers** of the AI range (`python
  tools/frontier.py --range 54cbd0 59fcd0 --library-done`; without that
  flag ~93 already classified std::string static initializers show up as
  READY). Done in sessions 48 and 51 (see the LOGBOOK). Left: the
  woodcutter 0x567e00 and stonecutter 0x568420 conditions (two float
  curves per race and stage at 0x7e1300..0x7e137c, jump tables
  0x568200..0x568240; races above 3 keep the defaults from 0x65a57c; a
  per-race woodcutter surplus term), residences 0x568890 (uses Random),
  and small READY ones: 0x59db30, 0x571f60, 0x58a250, 0x59a720, 0x58b410,
  0x573810, 0x58b380, 0x593040, 0x5934a0. Open: the meaning of
  AiGroupOrders +0x1c48 (`group_flags1c48`, 20 x 10 bytes cleared by
  Army::Reset).
- **Who creates the AiPlayerData nodes** (one per economy area; list
  +0x200 is the deletion queue, session 48); the constructor part
  0x550a30 (calls AddAgent) is next to 0x550f80. AiPlayerData offsets are
  list-node offsets (value from +8): documented in ai_helpers.cpp
  (session 51), the fields are not re-based yet.
- **Package A4: command senders** 0x58e220..0x58ec33 and
  0x590990..0x590d86: need `MessageQueue::Push` (pass-through hook) and
  `GameSetup_sub_4318a0` (0x7a7a18 +0x7c flag; name it) as probed callees.
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
  0x59c2c0 always returns 1; `AI::CastBattleSpells` 0x5953b0 pairs the
  Amazons' mana checks with the wrong casters.
