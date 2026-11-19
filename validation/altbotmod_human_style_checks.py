"""AltBotMod human style checks (V39 settings, AltBotMod Hunt v4 aim model): the settings, the wiring in core.c, and a native test
of the human-aim model bm_haim_* in enhanced.c (needs a C compiler)."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "files/BotMod/src"
core = (SRC / "core.c").read_text()
enh = (SRC / "enhanced.c").read_text()
hdr = (SRC / "enhanced.h").read_text()
ini = (ROOT / "files/BotMod/enhanced.ini").read_text()
ps = (ROOT / "files/ServerControl/ServerControl-Core.ps1").read_text()
def ini_value(key):
    m = re.search(r"(?m)^" + key + r"=(.*)$", ini)
    return m.group(1).strip() if m else None
KEYS = ("HumanStyle", "HumanAimErrorMrad", "HumanSniperAimErrorMrad", "HumanVehicleAimErrorMrad", "HumanAirAimErrorMrad", "HumanAimSkillSpread",
        "HumanRecoilControlBest", "HumanRecoilControlWorst", "HumanReactionSeconds", "HumanFirstShotSeconds", "HumanFovDegrees",
        "HumanShootBackMeters", "HumanSniperShootBackMeters", "HumanStrafe")
checks = {
    "ini: HumanStyle=1": ini_value("HumanStyle") == "1",
    "ini: human aim error 6 / sniper 3 / vehicle 3 / air 4 mrad": [ini_value(k) for k in ("HumanAimErrorMrad", "HumanSniperAimErrorMrad", "HumanVehicleAimErrorMrad", "HumanAirAimErrorMrad")] == ["6", "3", "3", "4"],
    "ini: ranges 70 / 150 / 150 m, reaction 0.25 s": [ini_value(k) for k in ("InfantryEngageMeters", "ReconEngageMeters", "SniperEngageMeters", "InfantryReactionSeconds")] == ["70", "150", "150", "0.25"],
    "ini: 160 degree view, shoot back 150 / 400 m": [ini_value(k) for k in ("HumanFovDegrees", "HumanShootBackMeters", "HumanSniperShootBackMeters")] == ["160", "150", "400"],
    "ini: side steps 3, damage x1": ini_value("SideSteps") == "3" and ini_value("BotDamage") == "1.0",
    "enhanced.c: every human-style key is parsed": all(('"%s"' % k) in enh for k in KEYS),
    "enhanced.c: SideSteps accepts 3": 'INT_KEY("SideSteps", sideSteps, 0, 3)' in enh,
    "enhanced.h: BMConfig has the human-style fields, the model its API": "int humanStyle;" in hdr and "void bm_haim_step(BMHumanAim *a" in hdr,
    "core.c: the error goes into the infantry aim point and pitch": "bm_haim_step(&g_humanAim[i], human_dt(i), sigma, skill, turnYaw, turnPitch, &ey, &ep);" in core and "+ humanPitchErr;" in core,
    "core.c: lead with motion taken in late": "human_lead_point(i, t, tp, dh, lag, lp)" in core and "human_track_velocity(i, now - " in core,
    "core.c: recoil only partly held down": "g_aimClimb[i] * recoilCtl" in core and "bm_haim_recoil_share(" in core,
    "core.c: calibration ignores the injected sideways error": "es = sideAt - injLat" in core,
    "core.c: a hit jolts the aim, a new target is a new take": "human_jolt(i);" in core and "human_new_target(i);" in core,
    "core.c: mounted guns get their steady-angle sway (not guided missiles)": "g_enhanced.humanStyle && !guided" in core and "bm_haim_sigma(errMrad, dh, 0.0f, 0.0f, skill, 0)" in core,
    "core.c: shoot back further than the fight range": "g_enhanced.humanShootBack" in core,
    "core.c: human profile applied after every config load": "bm_human_style_apply(&g_enhanced);" in core,
    "core.c: the per-bot state follows the bot list": all(("FORGET(%s);" % a) in core for a in ("g_humanAim", "g_humanTrack", "g_humanTrackAt", "g_humanTrackWho", "g_humanRealVel", "g_humanCycle")),
    "Server Control: every human-style key, SideSteps up to 3": all(("'%s'" % k) in ps for k in KEYS) and "'SideSteps' 'Combat' 'int' 0 3 3" in ps,
}
test = r'''
#include "enhanced.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail(const char *m) { printf("native: %s\n", m); return 1; }
static int close_to(float a, float b, float tol) { return fabsf(a - b) <= tol; }
int main(void) {
    srand(11);
    // size: 6 mrad at 25 m; part fixed miss in metres, part fixed angle - the angle shrinks with the distance, the miss in metres grows
    float s10 = bm_haim_sigma(6, 10, 0, 0, 1, 1), s25 = bm_haim_sigma(6, 25, 0, 0, 1, 1), s50 = bm_haim_sigma(6, 50, 0, 0, 1, 1);
    float s100 = bm_haim_sigma(6, 100, 0, 0, 1, 1), s200 = bm_haim_sigma(6, 200, 0, 0, 1, 1);
    if (!close_to(s25, 0.006f, 1e-6f) || !close_to(s100, 0.006f * sqrtf(0.7f / 16.0f + 0.3f), 1e-6f)) return fail("size at 25 / 100 m");
    if (!(s10 > s25 && s25 > s50 && s50 > s100 && s100 > s200)) return fail("the angle shrinks with the distance");
    if (!(10 * s10 < 25 * s25 && 25 * s25 < 50 * s50 && 50 * s50 < 100 * s100 && 100 * s100 < 200 * s200)) return fail("the miss in metres grows");
    if (!(200 * s200 < 8 * 25 * s25)) return fail("the miss grows slower than the distance");
    if (!close_to(bm_haim_sigma(6, 25, 4, 0, 1, 1), s25 * 1.48f, 1e-6f) || !close_to(bm_haim_sigma(6, 25, 0, 0.25f, 1, 1), s25 * 1.5f, 1e-6f)) return fail("moving / a crossing target");
    if (!close_to(bm_haim_sigma(6, 25, 0, 0, 1.5f, 1), s25 * 1.5f, 1e-6f)) return fail("skill scales it");
    if (bm_haim_sigma(0, 25, 0, 0, 1, 1) != 0.0f || bm_haim_sigma(6, 25, 0, 0, 0, 1) != 0.0f) return fail("0 mrad / 0 skill: a perfect aim");
    if (!close_to(bm_haim_sigma(3, 400, 9, 1, 1, 0), 0.003f, 1e-7f) || !close_to(bm_haim_sigma(3, 20, 0, 0, 1, 0), 0.003f, 1e-7f)) return fail("a mounted gun: a steady angle");
    // the sway: zero mean, sideways sd = sigma, up / down 3/4 of it, and slow (neighbouring frames close together)
    BMHumanAim a; bm_haim_reset(&a); float y, p; double sy = 0, syy = 0, sp = 0, spp = 0, step = 0, prev = 0; int n = 0;
    bm_haim_step(&a, 0, 0.01f, 1, 0, 0, &y, &p);
    for (int k = 0; k < 120000; k++) {
        bm_haim_step(&a, 1 / 30.0f, 0.01f, 1, 0, 0, &y, &p);
        if (k > 300) { sy += y; syy += (double)y * y; sp += p; spp += (double)p * p; step += fabs(y - prev); n++; }
        prev = y;
    }
    double my = sy / n, sdy = sqrt(syy / n - my * my), mp = sp / n, sdp = sqrt(spp / n - mp * mp);
    if (fabs(my) > 0.0015 || sdy < 0.0092 || sdy > 0.0108) return fail("sideways sway: mean 0, sd = sigma");
    if (sdp < 0.0069 || sdp > 0.0081) return fail("up / down sway: 3/4 of sigma");
    if (step / n > 0.004) return fail("the sway is slow (no frame-to-frame jitter)");
    // the take: a 0.4 rad swing onto a new target misses by a part of it (mostly past), and the miss settles
    int past = 0, takes = 0; float biggest = 0;
    for (int k = 0; k < 400; k++) {
        bm_haim_reset(&a); bm_haim_step(&a, 0, 0, 1, 0.4f, 0, &y, &p);
        if (fabsf(y) > 0.08f + 1e-6f) return fail("a take misses by at most 0.08 rad");
        if (y > 0) past++;
        if (y != 0) takes++;
        biggest = fmaxf(biggest, fabsf(y));
        for (int f = 0; f < 30; f++) bm_haim_step(&a, 1 / 30.0f, 0, 1, 0, 0, &y, &p);
        if (fabsf(y) > 0.003f) return fail("a take settles within a second");
    }
    if (past < 240 || past > 360 || takes < 390 || biggest < 0.05f) return fail("most takes swing past the target, some stop short");
    bm_haim_reset(&a); bm_haim_step(&a, 0, 0, 1, 0, 0, &y, &p);
    if (y != 0 || p != 0) return fail("no swing, no take error");
    // the jolt: up and to a side, gone in about a second
    bm_haim_reset(&a); bm_haim_step(&a, 0, 0, 1, 0, 0, &y, &p); bm_haim_jolt(&a, 1); bm_haim_step(&a, 1 / 30.0f, 0, 1, 0, 0, &y, &p);
    if (!(p > 0.007f && p < 0.016f) || !(fabsf(y) > 0.002f && fabsf(y) < 0.009f)) return fail("a jolt kicks up and to a side");
    for (int f = 0; f < 30; f++) bm_haim_step(&a, 1 / 30.0f, 0, 1, 0, 0, &y, &p);
    if (fabsf(p) > 0.0005f || fabsf(y) > 0.0005f) return fail("a jolt dies away");
    for (int k = 0; k < 20; k++) bm_haim_jolt(&a, 1.5f);
    bm_haim_step(&a, 0, 0, 1, 0, 0, &y, &p);
    if (p > 0.04f + 1e-6f || fabsf(y) > 0.03f + 1e-6f) return fail("jolts are capped");
    // each bot its own steadiness, the same all session; skill spread and recoil share follow it
    float lo = 1, hi = 0, sum = 0; char name[32];
    for (int k = 0; k < 400; k++) {
        snprintf(name, sizeof name, "[BOT] Assault US %d", k); float t = bm_haim_trait(name);
        if (t < 0 || t >= 1 || t != bm_haim_trait(name)) return fail("trait in [0, 1), stable");
        lo = fminf(lo, t); hi = fmaxf(hi, t); sum += t;
    }
    if (lo > 0.05f || hi < 0.95f || fabsf(sum / 400 - 0.5f) > 0.05f) return fail("traits spread over the bots");
    if (!close_to(bm_haim_skill(0, 0.5f), 0.5f, 1e-6f) || !close_to(bm_haim_skill(1, 0.5f), 1.5f, 1e-6f) || !close_to(bm_haim_skill(0.5f, 0.5f), 1.0f, 1e-6f)) return fail("skill spread");
    if (!close_to(bm_haim_recoil_share(0.9f, 0.5f, 0), 0.9f, 1e-6f) || !close_to(bm_haim_recoil_share(0.9f, 0.5f, 1), 0.5f, 1e-6f)) return fail("recoil share");
    // the profile every level plays with HumanStyle on, and the step pattern
    BMConfig c; bm_defaults(&c); bm_human_style_apply(&c);
    for (int lv = 1; lv <= 5; lv++) {
        const BMDifficulty *P = bm_difficulty_level(lv);
        if (P->engageRange != 70 || P->fightRange != 70 || P->fov != 80 || P->jumpFight != 0 || P->jumpRun != 0 || fabsf(P->reaction - 0.25f) > 1e-5f || P->settle != 6) return fail("profile");
    }
    BMDodgeStyle st; if (!bm_dodge_style(&c, bm_difficulty_level(5), &st) || st.stepMin != 30 || st.restMin != 45 || st.needThreat || st.hopScale != 0) return fail("step and shoot");
    c.humanStyle = 0; bm_human_style_apply(&c);
    if (bm_difficulty_level(5)->engageRange != 400) return fail("HumanStyle=0 keeps the V38 profile");
    puts("native: ok"); return 0;
}
'''
ok_native = False
cc = "gcc"
with tempfile.TemporaryDirectory() as d:
    t = Path(d) / "t.c"; t.write_text(test); exe = Path(d) / "t"
    try:
        subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(SRC), str(t), str(SRC / "enhanced.c"), str(SRC / "air.c"), str(SRC / "modes.c"), "-lm", "-o", str(exe)], check=True, capture_output=True, text=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True); print(r.stdout.strip()); ok_native = r.returncode == 0
    except (subprocess.CalledProcessError, FileNotFoundError) as ex:
        print("native test could not run:", getattr(ex, "stderr", ex))
checks["native: size, sway, take, jolt, steadiness, profile and step pattern of the human aim"] = ok_native
for label, ok in checks.items():
    print(("PASS" if ok else "FAIL") + ": " + label)
sys.exit(0 if all(checks.values()) else 1)
