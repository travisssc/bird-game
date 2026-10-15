# Flight model

The heart of V1. The code is in `Source/FlightCore/` (plain C++, no Unreal dependencies), and
the tests are in `Tools/FlightCoreTests/`. All tunables are in `FFlightParams`
(`FlightModel.h`), with crow defaults.

## Why a separate, engine-agnostic core
- **Testable anywhere.** It builds with CMake + doctest in seconds, including in cloud sessions
  with no Unreal. That gives fast, deterministic tests for the part of the game that matters most.
- **Multiplayer-ready.** The model is deterministic and its state is plain data, which suits
  client prediction and server reconciliation.
- **World-agnostic.** It knows nothing about level geometry, so the city can change freely.

The Unreal side (not written yet) owns collision, ground movement, rendering, animation and
networking. Each tick it feeds mouse/keys into the look controller (`LookControl.h`), calls the
flight model while airborne, samples the wind (`WindField.h`) and updates the visible wind
streaks (`WindStreaks.h`).

| File | What it does |
|---|---|
| `FlightModel.h` | Wings, orientation, aerodynamics, integration |
| `WindField.h` | Wind: shear, gusts, thermals, flow around buildings; `CityBreeze()` preset |
| `LookControl.h` | Camera, flight aim, Alt free look, head angles |
| `WindStreaks.h` | Visible wind: translucent streaks carried by the wind field |

## How it works (one substep, 120 Hz)
1. **Wing pose.** Wing extension moves toward a target at a limited rate. The target is set by
   priority: flap (1.0) > tuck (0.0) > glide (1.0) > relaxed (0.5).
2. **Orientation (player-driven).** The nose turns toward `AimDirection` (the flight aim from
   the look controller) at a limited rate, slower with wings tucked. Heading and pitch are
   stepped separately, so flicking the camera behind you makes a flat turn, not a split-S.
   Nose pitch is clamped to ±88°. On top of that:
   - **Glide trim** (`GlideTrimAssist`, on): when not flapping or tucking (Space held or wings
     relaxed) and not stalled, aiming
     at the horizon holds the best-glide attitude, nose ~5° up for the crow. It fades out
     linearly by 30° of aim pitch, so dives and climbs are untouched.
   - **Stall:** when stalled (and not flapping), the effective aim is pulled toward the
     oncoming air, so the nose drops. That's stall recovery.
   - **Load-factor cap** (`MaxLoadFactor`, 7 g): above ~17 m/s, even a small angle to the
     airflow makes a big force, so the nose is held within the angle that produces 7 g. The
     path then bends at 7 g and the nose rides along it. A 60 m/s pull-out takes ~1.5 s and
     ~60 m of height instead of a 19 g snap. The forces are clamped to the same cap as a backstop.
     (The nose limit adds the lift and tail slopes, so a pure pull peaks a little under, ~6.5–7 g.)
   - **Turn stall protection** (`TurnStallMargin`): yawing the nose ahead of the flight path in
     a bank raises the wing's angle of attack. The yaw step is shortened so a turn never pushes
     it past 90% of stall: a camera flick makes the bird turn wider, not stall. Pulling the nose
     up can still stall it.
   - **Bank** follows a coordinated-turn rule, `tan(bank) = v * yawRate / g`, capped at 65°.
     When the nose stops but the flight path hasn't caught up, the bank is *held* (and eases
     off) at the rate the path must turn to close the gap (`gap / BankTrackTimeSec`,
     `BankTrackBlend` 1 = on). The bird stays banked until its path has caught up, like a bike
     leaning out of a corner, instead of rolling level early and skidding sideways. The hold
     term never *starts* a bank, so wind moving the path (gusts, a tailwind on takeoff) can't
     roll the bird.
3. **Forces (physics-driven).** These are computed from the *air-relative* velocity (velocity minus wind):
   - Lift = q · S · CL(α), perpendicular to the airflow. CL is linear up to stall (~17.8°), then
     decays. S = wing area × extension.
   - Drag = profile + induced (CL²/πAR·e) + flat-plate (past stall only) + body.
   - Side force from the tail/body, opposing sideslip, so velocity follows the heading.
   - Flapping: forward thrust that fades with speed (it caps flapping cruise around 16 m/s),
     plus vertical lift that fades by 8 m/s (this is what makes takeoff work). Both pulse with
     the wingbeat (4 Hz). `FlapPhase` is exposed for animation sync.
4. **Integrate.** Semi-implicit Euler.

**What this means for the player.** As with a real glider, *nose attitude sets speed*:
- Look level: best glide, about 10.5:1 at about 8.6 m/s (9:1 at 11 m/s with the trim off).
- Look down: faster.
- Look up while gliding: you bleed speed and stall.
- Tuck: you drop like a stone, up to about 65 m/s.
- Pull out of a dive: a weighty 7 g arc, then the speed turns back into height (a zoom climb).
- Turn: the bird banks up to ~65° and carves round. A 90° turn is lined up in ~1.4 s, with no
  stall and no sideways skid at the end.
- **Airspeed is speed through the air**, not over the ground. In calm air a slow bird can't
  glide: it stalls and has to dive for speed. Facing into a wind gives airspeed for free, so
  in front of a building in an 8 m/s breeze a crow hangs almost still while the updraft lifts it.

## Free look (hold Alt) and head look (`LookControl.h`)
- **Normal flight:** mouse deltas move the flight aim and the camera together (pitch ±85°).
- **Hold Alt:** the flight aim freezes in world space, so the bird holds its course. The mouse
  orbits the camera all the way round, and the head turns to look where the camera looks,
  within the neck limits (±120° yaw, +50°/−70° pitch, 720°/s). Past the limit (looking
  behind), the head stays on its side instead of flicking across.
- **Release Alt:** the camera eases back to the flight aim (~99% in 0.3 s). The mouse steers
  straight away, and re-pressing Alt mid-swing picks the camera up where it is. Releasing never turns the bird.
- Outside free look, the head leads into the aim by half (birds look into their turns).

## Unreal / CMC integration requirements (from the 2026-10-15 review)
- Use `UpdateVelocityAndOrientation()` per CMC substep, and let `PhysCustom` sweep
  `Velocity * dt`. Don't use `Step()`, which moves without collision.
- `WingExtension`, `BankRad` and `FlapPhase` affect forces but aren't position, velocity or
  rotation. They must be in the saved move / correction data (or flap pulses must be derived
  from replicated data). Otherwise client and server diverge every wingbeat.
- Gusts depend on absolute time. Either sync the wind clock per move, or keep gusts cosmetic
  and out of the authoritative simulation. Steady wind and thermals are time-independent.
- Call `SanitizeParams()` on species params after loading or editing them.
- Unreal may compile with fast-math on MSVC. Re-run the NaN-robustness test as a UE Automation
  test to confirm.
- Don't use `using namespace BirdFlight;` at file scope in UE code (`Chaos::FVec3` exists).
- **Look:** the PlayerController feeds raw mouse deltas and the Alt state to `UpdateLook()`,
  then writes the camera yaw/pitch to `ControlRotation` (it must not also add mouse input
  itself). `FFlightInput.AimDirection = GetAimDirection()`, so the frozen aim is what's
  replicated through saved moves. Head yaw/pitch go to the AnimInstance (a neck/head bone
  rotation). They're cosmetic, so replicate them unreliably at a low rate.
- **Buildings in the wind:** at level load, collect building boxes (or tag actors) into an
  `FWindObstacle` array that outlives every `SampleWind` call. `SampleWind` loops over all
  obstacles (with a cheap distance reject), so a big city needs a spatial grid first.
- **Wind streaks:** one `FWindStreakSystem` per local player, cosmetic and never replicated.
  It's ~60 KB, so heap-allocate it. Each frame, call `Update(wind, time, cameraPos, cameraFwd)`,
  then draw each live streak as a thin camera-facing ribbon through its points with opacity
  `Alpha`, using an unlit translucent material (white, soft edges). Plan: a Niagara system with
  a ribbon renderer fed from arrays (`UNiagaraDataInterfaceArrayFunctionLibrary`), one ribbon
  ID per streak. Fallback for the first playtest: `DrawDebugLine`.

## Wind (`WindField.h`): a core mechanic
`SampleWind(params, position, time)` is deterministic, so clients and the server agree without
replicating wind state. It combines:
- **Steady base wind with shear:** a power law (`ShearExponent`, ~0.3 for a city) that
  grows with height, so streets are sheltered and rooftops are windy. Gusts scale with it.
- **Smooth gusts:** sum-of-sines that vary in time and space, seeded. Vertical gusts are 0.4×
  weaker.
- **Up to 16 thermals:** Gaussian updraft columns that fade near the top.
- **Buildings** (`FWindObstacle` boxes, any yaw). A cheap, smooth model of the flow, not CFD:
  - Air slows against the windward face and escapes up it (**ridge lift**, up to 0.6× the
    wind speed near roof height, reaching ~1–2 heights upwind) and around the corners.
  - Behind the building is a **wake**: up to 80% of the wind removed, a little sink peaking one
    height downwind, and fast turbulence. It fades over 4 heights.
  - Everything is continuous (tested), so crossing a wake's edge never kicks the bird.
- `FWindParams::CityBreeze()`: the default world wind (~5 m/s at 50 m, light gusts). The air
  is never dead calm.

Wake turbulence and gusts depend on absolute time; see the multiplayer note above.

## Visible wind (`WindStreaks.h`)
Short, translucent white streaks drifting with the real wind around the camera (160 at full
strength, within 60 m, mostly ahead). Each streak is a trail of 8 points advected through
`SampleWind`, so the streaks lie along the wind and are longer when it's stronger (~4 m at
8 m/s). They curve up a windward face, split around corners and thin out in a wake. Density
and opacity follow the local wind speed: none below 1.5 m/s, full from 8 m/s, max alpha 0.22.
They fade in and out over their 1.5–2.5 s life. Preview, rendered from the real code:
[img/wind_streaks.svg](img/wind_streaks.svg) (regenerate with `wind_lab`, see docs/BUILD.md).

## Verified behaviors (tests)
| Behavior | Expectation |
|---|---|
| Level-aim glide | glide ratio 6–14, 7–18 m/s, no stall |
| Nose-high glide | stalls, then the nose drops |
| Tucked vs spread dive | tucked > 1.4× spread; 40–90 m/s terminal |
| Dive then pull up | > 25 m/s at the bottom, zoom-climbs > 10 m |
| Flap at cruise | climbs |
| Flap from rest | takes off forward |
| No flap, no wind | mechanical energy never increases |
| Turns | banks into the turn (right = +bank), levels out after; tucked turns slower |
| Uniform wind | ground track shifts exactly by wind × time |
| Thermal circling | gains height; the same circle in still air loses it |
| Determinism, NaN-safety, hitch clamp | bit-identical replays; no NaN for degenerate inputs |
| Aim flicked 150–180° behind | flat turn: nose never below −30°, no roll snap |
| Corrupted state/wind, zeroed params | repaired; no NaN |
| Per-substep API + caller move | bit-identical to `Step()` |
| Pull-out from a tucked dive | peak load ≤ 7.35 g (cap reached), level in 1–2.5 s |
| Glide trim | level aim = theoretical best glide (±3%); off = nose level; mapping monotonic |
| Banked 90° turn | path lined up < 2 s, no overshoot; nose-rate banking > 1 s slower |
| Flick turns at glide speed | never stall |
| No turn input (crosswind/tailwind takeoff, side gust gliding or diving) | bank < 10° |
| Hanging in a building's updraft | 8 m/s wind: climbs > 5 m, drifts < 40 m in 20 s; calm: sinks, travels > 100 m |
| Slow in calm air vs headwind | calm: stalls and dives; 8 m/s headwind: glides |
| Shear, building flow, rotated boxes, CityBreeze | see `test_wind.cpp`; flow continuous (incl. 45° boxes, raised bases) |
| Free look / head look | aim frozen, course held, neck limits, no flick, swing back, NaN-safe |
| Wind streaks | none in calm air; denser/longer in wind; along the wind; rise over a face; fps-independent |

Chart of the scripted scenarios: [img/flight_lab.svg](img/flight_lab.svg). To regenerate it, see
docs/BUILD.md.

## Playtest levers (all in `FFlightParams` / `FWindParams` / `FLookParams`)
| Feel question | Lever | Default |
|---|---|---|
| Level look = best glide, or nose level? | `GlideTrimAssist` 0..1 | 1 (best glide) |
| Pull-outs too snappy / too heavy? | `MaxLoadFactor` | 7 g (5 g ≈ 2.1 s, ~80 m) |
| Turns carve (stay banked) or roll level early? | `BankTrackBlend` 0..1, `BankTrackTimeSec` | 1, 0.25 s |
| Turns too wide / flick turns stall? | `TurnStallMargin` (>1 disables) | 0.9 |
| Wind strength / gustiness / shelter | `CityBreeze()` values, `ShearExponent` | ~5 m/s, 0.3 |
| Building lift strength | `RidgeLiftScale`, `WakeCalm`, `WakeTurbulence` | 0.6, 0.8, 0.35 |
| Streak amount / visibility | `NumStreaks`, `MaxAlpha`, `MinVisibleWindMps` | 160, 0.22, 1.5 |
| Camera swing-back speed, neck range | `ReturnTimeSec`, `NeckYawLimitDeg` | 0.3 s, 120° |

## Known gaps and open design questions (decide by playtest)
- **Flap + tuck priority.** Flap wins (owner: fine for now).
- **No stamina yet**, so flapping is unlimited.
- **No manual roll or yaw keys (A/D)** yet; banking is automatic.
- **Ground, landing and perching** are Unreal-side, not built yet.
- **Nose leads the path by ~20° in hard turns** (the turn stall protection's limit). If it looks
  like crabbing on the real model, the next step is the bird rolling and pulling (bank-to-turn)
  rather than yawing.
- **Building flow is a cheap approximation.** Streets between buildings (channeling), roof
  vortices and corner acceleration aren't modelled. Tune by eye against the streaks.
- Flapping while aiming straight up climbs forever (~4.75 m/s) until stamina exists.
- All numbers are first guesses at crow-like values. Expect heavy tuning in playtests.
