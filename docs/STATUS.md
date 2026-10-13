# Status

_Last updated: 2026-10-13 (setup session, cloud)_

## Current state
- Repo contains docs and config only. **No Unreal project exists yet.**
- Engine decided: UE5, C++-first (see DECISIONS.md).

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
6. Propose a flight-model architecture to the owner before building it. It must not depend on
   the world source (tiles vs a hand-built city).

## Soon after: Cesium real-world spike (core goal, see DECISIONS.md)
Install Cesium for Unreal and check cost/licensing, collision, multiplayer implications and
the look. **Ask the owner before creating any Google Cloud billing account or API key.**

## Open questions for the owner
- None right now. (Answered 2026-10-13: RTX 3090 with 3 TB free; real-world map is core;
  Shift = tuck.)
