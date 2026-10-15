# Vision and design notes

Owner's words, lightly organized. These are design intent, not implementation.

## The game
- Third-person bird game: fly around a city. Multiplayer, public, desktop.
- Like **"The Isle"** (survival as a dinosaur), but you're a bird. Pick your species.
- The fun is **flying** and **interacting with other birds**.
- Later: eat food on the ground, steal food out of NPC humans' hands.
- A city we build ourselves (real-world tiles dropped: too low-detail up close). A real-world
  *layout* generated from OpenStreetMap is a possible later option.
- Look: a cartoon/realism hybrid, more real than cartoon. Not photorealistic.

## V1: flight mechanics (the only focus until it feels great)
Initial control proposal (to be tuned in playtests):
| Input | Action |
|---|---|
| Mouse | Steer / look (camera-relative flight) |
| Hold **Space** | Glide: wings spread, trade height for distance, slow sink |
| Hold **Left Click** | Flap: gain lift/speed, costs stamina later |
| Hold **Shift** | Tuck wings: less drag, faster, dive |
| W/A/S/D (proposed) | Pitch/roll fine control, or walking on the ground |

Physics/feel goals:
- Momentum and energy: diving builds speed, pulling up trades speed for height.
- Banking turns. Stall when too slow. Landing and takeoff.
- **Wind**: physical (gusts, updrafts/thermals off buildings, turbulence) **and visual**
  (particles/streaks, feathers/foliage responding, speed lines when diving).
- A third-person camera that conveys speed (FOV, lag, shake when diving).

Ideas to raise with the owner as we go (not committed): stamina, thermals to circle in,
perching on ledges/wires, flocking/formation bonuses, ruffled feathers in turbulence,
species stat differences (crow = agile, gull = glider, pigeon = sprinter).
