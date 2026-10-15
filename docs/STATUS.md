# Status

_Last updated: 2026-10-15 (cloud session: flight core)_

## Current state
- Engine decided: UE5, C++-first. **No Unreal project exists yet.**
- City: we build our own (Cesium/Google Tiles dropped 2026-10-15, see DECISIONS.md).
- **Flight core done and verified standalone** (`Source/FlightCore/`, 28 tests passing with gcc,
  clang and gcc+ASan/UBSan; independently reviewed, findings fixed). Design: docs/FLIGHT_MODEL.md. Chart: docs/img/flight_lab.svg.
- UNVERIFIED in Unreal: `FlightCore.Build.cs` and `FlightCoreModule.cpp` were written without an
  engine. The first local build must confirm the module compiles under UBT/MSVC.

## Next up: first local session on the owner's PC (suggest Opus: it's architecture)
1. Check the environment: UE5 installed (pick the latest stable 5.x and record the exact
   version in DECISIONS.md), Visual Studio 2022 with the C++ game workload, Git LFS, and enough
   disk space. Ask the owner only for things Claude can't install itself.
2. Create the project **in this repo root** as a C++ project named `BirdGame`
   (Third Person C++ template, or the Blank C++ template plus Enhanced Input). Commit the
   generated files that belong in source control.
3. Confirm and fix the commands in docs/BUILD.md (build, test, run, screenshot) by actually
   running them. Record anything surprising in the CLAUDE.md mistake log.
4. Add a first Automation Spec test (a trivial one, to prove the pipeline) and run it headless.
5. Find a free crow asset (record source + license in docs/ASSETS.md).
6. Add the existing `FlightCore` module to `BirdGame.uproject` / the game module's dependencies
   and build it under MSVC. Fix anything UBT complains about.
7. Build the Unreal integration (plan in DECISIONS.md 2026-10-15, plus the requirements list in
   docs/FLIGHT_MODEL.md): a CMC subclass with a custom
   Flying mode calling `BirdFlight::Step()`, Enhanced Input (Space/LMB/Shift/mouse), a
   third-person camera, a simple blockout test city with tall buildings, and a debug HUD
   (airspeed, AoA, stall, wing extension). Then the first playtest with the owner.

## Cloud-session work that doesn't need Unreal
- Tune or extend the flight core (stamina, A/D manual roll, landing flare) with tests.
- Ground/walking model design. Species parameter sets (gull, pigeon, hawk) as data.

## Open questions for the owner
- None right now. (Answered 2026-10-13: RTX 3090 with 3 TB free; real-world map is core;
  Shift = tuck.)
