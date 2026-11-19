// AltBotMod Hunt v2: snap aim. In a fight a bot aims through the input's own aim fields (OFF_INPUT_AIM_VALID / YAW / PITCH)
// - what a player's client sends, and what this mod already does for a bot holding the key at an M-COM (mode_apply: the
// game arms only with the recorded aim, and it does) and for pilots (air_apply) - instead of turning with the yaw / pitch
// stick. The stick turns a soldier at a fixed rate and never past heading 0 one way (the engine keeps the heading in
// (-2pi, 0] and stops a raise at 0, see steer_bot), so an enemy just across north cost a turn the long way round - up to
// 350 degrees while it shot back - and any enemy behind cost seconds. Now the aim swings to the target at
// SnapAimDegreesPerSecond at most (720: half a turn in a quarter of a second), the stick stays at rest meanwhile (as at
// the M-COM), and the fields are let go the moment the fight ends. SnapAim: 0 off, 1 the nightmare level, 2 every level.
// Conventions, from the code that works: the recorded M-COM aims face (-sin yaw, cos yaw), so aim yaw = atan2(-dx, dz) in
// [0, 2pi), while steering turns the soldier's own yaw towards atan2(dx, dz): aim yaw = -(soldier yaw). Aim pitch = soldier
// pitch (+ up: the M-COM aims look down at -0.6, a revive at -1.2).
// Self-check (the engine's answer in a fight was never measured): while a bot snap-aims and has had to turn (its aim 0.1 rad
// or more from where it faced), its own yaw is compared with the aims it was given 1-6 frames before. 24 such frames
// decide: it follows -> on for the session; it follows with the other sign -> that sign, checked again; neither -> snap aim
// switches itself off for the session (the stick, as before) and the log says what it saw.
#define SNAP_HISTORY 8
static unsigned char g_snapOn[MAX_BOTS];                   // the aim fields are this bot's now
static float g_snapYaw[MAX_BOTS], g_snapPitch[MAX_BOTS];   // the aim given last (aim convention)
static float g_snapStart[MAX_BOTS];                        // the aim yaw it faced when it began
static float g_snapHist[MAX_BOTS][SNAP_HISTORY];           // the aims given, by frame & 7
static unsigned g_snapSince[MAX_BOTS];
static int g_snapSign = 1;                                 // 1: aim yaw = -(soldier yaw); -1: the other way round
static int g_snapState;                                    // 0 checking, 1 the soldiers follow, -1 off for the session
static unsigned g_snapSamples, g_snapFollow, g_snapOther, g_snapFrames, g_snapFlips;
static float g_snapMissCmd, g_snapMissSeen;                // the last sample that did not follow (for the log)
// The pitch the same way (a mirrored pitch would send every burst into the sky or the ground while the yaw check passed):
// aimed frames not firing, with an aim pitch 0.04 rad or more off level, compare the soldier's pitch with the pitch of
// 1-6 frames before. 12 decide; 9 mirrored -> the pitch sign turned round. This check never switches snap aim off.
static float g_snapPHist[MAX_BOTS][SNAP_HISTORY];
static int g_snapPitchSign = 1, g_snapPitchState;          // pitch: 0 checking, 1 decided
static unsigned g_snapPSamples, g_snapPFollow, g_snapPMirror;

static float snap_wrap2pi(float a) { a = fmodf(a, 6.2831853f); return a < 0 ? a + 6.2831853f : a; }
static float snap_wrappi(float a) { return remainderf(a, 6.2831853f); }
static float snap_aim_of(float soldierYaw) { return snap_wrap2pi(-(float)g_snapSign * soldierYaw); }   // soldier / steering yaw -> aim yaw

static const char *snap_state_text(void)
{
    if (g_enhanced.snapAim <= 0) return "off (SnapAim=0)";
    if (g_snapState < 0) return "OFF for this session - the soldiers did not follow the aim fields";
    if (g_snapState == 0) return g_enhanced.snapAim >= 2 ? "on, every level (self-check under way)" : "on, nightmare (self-check under way)";
    return g_enhanced.snapAim >= 2 ? "on, every level (checked: the soldiers follow)" : "on, nightmare (checked: the soldiers follow)";
}
static void snap_check_sample(int follow, int other, float cmd, float seen)
{
    g_snapSamples++;
    if (follow) g_snapFollow++; else if (other) g_snapOther++; else { g_snapMissCmd = cmd; g_snapMissSeen = seen; }
    if (g_snapSamples < 24) return;
    if (g_snapFollow >= 18) {
        g_snapState = 1;
        bot_log("snapaim: on - the soldiers follow the aim fields (%u of %u aimed frames, sign %d)", g_snapFollow, g_snapSamples, g_snapSign);
    } else if (g_snapOther >= 18 && !g_snapFlips) {
        g_snapSign = -g_snapSign; g_snapFlips++;
        bot_log("snapaim: the aim yaw runs the other way on this server (%u of %u) - turned round, checking again", g_snapOther, g_snapSamples);
        g_snapSamples = g_snapFollow = g_snapOther = 0;
    } else {
        g_snapState = -1;
        bot_log("snapaim: OFF for this session - the soldiers did not follow the aim fields (%u followed, %u the other sign, of %u; e.g. aim %.2f, "
                "soldier at %.2f) - bots turn with the stick as before", g_snapFollow, g_snapOther, g_snapSamples, g_snapMissCmd, g_snapMissSeen);
    }
}
static void snap_check_pitch(int follow, int mirror)
{
    g_snapPSamples++;
    if (follow) g_snapPFollow++; else if (mirror) g_snapPMirror++;
    if (g_snapPSamples < 12) return;
    if (g_snapPMirror >= 9 && g_snapPitchSign == 1) {
        bot_log("snapaim: the aim pitch runs the other way on this server (%u of %u) - turned round, checking again", g_snapPMirror, g_snapPSamples);
        g_snapPitchSign = -1; g_snapPSamples = g_snapPFollow = g_snapPMirror = 0;
        return;
    }
    g_snapPitchState = 1;
    bot_log("snapaim: pitch checked - %u of %u aimed frames on the pitch given, %u mirrored (sign %d)", g_snapPFollow, g_snapPSamples, g_snapPMirror, g_snapPitchSign);
}
static int snap_aim_wanted(int i, char *pl, char *s)
{
    if (g_enhanced.snapAim <= 0 || g_snapState < 0 || !s || player_vehicle(pl) || air_pilot(i) || g_air[i].phase) return 0;
    BotBrain *b = bot_brain(i);
    if (!b->enabled || b->task != TASK_FIGHT || !(*(float *)(s + OFF_SOLDIER_HEALTH) > 0)) return 0;
    const BMDifficulty *P = bot_profile(i);
    if (!P || (g_enhanced.snapAim < 2 && !P->push)) return 0;   // SnapAim=1: the nightmare level only
    // their own steering: a knifed fence, a vault, stairs, a ladder, a breach, a fall, a swim, the M-COM key
    if (H->frame < g_fenceUntil[i] || H->frame < g_vaultUntil[i] || g_stairOn[i] || g_paths[i].climbPhase || H->frame < g_breachUntil[i] ||
        g_gliding[i] || g_swimming[i] || rush_operator_holding(i)) return 0;
    return 1;
}
// Every frame, for every bot, in the input pass (after the stick levels are set, before the pilots' and the M-COM's aims).
static void snap_aim_apply(int i, void *in)
{
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    if (!snap_aim_wanted(i, pl, s)) {
        if (g_snapOn[i]) { *(int *)((char *)in + OFF_INPUT_AIM_VALID) = 0; g_snapOn[i] = 0; }   // the stick again
        return;
    }
    float yaw = *(float *)(s + OFF_SOLDIER_YAW), pitch = *(float *)(s + OFF_SOLDIER_PITCH);
    if (!isfinite(yaw) || !isfinite(pitch)) return;
    float seen = snap_aim_of(yaw);   // where it faces now, as an aim
    if (!g_snapOn[i]) {
        g_snapOn[i] = 1; g_snapSince[i] = H->frame; g_snapYaw[i] = g_snapStart[i] = seen; g_snapPitch[i] = pitch;
        for (int k = 0; k < SNAP_HISTORY; k++) { g_snapHist[i][k] = seen; g_snapPHist[i][k] = pitch; }
    } else if (g_snapPitchState == 0 && H->frame - g_snapSince[i] >= 4 && bot_brain(i)->step != 2) {
        // the pitch check (not while the trigger is down: the recoil lifts the soldier's pitch)
        for (int k = 1; k <= 6 && H->frame - (unsigned)k > g_snapSince[i]; k++) {
            float c = g_snapPHist[i][(H->frame - (unsigned)k) & (SNAP_HISTORY - 1)];
            if (fabsf(c) < 0.04f) continue;
            if (fabsf(pitch - c) < 0.04f || fabsf(pitch + c) < 0.04f || k == 6) { snap_check_pitch(fabsf(pitch - c) < 0.04f, fabsf(pitch + c) < 0.04f); break; }
        }
    }
    if (g_snapOn[i] && g_snapState == 0 && H->frame - g_snapSince[i] >= 4) {
        // the self-check: does its own yaw match an aim it was given 1-6 frames ago (one it had to turn for)?
        float other = snap_wrap2pi((float)g_snapSign * yaw), best = 9, bestOther = 9, cmd = 0; int used = 0;
        for (int k = 1; k <= 6 && H->frame - (unsigned)k > g_snapSince[i]; k++) {
            float c = g_snapHist[i][(H->frame - (unsigned)k) & (SNAP_HISTORY - 1)];
            if (fabsf(snap_wrappi(c - g_snapStart[i])) < 0.1f) continue;   // (no turn asked for: proves nothing)
            best = fminf(best, fabsf(snap_wrappi(seen - c))); bestOther = fminf(bestOther, fabsf(snap_wrappi(other - c)));
            if (!used) cmd = c;
            used = 1;
        }
        if (used) snap_check_sample(best < 0.08f, bestOther < 0.08f, cmd, seen);
        if (g_snapState < 0) { *(int *)((char *)in + OFF_INPUT_AIM_VALID) = 0; g_snapOn[i] = 0; return; }
    }
    // towards where the fight wants it to look (nav->targetYaw: the lead point, the chase, a grenade throw) and the fight's
    // pitch (g_aimPitch, with its learned corrections), at most SnapAimDegreesPerSecond either way
    float step = g_enhanced.snapAimDegrees * 0.01745329f / 30.0f, want = snap_aim_of(bot_nav(i)->targetYaw);
    float d = snap_wrappi(want - g_snapYaw[i]);
    g_snapYaw[i] = snap_wrap2pi(g_snapYaw[i] + fmaxf(-step, fminf(step, d)));
    float wantP = isfinite(g_aimPitch[i]) ? fmaxf(-1.45f, fminf(1.45f, g_aimPitch[i])) : 0.0f;   // (g_snapPitch: the soldier's convention)
    g_snapPitch[i] += fmaxf(-step, fminf(step, wantP - g_snapPitch[i]));
    float outP = (float)g_snapPitchSign * g_snapPitch[i];
    g_snapHist[i][H->frame & (SNAP_HISTORY - 1)] = g_snapYaw[i];
    g_snapPHist[i][H->frame & (SNAP_HISTORY - 1)] = g_snapPitch[i];   // (what the soldier's pitch should become)
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_YAW, 0.0f);     // the stick at rest while the aim fields steer (as at the M-COM)
    ((SetLevel_t)ADDR_ENTRYINPUT_SET_LEVEL)(in, EIA_PITCH, 0.0f);
    *(int *)((char *)in + OFF_INPUT_AIM_VALID) = 1;
    *(float *)((char *)in + OFF_INPUT_AIM_YAW) = g_snapYaw[i];
    *(float *)((char *)in + OFF_INPUT_AIM_PITCH) = outP;
    g_snapFrames++;
}
// Chat (!snapaim) and console (snapaim): the state; admins: 0 / 1 / 2 (a new value checks the engine's answer again).
static void snap_aim_command(const char *who, const char *arg)
{
    while (arg && *arg == ' ') arg++;
    if (arg && *arg) {
        if (who && !chat_is_admin(who)) { chat_say("[AltBotMod] %s: only admins can do that", who); return; }
        if (arg[0] < '0' || arg[0] > '2' || arg[1]) { if (who) chat_say("[AltBotMod] !snapaim 0 (off), 1 (nightmare bots), 2 (every level)"); return; }
        g_enhanced.snapAim = arg[0] - '0';
        g_snapState = 0; g_snapSamples = g_snapFollow = g_snapOther = 0;   // (check again: the answer may differ now)
    }
    bot_log("snapaim: %s; turns at most %.0f deg/s; %u aimed frames so far", snap_state_text(), g_enhanced.snapAimDegrees, g_snapFrames);
    if (who) chat_say("[AltBotMod] snap aim %s", snap_state_text());
}
