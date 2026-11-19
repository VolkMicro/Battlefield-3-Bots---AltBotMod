# Requested combat and vehicle-control changes

## Accuracy / combat behaviour

This source revision tightens vehicle-mounted weapon fire control and shortens sustained gun bursts:

- Ground vehicle guns now require closer alignment (0.90 m target-plane window rather than 1.25 m).
- Coaxial machine guns use a 0.30 m alignment window rather than 0.40 m.
- Aircraft passenger/door guns use 1.4 m for infantry and 2.0 m for vehicles rather than 2.2 m / 3.5 m.
- Vehicle guns fire in 0.5 s bursts followed by 0.3 s pauses instead of 1.8 s bursts / 0.2 s pauses, reducing accumulated recoil and giving the aim controller time to settle.
- Existing infantry centre-mass aiming, impact-based calibration, recoil correction, and target leading are retained.

The mod's default configuration already selects Elite difficulty and Aggression=2 (Brutal). These changes do not remove reaction time, line-of-sight checks, weapon range limits, or projectile ballistics.

## Driver / seat-key takeover limitation

The supplied project is a dedicated-server DLL. It receives server-side player and vehicle state, but it does not receive raw client keyboard events. The E interaction and F1/F2/F3 seat-selection keys are interpreted by the BF3 client and translated into server requests. The current source does not include a verified hook for the server's human seat-change request path or a safe bot-eviction implementation.

For that reason, this revision does **not** claim to implement forced driver takeover or F1/F2/F3 bot-seat eviction. Doing so safely requires reverse-engineering and validating the exact server build's seat-entry message / method path, then testing occupied-seat eviction in a live server. Guessing a hook at the hard-coded native addresses could crash the server or corrupt vehicle occupancy.

## Validation

`validation/altbotmod_accuracy_checks.py` checks the updated fire-control constants. The full `validation/run_checks.py` currently stops in the pre-existing `air_native_tests.c` harness because `g_levelReadyAt` and `g_fleetPriorityUntil` are undeclared in that harness; this failure is outside the accuracy changes. A Windows 32-bit MinGW toolchain is required for the packaged `build.bat` native DLL build.
