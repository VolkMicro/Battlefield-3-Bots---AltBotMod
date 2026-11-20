# Requested changes

What the testers asked for, what was done and what is still open, newest first. **Open**: not done yet. **To test**: in the build,
waiting for a playtest. **Done**: in the build and confirmed in a playtest (or needs none).

## Open

### Pathfinding (Mr.Cat, after playing Hunt v4)
"Can I make pathfinding?" - yes, Mr.Cat takes it on. Where it lives (`files/BotMod/src`):
- `nav.c`: the walk grid and the path search (`nav_find_path_sync`)
- `nav_async.inc`: the background planning queue
- `core.c`, `plan_path` / `plan_path_real`: how a bot asks for a path
- `trace_nav.c`, `trace_native.inc`: the saved routes (`BotMod_traces`)
- `zone_nav.c`: the objective mesh
- `route_actions_native.inc`: recorded actions along a route

Checks after a change: `validation/nav_queue_checks.py`, `navigation_search_limits.py`, `stability_navigation_checks.py`,
`hq_navigation_checks.py`, or everything with `validation/run_release_checks.py`. Work on top of the current `hunt` branch and send the
changed files or a diff. Wanted from the playtests: the maps and spots where bots get stuck (map name + `BotMod_<port>.log`).

### Driver / seat takeover (V36 request)
A player pressing E to take the driver seat from a bot, or F1/F2/F3 onto a seat a bot holds. Not done, and not safe to guess at:

The project is a dedicated-server DLL. It receives server-side player and vehicle state, but it does not receive raw client keyboard
events. The E interaction and F1/F2/F3 seat-selection keys are interpreted by the BF3 client and translated into server requests. The
current source does not include a verified hook for the server's human seat-change request path or a safe bot-eviction implementation.
Doing it safely needs the exact server build's seat-entry message / method path reverse-engineered and validated, then occupied-seat
eviction tested on a live server. Guessing a hook at the hard-coded native addresses could crash the server or corrupt vehicle occupancy.

## To test

### "Like an aimbot" (Mr.Cat, after playing Hunt v4)
"Way better", but as accurate as an aimbot. The aim model was rewritten after that playtest (Hunt v4, `bm_haim_*`: a sway around the
target, a miss on the swing onto a new target, a jolt when hit; one standard deviation 15 cm at 25 m, 35 cm at 100 m). Play it again; if
it is still too accurate, raise `HumanAimErrorMrad` (8 or 10, default 6). `SnapAim=0` turns the instant turning off, to compare.

## Done

### Accuracy beyond 20 m, bursts at range, recoil per weapon (Mr.Cat, 2026-11-19, after Hunt v2)
"The bullets go everywhere past 20 m; each weapon has its own recoil; shoot bursts at long range." Hunt v3: from 20 m out the pause after
a burst grows to the gun's settle time, recoil is learned per gun, nightmare stops and crouches to fire at range. Hunt v4: the human style's
partial recoil control on top. After v4: "way better".

### More aggressive bots (Mr.Cat, 2026-11-19)
The Hunt (up to 8 bots go after each player from several sides), the `nightmare` level, and in v4 every level closes in and chases 20 s
while `Hunt=1`. After v4: "way better".

### Keep the V39 style our own (Mr.Cat, 2026-11-19)
Done in v4: the style's settings are `HumanStyle` / `Human*`, and its aim model is new code.

### "The bots are the same" (Mr.Cat, 2026-11-19, after Hunt v1)
Hunt v2: the installer had kept the old `Difficulty`, so none of the new rules ran; snap aim; a chat line once a round shows which build
runs.

### Vehicle gun accuracy, shorter vehicle bursts (V36)
- Ground vehicle guns: a closer alignment window (V36 0.90 m on the target plane, was 1.25 m; V38 settled on 1.20 m for vehicles and
  0.22 m for infantry targets).
- Coaxial machine guns: a 0.30 m window (was 0.40 m).
- Aircraft passenger / door guns: 1.4 m for infantry and 2.0 m for vehicles (was 2.2 m / 3.5 m).
- Vehicle guns fire 0.5 s bursts with 0.3 s pauses (was 1.8 s / 0.2 s), so the recoil settles.
- Infantry centre-mass aiming, impact-based calibration, recoil correction and target leading stay.

These do not remove reaction time, line-of-sight checks, weapon range limits or projectile ballistics. Defaults since V39: Difficulty
normal, Aggression 1, `HumanStyle=1`.

## Validation

`validation/run_release_checks.py` runs the offline checks: 66 of 69 pass; `map-layout`, `altbotmod-v35` and
`altbotmod-route-coverage` need research files that are not in the repository. `validation/altbotmod_accuracy_checks.py` checks the
fire-control constants. The native DLLs need a Windows 32-bit MinGW toolchain (`files\BotMod\build.bat`); `files\server\BotModCore.dll`
in the `hunt` branch is already built.
