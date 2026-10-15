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

The Unreal side (not written yet) owns collision, ground movement, camera, animation, VFX
and networking. It calls `BirdFlight::Step()` each tick while airborne.

## How it works (one substep, 120 Hz)
1. **Wing pose.** Wing extension moves toward a target at a limited rate. The target is set by
   priority: flap (1.0) > tuck (0.0) > glide (1.0) > relaxed (0.5).
2. **Orientation (player-driven).** The nose turns toward `AimDirection` (the camera forward)
   at a limited rate, slower with wings tucked. Bank follows a coordinated-turn rule,
   `tan(bank) = v * yawRate / g`, capped at 65°. When stalled (and not flapping), the
   effective aim is pulled toward the oncoming air, so the nose drops: that's stall recovery.
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
- Look level: a gentle, stable glide, about 9:1 at about 11 m/s.
- Look down: faster.
- Look up while gliding: you bleed speed and stall.
- Tuck: you drop like a stone, up to about 65 m/s.
- Pull out of a dive: the speed turns back into height (a zoom climb).

## Wind (`WindField.h`)
`SampleWind(params, position, time)` is deterministic, so clients and the server agree without
replicating wind state. It combines:
- Steady base wind.
- Smooth gusts: sum-of-sines that vary in time and space, seeded. Vertical gusts are 0.4× weaker.
- Up to 16 thermals: Gaussian updraft columns that fade near the top.

Building updrafts (ridge lift) and wind shadows need level geometry, so they'll be added on
the Unreal side. Visual wind (particles, streaks, foliage) is Unreal-side too.

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

Chart of the scripted scenarios: [img/flight_lab.svg](img/flight_lab.svg). To regenerate it, see
docs/BUILD.md.

## Known gaps and open design questions (decide by playtest)
- **Aim assist for gliding.** Should looking level give "best glide", or keep the honest
  physics (level = a slightly descending glide)? It's honest for now.
- **Flap + tuck priority.** Flap wins. Is that right in a dive?
- **No stamina yet**, so flapping is unlimited.
- **No manual roll or yaw keys (A/D)** yet; banking is automatic.
- **Ground, landing and perching** are Unreal-side, not built yet.
- All numbers are first guesses at crow-like values. Expect heavy tuning in playtests.
