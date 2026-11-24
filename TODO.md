# TODO (backlog index)

Things we should do at some point, but not right now. The current plan
and its order are in `LOGBOOK.md` ("Current focus"). The backlog itself
lives in one file per area under `docs/todo/`; this index stays one
screen long. When picking the next piece of work, open the file of the
area you work on (or `grep -n '^- \*\*' docs/todo/*.md` for all item
titles).

Add an item to the right file whenever something is postponed (what, why,
where to start), and remove it when it's done (mention it in the LOGBOOK
session entry). A new area gets a new file and a line here.

| File | Area |
|---|---|
| `docs/todo/readability.md` | style guide rollout, enums and shared constants, lints, typed fields, typed-access leftovers (class layouts still partial) |
| `docs/todo/computer-players.md` | AI packages A2..A6, naming-pass corrections in the AI code and layouts, original bugs to keep |
| `docs/todo/file-formats.md` | savegame/map/graphics loaders, naming-pass corrections (globals, world regions, verified names found wrong), missing and false function starts, source-file range fixes |
| `docs/todo/naming.md` | provisional names to audit, constructors, remaining `unk` fields |
| `docs/todo/evidence.md` | whole-game evidence gaps (jobs, spells, ExecuteCommands ids), emulator tooling issues, longer and more varied runs, the live-vs-replay hash difference |
| `docs/todo/known-differences.md` | known behaviour differences to clean up; work postponed by decision (network and lobby) |
