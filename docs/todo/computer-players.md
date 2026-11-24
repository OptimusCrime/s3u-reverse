# Computer players (re/computer_players.md)

Part of the backlog (index: `TODO.md`). Each item: what, why, and
where to start. Remove an item when it is done (mention it in the
LOGBOOK session entry).

## Computer players (re/computer_players.md)
- **Package A2: small READY helpers** of the AI range (`python
  tools/frontier.py --range 54cbd0 59fcd0 --library-done`; 74 READY after
  session 53). Done in sessions 48, 51, 52, 53 (see the LOGBOOK). Next:
  `CEvaLand::EvaluateCell` 0x5540f0 (unblocks 3), RandomPointNearSettler
  0x594f70 / 0x5952d0, Army 0x592100 / 0x592240, the military-building
  searches 0x58aa70 / 0x58a7e0 (ret 0xc; 0x58a7e0's first argument looks
  unused), CountResourceInOwnersLand 0x599a30 (7 callers), CheckSite
  0x5760d0, small ones 0x593f40, 0x58e200, 0x591c10, 0x58ede0. x87 code
  (follow CLAUDE.md "Floating point"): residences 0x568890 (curves at 0x7e1380..),
  woodcutter 0x567e00 and stonecutter 0x568420 (curves per race and stage
  at 0x7e1300..0x7e137c, jump tables 0x568200..0x568240; races above 3
  keep the defaults from 0x65a57c). Open: the meaning of AiGroupOrders
  +0x1c48 (`group_flags1c48`).
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
  contents. Next: log byte 6 against the site owner in a real game
  (runtime observation), then decide how to reimplement it. The other 18
  senders are verified (ai_commands.cpp).
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
