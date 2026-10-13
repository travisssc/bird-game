# Build, run, test

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
