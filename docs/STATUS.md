# Status

_Last updated: 2026-10-15 (cloud session: free look, wind mechanic, flight feel)_

## Current state
- Engine decided: UE5, C++-first. **No Unreal project exists yet.**
- City: we build our own (Cesium/Google Tiles dropped 2026-10-15, see DECISIONS.md).
- **Flight core done and verified standalone** (`Source/FlightCore/`, 28 tests passing with gcc,
  clang and gcc+ASan/UBSan; independently reviewed, findings fixed). Design: docs/FLIGHT_MODEL.md. Chart: docs/img/flight_lab.svg.
- **Added 2026-10-15 (second session), all verified standalone (53 tests):** Alt free look +
  head look (`LookControl.h`), wind as a core mechanic (shear, building ridge lift and wakes,
  `CityBreeze()` default), visible wind streaks (`WindStreaks.h`, preview
  docs/img/wind_streaks.svg), a 7 g pull-out cap, glide trim (level = best glide), banking from
  the flight path, and turn stall protection. Owner's answers are in DECISIONS.md.
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
   docs/FLIGHT_MODEL.md): a CMC subclass with a custom Flying mode calling
   `UpdateVelocityAndOrientation()`, Enhanced Input (Space/LMB/Shift/**Alt**/mouse) through
   `UpdateLook()`, a third-person camera driven by the look state, a neck bone driven by the head
   angles, a simple blockout test city with tall buildings fed to the wind as `FWindObstacle`s,
   `CityBreeze()` wind, wind streaks (debug lines first, then a Niagara ribbon), and a debug HUD
   (airspeed, AoA, stall, wing extension, g, wind). Then the first playtest with the owner.
8. First playtest checklist (A/B the levers live, e.g. via console variables):
   - Glide looking level: does it feel floaty/real? A/B `GlideTrimAssist` 1 vs 0.
   - Tucked dive, then look level: is the 7 g pull-out satisfying? Try 5 and 9.
   - Flick the camera 90° and 180°: carve (`BankTrackBlend` 1) vs flat skid (0). Any stall?
   - Hold Alt and look around/behind: does the bird hold course? Does the head look natural?
     Is the swing-back on release comfortable?
   - Hang in front of a tall building facing the wind. Fly through the lee: bumpy and sinking?
   - Can you read the wind from the streaks? Too many, too faint, too long?

## Cloud-session work that doesn't need Unreal
- Tune or extend the flight core (stamina, A/D manual roll, landing flare) with tests.
- Wind: a spatial grid for many buildings; street channeling; a wind direction that changes
  slowly over a session.
- Ground/walking model design. Species parameter sets (gull, pigeon, hawk) as data.

## Open questions for the owner
- None right now. (Answered 2026-10-13: RTX 3090 with 3 TB free; real-world map is core;
  Shift = tuck.)
