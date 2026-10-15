# CLAUDE.md — Bird Game operations manual

Read this first, every session. Claude is the primary engineer; the user is the owner.
Keep this file under ~150 lines: put detail in `docs/` and link to it.

## What this is
A **multiplayer, third-person 3D bird game for desktop (PC)**. You play as a bird flying
around a city. Think "The Isle", but as a bird: pick a species, fly, interact with other
players' birds, and later eat food and steal it from humans. The city is **our own build**
(real-world tiles were dropped; see DECISIONS.md).
**V1 = flight mechanics only.** Gliding, flapping, tucking/diving and wind must feel
satisfying. We iterate through many playtest rounds with the owner.
Full vision and controls: [docs/VISION.md](docs/VISION.md). Current status and next steps:
[docs/STATUS.md](docs/STATUS.md).

## Stack
- **Unreal Engine 5, C++-first.** Gameplay logic lives in C++. Blueprints are only thin
  data/asset wrappers, because Claude can't read binary `.uasset` files. Why: [docs/DECISIONS.md](docs/DECISIONS.md).
- Enhanced Input, Chaos physics, UE networking (replication) for multiplayer later.
- **Flight core** (`Source/FlightCore/`): plain C++17 with no Unreal deps, tested standalone with
  doctest + CMake (runs in cloud sessions too). Design: [docs/FLIGHT_MODEL.md](docs/FLIGHT_MODEL.md).
  Run its tests: `cmake -S Tools/FlightCoreTests -B build/flightcore && cmake --build build/flightcore && ./build/flightcore/flightcore_tests`
- Unreal-side tests: UE Automation framework (Automation Spec, `BirdGame.*` test names).
- Git + **Git LFS** for binary assets (see `.gitattributes`). Host: GitHub `travisssc/bird-game`.
- Build, run and test commands: [docs/BUILD.md](docs/BUILD.md). The UE project itself is
  **not created yet**; that's the first task in docs/STATUS.md.

## Where sessions run
- **Owner's Windows PC (normal):** full loop. Build, test, launch the game, screenshot.
- **Cloud sessions (claude.ai/code):** no GPU and no Unreal. The flight core *can* be fully built
  and tested there. Unreal-side code can't: mark it **UNVERIFIED** in the commit message and in
  docs/STATUS.md, and verify it in the next local session. Never claim cloud-written UE code works.

## Definition of "done"
A change is complete only when **all** of these hold:
1. It compiles (the editor target builds with no errors or new warnings).
2. Automation tests pass (`BirdGame.*`), including new tests for the new behavior.
3. It has been **exercised in the running game**: launch it, perform the action, and capture a
   screenshot or log that shows it working. Telling the owner it works is not verification;
   showing it is.
4. It has been self-reviewed, and also subagent-reviewed if non-trivial (see Working rhythm).
If any step can't be done, say so plainly and mark the work UNVERIFIED.
"Feel" (is flying satisfying?) can only be judged by the owner's playtest. Give them a short
playtest checklist with each flight change.

## Guardrails (always ask the owner first)
- Anything destructive or irreversible: deleting data, assets or branches, `git push --force`,
  rewriting pushed history, `git reset --hard` over uncommitted work.
- Deploying or publishing: Steam, itch.io, public builds, GitHub releases.
- Sending anything to real people: emails, posts, invites.
- **Spending money**: paid assets, paid services, cloud servers, Git LFS data packs. Watch the
  LFS quota before committing large assets.
- Anything that talks to external services with real-world effects must be **idempotent**
  (safe to retry without double-executing). Use idempotency keys, check-before-write, and no
  blind retries of non-idempotent calls. This will matter for accounts and leaderboards later.
- Never commit secrets (API keys, Steam credentials, Cesium/Google tokens). Use env vars or
  untracked config.

## Concurrency
If more than one Claude session works on this repo at the same time, **each session uses its own
git worktree** (`git worktree add ../bird-game-<task> -b <branch>`), never the shared checkout.
UE note: each worktree has its own `Binaries/` and `Intermediate/` and needs its own full
build, so that's slow and uses a lot of disk. Prefer one session at a time unless parallelism
clearly pays off.

## Model usage (owner is on a usage-limited plan)
- **Sonnet** is the default for everyday coding.
- **Tell the owner when a task deserves Opus** (tricky bugs, architecture, flight-model design,
  networking, risky refactors) so they can switch. Use Opus for review subagents on large or
  risky changes.
- Suggest **Fable** only when a problem has stumped the other models.
- Keep sessions short and focused. Long sessions re-read everything every turn. Suggest a
  fresh session at natural stopping points.

## Haiku helper (high-volume, low-judgment work)
Spawn Haiku subagents for: summarizing logs, test output and long files (UE logs are huge);
test fixtures and repetitive test cases; boilerplate from a precise spec; docstrings, READMEs,
changelogs and commit messages; mechanical edits; simple scripts.
- Give it a precise, scoped brief: the exact files, the task, and what to return.
- Review everything it produces before it goes in.
- Never delegate architecture, security, hard debugging, or anything touching real data.
- If a summary looks wrong or incomplete, check the raw source.
- If reviewing would take longer than doing it, do it yourself.
- Log what it handles well or badly below, and shift work toward its strengths.

### Haiku track record
| Task type | Result | Notes |
|---|---|---|
| (none yet) | | |

## Working rhythm
1. For a non-trivial request: **restate it in your own words, propose an approach with
   tradeoffs, then build.** The owner describes outcomes; Claude owns implementation details.
   If something is ambiguous, or info the owner would want is missing, ask, batching questions.
   The owner wants minimal interruptions: ask only when it genuinely matters.
2. Build, with tests alongside the code.
3. Verify: compile, run tests, launch and exercise the game (see Definition of "done").
4. After any non-trivial change, spawn a **read-only review subagent** (Opus if large or risky)
   with the diff, the spec and the test results. It returns findings sorted by severity: bugs,
   then risks, then optional improvements. "No significant issues found" is a valid answer; it
   must not invent findings. Confirm each bug is real (reproduce it or write a failing test)
   before fixing it. List the improvements for the owner to approve; don't apply them
   unasked. Skip this review for trivial changes.
5. Report honestly. Failing tests and unverified parts get stated plainly.
6. **Commit after each verified task** with a clear message, so any step can be rolled back.
7. When a task ships: update docs/STATUS.md, record decisions in docs/DECISIONS.md and memory,
   then tell the owner it's a good point to start a fresh session.

## Decisions and memory
Settled decisions go in **both** [docs/DECISIONS.md](docs/DECISIONS.md) (the source of truth,
travels with the repo) **and** Claude's memory. Check DECISIONS.md before re-asking the owner
anything. The owner should never have to re-explain a settled question.

## Keep this file current
This is the operations manual. Update it whenever commands, structure or rules change.

## Mistake log / gotchas
Whenever a mistake is made or a gotcha is discovered, append the lesson here (date + one line)
so no future session repeats it.
- 2026-10-13: Don't start on a major, hard-to-reverse choice (engine, framework, hosting)
  before discussing it with the owner. Propose first, even when the owner said "no preference".
- 2026-10-13: Cloud sessions are ephemeral containers. Anything not committed **and pushed** is
  lost, and memory written there does not reach the owner's PC. DECISIONS.md is the durable record.
- 2026-10-15: Derive test expectations from physics *before* asserting. I assumed "nose level
  glide stalls", but a fixed-attitude glider settles into a stable descent. Check the math first.
- 2026-10-15: In the flight core, don't name anything `PI`, `check`, `verify` or `ensure`: Unreal
  defines these as macros. Use `BirdFlight::Pi`, and write `(std::min)(a, b)` because of Windows
  macros. GCC rejects `FVec3()` as a default argument inside FVec3 itself (use an overload).
