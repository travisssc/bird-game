# Decisions

Settled decisions, newest last. Each entry: date, decision, why, alternatives considered.
Don't re-litigate these without new information. If one changes, add a new entry that
supersedes the old one; don't edit history.

## 2026-10-13 — Roles
Claude is the primary engineer. The owner (Travis) sets direction, playtests and approves
high-stakes actions. The owner has a CS degree but wants Claude to run all commands and
interrupt only when approval is genuinely vital.

## 2026-10-13 — Product scope
- Multiplayer, public, desktop (PC) third-person 3D bird game. Like "The Isle", but as birds.
- **V1 = flight mechanics only**, iterated through many playtests until it's satisfying.
- Later (not now): pick a species, eat food on the ground, steal food from NPC humans,
  bird-to-bird interaction, a real-world city map (Microsoft Flight Simulator style), Steam.
- Accounts and leaderboards: maybe, much later. Money: never for now, maybe "way down the line".

## 2026-10-13 — Engine: Unreal Engine 5, C++-first
**Why:** the realism target (a hybrid that's more real than cartoon), the real-world map path
(Cesium for Unreal + Google Photorealistic 3D Tiles), proven multiplayer at "The Isle"'s scale
(The Isle itself is built on UE), mature Steam integration, and the Fab asset store.
Most sessions run on the owner's PC, so Claude can build, run and screenshot locally.
**C++-first** because Blueprints/maps/materials are binary `.uasset` files that Claude can't
read or diff. Blueprints stay thin: asset references and tunable values only.
**Alternatives:** Godot 4 (text-based and runs in cloud sessions, but weaker realism and no
mature real-world tiles; it was the better choice only if sessions were cloud-only). Unity
(Cesium exists, but has the same GPU/licence friction as UE plus licensing-policy risk).
**Consequence:** cloud sessions can't build or run the game. Work done there is UNVERIFIED
until the next local session.

## 2026-10-13 — Tests: UE Automation framework (Automation Spec)
Conventional for UE, runs headless from the command line. Every feature gets tests. Flight
math should be kept in plain, deterministic functions/components so it can be tested without
spawning a full world where possible.
Considered: an engine-agnostic C++ flight core tested with doctest (runnable in cloud
sessions). Rejected for now as less conventional; revisit if cloud sessions become common.

## 2026-10-13 — Version control: Git + Git LFS
Binary assets (`.uasset`, `.umap`, models, textures, audio) go through LFS
(`.gitattributes`). Build output (`Binaries/`, `Intermediate/`, `Saved/`,
`DerivedDataCache/`) is never committed. GitHub's free LFS quota is limited, so ask the owner
before anything that might need paid storage.

## 2026-10-13 — Placeholder art
Use free stock crow assets (CC0/free-licensed, e.g. Fab free content or Quaternius) to test
mechanics. Final art style (a cartoon/realism hybrid leaning realistic) will be decided later.
Record each asset's source and license in `docs/ASSETS.md` when added.

## 2026-10-13 — Model usage
Sonnet by default. Opus for architecture, tricky bugs and risky changes, and for review
subagents on large/risky diffs. Haiku subagents for high-volume, low-judgment work. Fable only
when other models are stumped. Keep sessions short.

## 2026-10-13 — Owner hardware
RTX 3090, ~3 TB free disk. Enough for UE5 (including a source build if ever needed), Lumen/Nanite
and Cesium tile streaming. Disk use is not a concern; still keep build output out of git.

## 2026-10-13 — Real-world map is a CORE goal
Flying over a real city (Microsoft Flight Simulator style) is core, not "someday".
Plan: **Cesium for Unreal + Google Photorealistic 3D Tiles**, evaluated early (a spike right
after the UE project exists) so flight is tuned in a representative environment.
Things the spike must answer before we commit:
- **Cost/licensing:** Google Map Tiles API needs a billing-enabled Google Cloud account, and
  Cesium ion has free vs commercial tiers. Their terms restrict caching, attribution and
  commercial use (which matters for Steam). **Ask the owner before creating any billing
  account or API key.** Keys never go in git.
- **Collision:** can birds land on and collide with streamed tile geometry reliably?
- **Multiplayer:** the server also needs collision for the area players are in.
- **Look:** photogrammetry tiles are photoreal, so how does that sit with the hybrid art
  style and our bird models?
Fallback if the terms or quality block us: a hand-built or procedurally generated city.
The flight model must not depend on which world source is used.

## 2026-10-13 — Controls (initial)
Hold Space = glide, hold Left Click = flap, hold **Shift = tuck wings / dive** (confirmed by
the owner), mouse = steer/camera. All bindings are via Enhanced Input so they stay remappable.
(Hold Alt = free look was added 2026-10-15, see below.)

## 2026-10-15 — City: build our own (supersedes the Cesium/Google Tiles plan above)
**Decision:** we drop Cesium + Google Photorealistic 3D Tiles. The game world is a city we build.
**Why (owner's call, agreed):** photogrammetry tiles are low-detail and "melted" up close, and
the game is played up close: landing, walking, perching, human NPCs and stealing food. The owner
also wants the city to be fully part of the game (collision everywhere, landable surfaces, the
server knows the geometry). Streamed Google tiles fight all of that, and their terms (no caching,
attribution, billing account) add friction. The owner wants personal use for now and will try
a small local server with friends soon.
**Still open (later):** a *real-world-inspired* layout is possible by generating our own city
geometry from OpenStreetMap data (footprints/heights, ODbL, attribution required). That keeps
real layouts with our own art. Not needed for V1; flight testing uses a simple test city.

## 2026-10-15 — Flight model: engine-agnostic C++ core with standalone tests
(Revisits the 2026-10-13 test decision.)
The flight simulation lives in `Source/FlightCore/`: plain C++17, its own UE module
(depends only on Core), and no UObjects. It's tested by doctest via CMake in
`Tools/FlightCoreTests/`.
**Why now:** the owner is away for a while, so cloud sessions are common, and this lets the most
important code be built and verified without Unreal. A deterministic pure core also suits
multiplayer prediction. UE Automation Specs are still used for Unreal-side code.
**Model:** "sim-lite" hybrid. The player drives the nose orientation (rate-limited toward the
camera aim, auto-bank); real aerodynamics (AoA, stall, induced drag) drive velocity. Units are
SI and the axes are Unreal's (X fwd, Y right, Z up). Details: docs/FLIGHT_MODEL.md.
**Planned Unreal integration:** a CharacterMovementComponent subclass with a custom "Flying"
movement mode calling `BirdFlight::Step()` in `PhysCustom`. That gives CMC's collision sweeps,
ground modes, and battle-tested client prediction/replication. Glide/flap/tuck use 3 of CMC's
4 custom saved-move flags. (Considered: the Mover plugin, which is newer and less proven, and a
fully custom pawn, which would mean writing prediction ourselves.)

## 2026-10-15 — Multiplayer target (near term)
Small sessions with a friend or two on a local/listen server. No dedicated servers, accounts or
matchmaking for now.

## 2026-10-15 — Owner answers: free look, wind as a core mechanic, flight feel
(Answers to the open questions in FLIGHT_MODEL.md; owner priorities: feel like a real bird, but
fun and satisfying mechanics come first.)
- **Free look: hold Alt** to look around without changing course. Only the bird's head turns;
  the camera swings back on release. Implemented in `LookControl.h`.
- **Wind is a core mechanic.** You can't glide without wind or speed (airspeed is speed through
  the air), and the world always has wind (`CityBreeze()`). Buildings make ridge lift and
  sheltered, turbulent wakes. Wind must be **visible as little translucent streaks** through the
  sky (`WindStreaks.h`).
- **Glide trim on by default** (owner: "whatever feels best, realistic"). Real birds trim
  themselves, so looking level flies the best glide. `GlideTrimAssist` is the A/B lever.
- **g-cap: yes, 7 g.** Weighty but not sluggish (owner: realism, but fun first, "not too
  weighty"). `MaxLoadFactor` is the lever.
- **Bank from the flight path: on by default** (owner unsure, wants to test). Measured: banking
  from the nose's turn rate left the path 5° off ~2.9 s into a 90° turn. Banking from the
  nose-vs-path gap lines it up in ~1.5 s with no overshoot. Added turn stall protection so
  camera flicks don't stall the bird mid-turn. `BankTrackBlend` 0/1 is the A/B for the playtest.
- **Flap beats tuck** when both are held: kept.
