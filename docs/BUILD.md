# Build, run, test

## Flight core (works everywhere, including cloud sessions)
```
cmake -S Tools/FlightCoreTests -B build/flightcore -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/flightcore
./build/flightcore/flightcore_tests            # all tests; add -s for details
./build/flightcore/flight_lab > build/lab.csv  # scripted flights
python3 Tools/FlightCoreTests/plot_lab.py build/lab.csv docs/img/flight_lab.svg
./build/flightcore/wind_lab > docs/img/wind_streaks.svg   # visible-wind preview
```
To look at an SVG without a browser (cloud sessions): run Chromium headless,
`/opt/pw-browsers/chromium-*/chrome-linux/chrome --headless --no-sandbox --screenshot=out.png --window-size=800,1000 file://$PWD/docs/img/wind_streaks.svg`.
Warnings are errors (`-Wshadow -Wconversion`, etc.) to approximate Unreal's strictness.
Also worth running occasionally: clang (`CXX=clang++`) and gcc with
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"` (clang's sanitizer runtime is missing in the
cloud image, so use gcc there). On Windows, any Visual Studio generator works.

## Unreal

> **Status: PROVISIONAL.** Written before the UE project existed. The first local session must
> run each command, fix it, and remove this note.

Assumes Windows and an Epic Launcher install. Set `UE_ROOT` to the engine folder, e.g.
`C:\Program Files\Epic Games\UE_5.x`. Project file: `BirdGame.uproject` in the repo root.

## Build (editor target)
```
"%UE_ROOT%\Engine\Build\BatchFiles\Build.bat" BirdGameEditor Win64 Development -Project="%CD%\BirdGame.uproject" -WaitMutex
```

## Test (headless, all project tests)
```
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "%CD%\BirdGame.uproject" -ExecCmds="Automation RunTests BirdGame; Quit" -unattended -nopause -nosplash -NullRHI -log -TestExit="Automation Test Queue Empty"
```
Results are in `Saved/Logs/BirdGame.log`. Use a Haiku subagent to summarize long logs, and check
the raw log for any failure it reports.

## Run the game (to exercise a feature)
```
"%UE_ROOT%\Engine\Binaries\Win64\UnrealEditor.exe" "%CD%\BirdGame.uproject" -game -windowed -ResX=1280 -ResY=720 -log
```
Screenshot from inside the game: console command `HighResShot 1` (saved under
`Saved/Screenshots/`). For scripted verification, prefer a dev-only console command or an
automation test that drives input, so checks are repeatable.

## Conventions
- Test names: `BirdGame.<Area>.<Behavior>`, e.g. `BirdGame.Flight.GlideSinkRate`.
- Test files: `Source/BirdGame/Tests/*.spec.cpp`, compiled only with `WITH_DEV_AUTOMATION_TESTS`.
