from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
src = ROOT / "files/BotMod/src"
profile = (src / "combat_profile_native.inc").read_text()
core = (src / "core.c").read_text()
air = (src / "air.c").read_text()
checks = {
    "infantry centre-mass firing window (V37: practical, no long-range aim lock)": "P->aimMargin * f * 2.6f" in profile,
    "vehicle/infantry turret firing windows (V38: practical lead-point tolerance)": "isVeh?1.20f:0.22f" in core,
    "coaxial machine-gun alignment tightened": "atanf(0.30f / fmaxf(1, dh))" in core,
    "aircraft door-gunner alignment tightened": "isVeh ? 2.0f : 1.4f" in core,
    "jet dogfight firing window tightened": "atanf(35/fmaxf(range,1))" in air,
    "jet ground-strafe firing window tightened": "atanf(20/fmaxf(range,1))" in air,
    "helicopter pilot firing window tightened": "o->targetAir ? 8.0f : 4.0f" in air,
    "vehicle guns use controlled recoil bursts": "#define GUN_BURST_ON           15" in core and "#define GUN_BURST_OFF          9" in core,
    "human-like infantry reaction time preserved": "InfantryReactionSeconds=0.25" in (ROOT / "files/BotMod/enhanced.ini").read_text(),
}
for label, ok in checks.items():
    print(("PASS" if ok else "FAIL") + ": " + label)
if not all(checks.values()):
    raise SystemExit(1)
