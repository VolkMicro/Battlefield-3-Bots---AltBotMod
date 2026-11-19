"""AltBotMod Hunt: offline checks for the hunting bots (hunt_native.inc) and the nightmare level (enhanced.c).

The real hunt_native.inc and enhanced.c are compiled against a mocked engine: 20 bots (10 a side), one human, a flat
world. Checked: who is sent (the nearest free bots, at most HuntersPerPlayer and 60 % of a team), the approach (one
straight in, the rest to flank points 25-40 m out on fixed bearings), what the hunters know (nothing in a fresh life's
first 6 s, then a rough fix HuntIntelMeters off, gunfire exact), letting go (the human dies / flies, a hunter takes a
vehicle, gets an order, dies, finds no route, hunting switched off) and the objective state handed back each time.
Not a live BF3 test.
"""
import argparse, subprocess, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'

HARNESS = r'''
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "enhanced.h"
#include "nav.h"
#define _stricmp strcasecmp
typedef long LONG;
#define MAX_BOTS 24
#define OFF_PLAYER_SOLDIER 16
#define OFF_PLAYER_ONLINE_NAME 64
#define OFF_SOLDIER_POSITION 0
#define OFF_SOLDIER_SPAWN_POSITION 16
#define OFF_SOLDIER_HEALTH 32
#define OFF_SOLDIER_YAW 36
enum { TASK_NONE, TASK_FOLLOW, TASK_DEFEND, TASK_SUPPLY, TASK_REPAIR, TASK_REVIVE, TASK_COVER, TASK_ROCKET, TASK_FIGHT };
typedef struct { int enabled, task, prevTask, targetFlag, enroute, waiting, crouched, step; unsigned retryAt; char *commander, *prevCommander; } BotBrain;
typedef struct { int mode; float targetYaw, x, z; } BotNav;
static struct { unsigned frame; int botCount; char *bots[MAX_BOTS]; float botMove[MAX_BOTS][3]; } host, *H = &host;
static BMConfig g_enhanced;
static LONG g_roundEpoch = 1;
// players: 20 bots + 1 human; soldiers (two per player: a life and the next)
#define NP 21
static char players[NP][256] __attribute__((aligned(16)));
static char soldiers[NP][2][64] __attribute__((aligned(16)));
static int teams[NP], lifeOf[NP];
static char *vehicleOf[NP];
static BotBrain brains[MAX_BOTS]; static BotNav navs[MAX_BOTS];
static struct { int phase; char *veh; int manual; } g_drive[MAX_BOTS];
static struct { int phase; } g_air[MAX_BOTS];
static struct { char *vehicle; } g_fleet[MAX_BOTS];
static unsigned char g_gliding[MAX_BOTS], g_hasSector[MAX_BOTS];
static unsigned g_sprintUntil[MAX_BOTS], g_lastSpawnedAt[MAX_BOTS]; static char *g_lastSpawnedBot[MAX_BOTS];
static struct { int pending, flag; float goal[3]; } g_flagRoute[MAX_BOTS];
static struct { int goalReady, routePending; unsigned plannedAt; } g_modeBot[MAX_BOTS];
static int localOrder[MAX_BOTS], operatorAt[MAX_BOTS], recon[MAX_BOTS], hasLauncher[MAX_BOTS];
static unsigned firedAt[NP]; static int spotted[NP];
static int g_lastPlanDeferred, plans, planFails, deferPlans, cancels, stands; static float planGoal[MAX_BOTS][3];
static NavGrid grid, *gridPtr = &grid;
static char pmBuf[512] __attribute__((aligned(16))); static void *pmArr[NP];
static int logs; static char lastChat[256];
static void bot_log(const char *f, ...) { (void)f; logs++; }
static void chat_say(const char *f, ...) { va_list a; va_start(a, f); vsnprintf(lastChat, sizeof lastChat, f, a); va_end(a); }
static int chat_is_admin(const char *who) { return who && !strcmp(who, "admin"); }
static BotBrain *bot_brain(int i) { return &brains[i]; }
static BotNav *bot_nav(int i) { return &navs[i]; }
static void bot_stand(int i) { (void)i; stands++; }
void nav_cancel_path(int i) { (void)i; cancels++; }
static NavGrid **nav_slot(void) { return &gridPtr; }
int nav_snap(const NavGrid *g, float x, float y, float z, float *out) { (void)g; (void)y; out[0] = x; out[1] = 0; out[2] = z; return 1; }
int nav_is_water(const NavGrid *g, float x, float y, float z) { (void)g; (void)y; (void)z; return x < -900; }
static int plan_budget_left(void) { return 1; }
static int plan_path(int i, char *s, float x, float y, float z) {
    (void)s; g_lastPlanDeferred = 0; plans++; planGoal[i][0] = x; planGoal[i][1] = y; planGoal[i][2] = z;
    if (deferPlans) { deferPlans--; g_lastPlanDeferred = 1; return 0; }
    if (planFails) { planFails--; return 0; }
    return 5;
}
static int idx(char *pl) { for (int k = 0; k < NP; k++) if (players[k] == pl) return k; return -1; }
static void *player_manager(void) { return pmBuf + 4; }   /* (+0xac lands 8-aligned, as on the 32-bit server) */
static int player_exists(char *pl) { return idx(pl) >= 0; }
static int is_bot(char *pl) { int k = idx(pl); return k >= 0 && k < 20; }
static int player_team(char *pl) { int k = idx(pl); return k >= 0 ? teams[k] : 0; }
static char *player_vehicle(char *pl) { int k = idx(pl); return k >= 0 ? vehicleOf[k] : NULL; }
static const char *vehicle_name(char *veh) { return veh; }
static int level_ready(void) { return 1; }
static int local_order_active(int i) { return localOrder[i]; }
static int rush_operator_holding(int i) { return operatorAt[i]; }
static int rush_operator_focus(int i, char *e) { (void)e; return operatorAt[i]; }
static int is_recon(int i) { return recon[i]; }
static int sniper_holding(int i) { (void)i; return 0; }
static int launcher_slot(int i) { return hasLauncher[i] ? 2 : -1; }
static int fired_within(char *pl, unsigned frames) { int k = idx(pl); return k >= 0 && firedAt[k] && H->frame - firedAt[k] < frames; }
static int spotted_cached(char *pl) { int k = idx(pl); return k >= 0 && spotted[k]; }
static float dist2(const float *a, const float *b) { float dx = a[0] - b[0], dz = a[2] - b[2]; return dx * dx + dz * dz; }
static void *find_player(const char *name) { for (int k = 0; k < NP; k++) if (!strcmp(players[k] + OFF_PLAYER_ONLINE_NAME, name)) return players[k]; return NULL; }
// snap aim (snap_aim.inc): the input structure, the stick levels, the steering states it stays out of
#define OFF_SOLDIER_PITCH 40
#define OFF_INPUT_AIM_VALID 0x1dc
#define OFF_INPUT_AIM_YAW 0x1f4
#define OFF_INPUT_AIM_PITCH 0x1f8
#define EIA_YAW 4
#define EIA_PITCH 5
typedef void (*SetLevel_t)(void *input, int action, float level);
static char inputs[MAX_BOTS][0x400] __attribute__((aligned(16))); static float levels[MAX_BOTS][8];
static void mock_set_level(void *in, int a, float v) { int i = (int)(((char *)in - inputs[0]) / 0x400); if (a >= 0 && a < 8) levels[i][a] = v; }
#define ADDR_ENTRYINPUT_SET_LEVEL mock_set_level
static int air_pilot(int i) { (void)i; return 0; }
static const BMDifficulty *profiles[MAX_BOTS];
static const BMDifficulty *bot_profile(int i) { return profiles[i]; }
static unsigned g_fenceUntil[MAX_BOTS], g_vaultUntil[MAX_BOTS], g_breachUntil[MAX_BOTS];
static unsigned char g_stairOn[MAX_BOTS], g_swimming[MAX_BOTS];
static struct { unsigned char climbPhase; } g_paths[MAX_BOTS];
static float g_aimPitch[MAX_BOTS];
#include "snap_aim.inc"
#include "hunt_native.inc"

static float *pos(int k) { return (float *)(soldiers[k][lifeOf[k]] + OFF_SOLDIER_SPAWN_POSITION); }
static void place(int k, float x, float z) {
    char *s = soldiers[k][lifeOf[k]];
    float p[3] = {x, 0, z}; memcpy(s + OFF_SOLDIER_POSITION, p, 12); memcpy(s + OFF_SOLDIER_SPAWN_POSITION, p, 12);
    float hp = 100; memcpy(s + OFF_SOLDIER_HEALTH, &hp, 4);
    memcpy(players[k] + OFF_PLAYER_SOLDIER, &s, sizeof s);
}
static void kill(int k) { char *n = NULL; memcpy(players[k] + OFF_PLAYER_SOLDIER, &n, sizeof n); }
static char *sold(int k) { char *s; memcpy(&s, players[k] + OFF_PLAYER_SOLDIER, sizeof s); return s; }
static void respawn(int k, float x, float z) { lifeOf[k] ^= 1; place(k, x, z); }
static void step(int frames) { for (int f = 0; f < frames; f++) { H->frame++; hunt_director_tick(); } }
static int hunters(void) { int n = 0; for (int i = 0; i < H->botCount; i++) if (hunt_quarry(i)) n++; return n; }
static void brain(int i) { if (brains[i].task == TASK_NONE) hunt_brain_tick(i, sold(i)); }
static const int HUMAN = 20;

int main(void)
{
    bm_defaults(&g_enhanced);
    assert(g_enhanced.hunt == 1 && g_enhanced.huntersPerPlayer == 8 && g_enhanced.huntIntelSeconds == 6 && g_enhanced.huntIntelMeters == 12);
    assert(g_enhanced.huntMaxMeters == 700 && g_enhanced.hearMeters == 120 && g_enhanced.difficulty == 5);
    printf("PASS: defaults - hunt on, 8 per player, rough fix every 6 s 12 m off, from 700 m, hearing 120 m; gunfight level still elite\n");
    grid.phase = 2;
    H->frame = 1000; H->botCount = 20;
    for (int k = 0; k < NP; k++) {
        snprintf(players[k] + OFF_PLAYER_ONLINE_NAME, 32, k == HUMAN ? "human" : "bot%d", k);
        teams[k] = k < 10 || k == HUMAN ? 1 : 2;
        if (k < 20) { H->bots[k] = players[k]; brains[k].enabled = 1; brains[k].targetFlag = -1; }
        pmArr[k] = players[k];
    }
    { void **a = pmArr; memcpy(pmBuf + 4 + 0xac, &a, sizeof a); unsigned c = NP; memcpy(pmBuf + 4 + 8, &c, sizeof c); }
    // the human at the origin; its team's bots 600 m south; the other team's bots east of it, 50 m apart from 50 m out
    place(HUMAN, 0, 0);
    for (int k = 0; k < 10; k++) place(k, (float)k * 10, -600);
    for (int k = 10; k < 20; k++) place(k, 50.0f * (k - 9), 0);
    for (int k = 0; k < 20; k++) { brains[k].targetFlag = 3; brains[k].enroute = 1; g_modeBot[k].goalReady = 1; }
    recon[19] = 1;   // (the furthest: a recon - never sent while SniperOverwatch is on)

    // 1. who is sent: the nearest free bots of the other team, one more per pass, at most 60 % of the team (6 of 10)
    step(10);
    assert(hunters() == 1 && hunt_quarry(10) == players[HUMAN]);
    assert(strstr(lastChat, "[AltBotMod] Hunt v2: up to 8 bots hunt each player, difficulty elite, snap aim on (checking)"));
    lastChat[0] = 0; step(30); assert(!lastChat[0]);   // (once a round)
    printf("PASS: the chat says once a round, as soon as a player is in, that this build runs and with what (hunt, difficulty, snap aim)\n");
    step(170);
    assert(hunters() == 6);
    for (int k = 10; k < 16; k++) assert(hunt_quarry(k) == players[HUMAN]);
    for (int k = 0; k < 10; k++) assert(!hunt_quarry(k));
    assert(g_hunterRole[10] == 0 && g_hunterRole[11] == 1 && g_hunterRole[12] == 2 && g_hunterRole[13] == 3 && g_hunterRole[14] == 0);
    printf("PASS: the nearest free bots of the other team are sent, one a pass, at most 60%% of a team (6 of 10); roles straight / left / right / behind\n");

    // 2. a fresh life: 6 s with nothing known - the hunters play the objectives meanwhile
    HuntTarget *T = hunt_target_of(players[HUMAN]);
    assert(T);
    unsigned born = 1000 + 10;   // (the first pass saw it)
    assert(H->frame >= born + HUNT_GRACE_FRAMES || T->at == 0);
    // the rough fix came after the grace: within 12 m of the truth, not exact
    assert(T->at && !T->exact && sqrtf(dist2(T->pos, pos(HUMAN))) <= 12.01f);
    printf("PASS: nothing known in a fresh life's first 6 s; then a rough fix within 12 m (not exact)\n");

    // 3. the approach: the first straight at the fix, the others to flank points 25-40 m out first
    plans = 0;
    brain(10);
    assert(g_huntDriving[10] && navs[10].mode == 3 && plans == 1 && brains[10].targetFlag == -1 && !brains[10].enroute);
    assert(dist2(planGoal[10], T->pos) < 0.01f);
    brain(11); brain(12); brain(13);
    for (int k = 11; k <= 13; k++) {
        float r = sqrtf(dist2(planGoal[k], T->pos));
        assert(r >= 24.9f && r <= 40.1f);
        assert(g_huntPhase[k] == 0);
    }
    {   // left and right flank points on opposite sides of the line from the human to the hunters, the third behind it
        float bx = 1, bz = 0;   // (the hunters came from the east)
        float l = (planGoal[11][0] - T->pos[0]) * -bz + (planGoal[11][2] - T->pos[2]) * bx;
        float r = (planGoal[12][0] - T->pos[0]) * -bz + (planGoal[12][2] - T->pos[2]) * bx;
        float behind = (planGoal[13][0] - T->pos[0]) * bx + (planGoal[13][2] - T->pos[2]) * bz;
        assert(l * r < 0 && behind < 0);
    }
    printf("PASS: one goes straight at it, the others to flank points 25-40 m out - left, right and behind it\n");
    // the human moves 5 m: no new route; 20 m: a new one - looked at 2 s after a route was made
    plans = 0;
    T->pos[0] += 5; H->frame += 61; brain(10);
    assert(plans == 0);
    T->pos[0] += 15; H->frame += 20; brain(10);
    assert(plans == 1 && dist2(planGoal[10], T->pos) < 0.01f);
    T->pos[0] += 15; H->frame += 20; brain(10);
    assert(plans == 1);   // (the next look comes 2 s after that route)
    H->frame += 45; brain(10); assert(plans == 2);
    printf("PASS: a route is renewed when the human is 8 m+ from where it was planned, at most every 2 s - not for every step\n");
    // close: no sprint in
    g_sprintUntil[10] = H->frame + 4; T->pos[0] = 50 + 20; T->pos[2] = 0; place(10, 50, 0); H->frame += 20; brain(10);
    assert(g_sprintUntil[10] == 0);
    printf("PASS: within 30 m of the human a hunter walks (it can shoot the moment it sees it)\n");
    // at the fix and nobody there: looks round, no route
    plans = 0; place(10, 69, 1); H->frame += 20; brain(10);
    assert(navs[10].mode == 1 && plans == 0);
    printf("PASS: at the fix with nobody there it looks round instead of planning again\n");
    // the flank point reached: in
    {   float R = 25.0f + (11 % 3) * 7.5f;   // (its flank point now: the human has moved since)
        place(11, T->pos[0] + sinf(g_huntAngle[11]) * R + 3, T->pos[2] + cosf(g_huntAngle[11]) * R); }
    H->frame += 61; brain(11);
    assert(g_huntPhase[11] == 1 && dist2(planGoal[11], T->pos) < 0.01f);
    printf("PASS: at its flank point a hunter goes in\n");

    // 4. gunfire: exact at once
    place(HUMAN, 30, 30); firedAt[HUMAN] = H->frame; step(10);
    assert(T->exact && dist2(T->pos, pos(HUMAN)) < 0.01f);
    hunt_saw(players[HUMAN], (float[3]){31, 0, 31}); assert(T->exact && T->pos[0] == 31);
    hunt_saw(players[3], (float[3]){1, 0, 1});   // (not hunted: nothing happens)
    printf("PASS: its gunfire and a bot seeing it give the exact place at once\n");

    // 5. a planner queue: the route waits, then comes
    deferPlans = 1; plans = 0; place(12, 300, 300); T->pos[0] = -200; H->frame += 20; brain(12);
    assert(g_huntPending[12] && plans == 1);
    H->frame += 3; brain(12); assert(!g_huntPending[12] && plans == 2 && navs[12].mode == 3);
    printf("PASS: a queued route is collected 3 frames later\n");

    // 6. a hunter that takes a vehicle is let go (and given back to the objectives); the next nearest takes its place
    g_drive[13].phase = 1; step(10);
    assert(!hunt_quarry(13) && !g_huntDriving[13] && hunters() == 6 && hunt_quarry(16));
    assert(H->frame < g_huntCooldown[13]);
    g_drive[13].phase = 0;
    printf("PASS: a hunter that takes a vehicle is let go (20 s before it can hunt again); the next nearest replaces it\n");
    // an order from a player, a recovery order: let go; the next nearest take the places - never the recon (19)
    brains[14].commander = players[HUMAN]; localOrder[15] = 1; step(10);
    assert(!hunt_quarry(14) && !hunt_quarry(15));
    brains[14].commander = NULL; localOrder[15] = 0;
    step(30);
    assert(hunters() == 6 && hunt_quarry(17) && hunt_quarry(18) && !hunt_quarry(19));
    printf("PASS: a player's order or a recovery order lets a hunter go; the next nearest take over, never a recon (snipers take perches)\n");
    // a hunter dies: let go (nobody free to replace it now: the others let go are still cooling off)
    kill(10); step(10);
    assert(!hunt_quarry(10) && !g_hunterOf[10] && hunters() == 5);
    printf("PASS: a dead hunter is let go\n");
    respawn(10, 50, 0);

    // 7. the human dies: every hunter goes back to the objectives (state handed back); a new life gets 6 s again
    for (int k = 0; k < 20; k++) if (hunt_quarry(k) && brains[k].task == TASK_NONE) { H->frame += 20; brain(k); }
    int driving = 0, wasDriving[20]; for (int k = 0; k < 20; k++) { wasDriving[k] = g_huntDriving[k]; driving += wasDriving[k]; } assert(driving > 0);
    for (int k = 0; k < 20; k++) if (wasDriving[k]) { brains[k].targetFlag = 2; brains[k].enroute = 1; g_modeBot[k].goalReady = 1; }   /* (stale) */
    kill(HUMAN); step(10);
    assert(hunters() == 0 && !hunt_target_of(players[HUMAN]));
    for (int k = 0; k < 20; k++) assert(!g_huntDriving[k] && !g_hunterOf[k]);
    for (int k = 0; k < 20; k++) if (wasDriving[k]) assert(brains[k].targetFlag == -1 && !brains[k].enroute && !g_modeBot[k].goalReady);
    respawn(HUMAN, -100, -100); step(10);
    T = hunt_target_of(players[HUMAN]); assert(T && !T->at);
    int sent = hunters(); assert(sent >= 1);
    for (int k = 10; k < 20; k++) if (hunt_quarry(k)) { int r = hunt_brain_tick(k, sold(k)); assert(r == 0 && !g_huntDriving[k]); }
    step(HUNT_GRACE_FRAMES); assert(T->at);
    printf("PASS: the human dies - all hunters back to the objectives; its next life: 6 s unknown, then hunted again\n");

    // 8. in an aircraft: not hunted; in a ground vehicle: only bots with a launcher keep at it
    vehicleOf[HUMAN] = "Vehicles/F18/F18_Super_Hornet"; step(10);
    assert(hunters() == 0);
    vehicleOf[HUMAN] = "Vehicles/M1A2/M1A2"; step(200);
    assert(hunters() > 0);
    for (int k = 10; k < 20; k++) if (hunt_quarry(k)) {
        hasLauncher[k] = k % 2; brains[k].task = TASK_NONE; H->frame += 20;
        int r = hunt_brain_tick(k, sold(k)); assert(r == (k % 2));
    }
    vehicleOf[HUMAN] = NULL;
    printf("PASS: a human in an aircraft is not hunted; in a tank only the hunters with a launcher keep after it\n");

    // 9. no route: the flank point gives way to straight in, then 4 failures let the hunter go for 30 s
    int h = -1; for (int k = 10; k < 20; k++) if (hunt_quarry(k)) { h = k; break; }
    assert(h >= 0);
    g_huntPhase[h] = 0; g_huntGoal[h][0] = 1e9f; planFails = 100; place(h, 400, 400);
    for (int t = 0; t < 12 && hunt_quarry(h); t++) { H->frame += 50; brain(h); }
    planFails = 0;
    assert(!hunt_quarry(h) && H->frame < g_huntCooldown[h]);
    printf("PASS: no route to the flank point - straight in; no route at all 4 times - let go for 30 s\n");

    // 10. switched off: everyone let go, nobody sent
    g_enhanced.hunt = 0; step(10);
    assert(hunters() == 0);
    step(100); assert(hunters() == 0);
    g_enhanced.hunt = 1; step(200); assert(hunters() > 0);
    hunt_command("someone", "off"); assert(g_enhanced.hunt == 1);   // (not an admin)
    hunt_command("admin", "off"); assert(g_enhanced.hunt == 0);
    hunt_command("admin", "4"); assert(g_enhanced.hunt == 1 && g_enhanced.huntersPerPlayer == 4);
    step(300); assert(hunters() == 4);
    hunt_command(NULL, "0"); assert(g_enhanced.hunt == 0); step(10); assert(hunters() == 0);   /* (the console: no admin check) */
    hunt_command("admin", "on"); assert(g_enhanced.hunt == 1 && g_enhanced.huntersPerPlayer == 8);   /* (on after 0: back to 8) */
    step(300); assert(hunters() == 6);
    printf("PASS: Hunt=0 lets every hunter go and sends none; !hunt on / off / <n> for admins only\n");
    // 11. a new round: nobody hunts anybody
    lastChat[0] = 0; g_roundEpoch++; step(1);
    assert(hunters() <= 1 && hunt_target_of(players[HUMAN]) && !hunt_target_of(players[HUMAN])->at);   /* (one sent in its first pass) */
    assert(strstr(lastChat, "Hunt v2"));
    printf("PASS: a new round starts afresh: last round's hunters and knowledge dropped, the chat line again\n");

    // 12. the kill feed's tally
    hunt_note_kill("bot12", "human", 1); hunt_note_kill("bot13", "human", 0); hunt_note_kill("human", "bot14", 0);
    hunt_note_kill("bot1", "human", 0);   /* (same team: not counted) */
    hunt_note_kill("human", "human", 0); hunt_note_kill("nobody", "human", 0); hunt_note_kill("", "bot12", 0);
    assert(g_huntKills[0] == 2 && g_huntKills[1] == 1 && g_huntKills[2] == 1);
    printf("PASS: the kill feed's tally - players killed by bots (head shots), bots killed by players; team kills, suicides, unknown names left out\n");

    // 13. snap aim
    const BMDifficulty *NM = bm_difficulty_level(6), *EL = bm_difficulty_level(5);
    int b = 3; char *sb = sold(b); float *yawp = (float *)(sb + OFF_SOLDIER_YAW), *pitp = (float *)(sb + OFF_SOLDIER_PITCH);
    int *valid = (int *)(inputs[b] + OFF_INPUT_AIM_VALID); float *aimY = (float *)(inputs[b] + OFF_INPUT_AIM_YAW), *aimP = (float *)(inputs[b] + OFF_INPUT_AIM_PITCH);
    g_enhanced.snapAim = 1; g_enhanced.snapAimDegrees = 720; profiles[b] = NM; brains[b].task = TASK_FIGHT; brains[b].enabled = 1;
    *yawp = -0.1f; *pitp = 0; navs[b].targetYaw = 0.2f; g_aimPitch[b] = 0.1f;   /* facing just one side of north, the target just the other */
    levels[b][EIA_YAW] = 0.7f; levels[b][EIA_PITCH] = 0.3f;
    H->frame++; snap_aim_apply(b, inputs[b]);
    assert(*valid == 1 && levels[b][EIA_YAW] == 0 && levels[b][EIA_PITCH] == 0);
    assert(fabsf(snap_wrappi(*aimY - (6.2831853f - 0.2f))) < 1e-4f && fabsf(*aimP - 0.1f) < 1e-4f);
    printf("PASS: snap aim - a nightmare bot in a fight aims through the aim fields (aim yaw = -soldier yaw), across north the short way (0.3 rad, the stick: 6), the stick at rest\n");
    navs[b].targetYaw = 0.2f + 3.14159265f; float before = *aimY; H->frame++; snap_aim_apply(b, inputs[b]);
    assert(fabsf(fabsf(snap_wrappi(*aimY - before)) - 720 * 0.01745329f / 30) < 1e-4f);
    printf("PASS: snap aim swings at SnapAimDegreesPerSecond at most (720: 0.42 rad a frame)\n");
    brains[b].task = TASK_NONE; H->frame++; snap_aim_apply(b, inputs[b]); assert(*valid == 0 && !g_snapOn[b]);
    *valid = 7; H->frame++; snap_aim_apply(b, inputs[b]); assert(*valid == 7);   /* (let go once: the fields are not its any more) */
    brains[b].task = TASK_FIGHT; profiles[b] = EL; *valid = 0; H->frame++; snap_aim_apply(b, inputs[b]); assert(*valid == 0);
    g_enhanced.snapAim = 2; H->frame++; snap_aim_apply(b, inputs[b]); assert(*valid == 1);
    g_fenceUntil[b] = H->frame + 30; H->frame++; snap_aim_apply(b, inputs[b]); assert(*valid == 0); g_fenceUntil[b] = 0;
    g_paths[b].climbPhase = 1; H->frame++; snap_aim_apply(b, inputs[b]); assert(*valid == 0); g_paths[b].climbPhase = 0;
    g_enhanced.snapAim = 0; H->frame++; snap_aim_apply(b, inputs[b]); assert(*valid == 0);
    printf("PASS: snap aim lets the fields go when the fight ends (once); SnapAim=1 nightmare only, 2 every level, 0 off; not on a fence, a ladder, stairs...\n");
    // the self-check: the engine follows / follows with the other sign / ignores it
    g_enhanced.snapAim = 1; profiles[b] = NM;
    for (int mode = 0; mode < 3; mode++) {
        g_snapState = 0; g_snapSign = 1; g_snapSamples = g_snapFollow = g_snapOther = g_snapFlips = 0; g_snapOn[b] = 0;
        *yawp = -1.0f;   /* (aims swept between 1.0 and 2.4 rad: never near 0 or pi, where both signs read the same) */
        for (int f = 0; f < 400 && (g_snapState == 0); f++) {
            navs[b].targetYaw = -(1.0f + 0.7f + 0.7f * sinf(f * 0.15f));
            H->frame++; snap_aim_apply(b, inputs[b]);
            if (!*valid) break;
            if (mode == 0) *yawp = -*aimY;                               /* follows: soldier yaw = -aim */
            else if (mode == 1) *yawp = *aimY - 6.2831853f;              /* the other sign, kept in (-2pi, 0] */
        }
        if (mode == 0) assert(g_snapState == 1 && g_snapSign == 1);
        if (mode == 1) assert(g_snapState == 1 && g_snapSign == -1 && g_snapFlips == 1);
        if (mode == 2) assert(g_snapState == -1 && !g_snapOn[b] && *valid == 0);
    }
    assert(!strcmp(snap_short(), "OFF (engine ignored it)"));
    H->frame++; snap_aim_apply(b, inputs[b]); assert(*valid == 0);   /* off for the session */
    snap_aim_command("admin", "1"); assert(g_snapState == 0 && g_enhanced.snapAim == 1);   /* an admin can ask for another check */
    printf("PASS: snap aim's self-check - the soldiers follow: on; they follow the other sign: turned round and on; they ignore it: off for the session, the stick as before\n");
    // the pitch check: the soldiers' pitch follows the aim pitch / mirrors it (then the sign turns round); never while firing
    for (int mode = 0; mode < 2; mode++) {
        g_snapState = 1; g_snapPitchState = 0; g_snapPitchSign = 1; g_snapPSamples = g_snapPFollow = g_snapPMirror = 0; g_snapOn[b] = 0;
        brains[b].step = 1; *pitp = 0; g_aimPitch[b] = 0.3f;
        for (int f = 0; f < 200 && !g_snapPitchState; f++) {
            H->frame++; snap_aim_apply(b, inputs[b]);
            *pitp = mode == 0 ? *aimP : -*aimP; *yawp = -*aimY;
        }
        if (mode == 0) assert(g_snapPitchState == 1 && g_snapPitchSign == 1 && g_snapPFollow >= 9);
        if (mode == 1) assert(g_snapPitchState == 1 && g_snapPitchSign == -1 && g_snapPFollow >= 9 && *aimP < -0.25f);   /* (the field now carries -0.3 for +0.3) */
    }
    g_snapPitchState = 0; g_snapPSamples = 0; brains[b].step = 2; g_snapOn[b] = 0;
    for (int f = 0; f < 60; f++) { H->frame++; snap_aim_apply(b, inputs[b]); *pitp = 1.0f; }
    assert(g_snapPSamples == 0);   /* (bursts: the recoil lifts the pitch - no samples) */
    g_snapPitchSign = 1; g_snapPitchState = 1;
    printf("PASS: snap aim's pitch check - followed: kept; mirrored: the sign turned round (the shots would have gone into the sky); no samples while firing\n");
    return 0;
}
'''

LEVELS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "enhanced.h"
int main(void)
{
    assert(bm_difficulty_parse("nightmare") == 6 && bm_difficulty_parse(" NightMare ") == 6 && bm_difficulty_parse("6") == 6);
    assert(bm_difficulty_parse("7") == -1 && bm_difficulty_parse("elite") == 5 && bm_difficulty_parse("off") == 0 && bm_difficulty_parse("brutal") == -1);
    assert(!strcmp(bm_difficulty_name(6), "nightmare") && !strcmp(bm_difficulty_name(5), "elite") && !strcmp(bm_difficulty_name(7), "?"));
    assert(bm_difficulty_level(9) == bm_difficulty_level(6) && bm_difficulty_level(0) == bm_difficulty_level(1));
    printf("PASS: levels off..nightmare (0-6), by name or number; out of range refused\n");
    for (int l = 1; l <= 5; l++) {
        const BMDifficulty *P = bm_difficulty_level(l);
        assert(P->noCoverMeters == 0 && P->chaseSeconds == 0 && P->push == 0 && P->dodge == 0 && P->detectSeconds == 0);
    }
    const BMDifficulty *E = bm_difficulty_level(5), *N = bm_difficulty_level(6);
    assert(E->reaction == 0.05f && E->burst == 26 && E->engageRange == 300 && E->coverBelow == 10);   // (elite untouched)
    assert(N->noCoverMeters == 40 && N->chaseSeconds == 20 && N->push == 1 && N->dodge == 3 && N->detectSeconds > 0.25f && N->detectSeconds < 0.35f);
    assert(N->reaction <= E->reaction && N->engageRange > E->engageRange && N->detectRange > E->detectRange && N->fightRange > E->fightRange);
    assert(N->aimMargin == E->aimMargin && N->aimTol == E->aimTol && N->pause == E->pause && N->burst >= E->burst);
    printf("PASS: easy..elite keep their profiles (no new behaviour); nightmare: elite's aim, longer reach, no cover within 40 m, 20 s chase, push, quick steps, 0.3 s looks\n");
    BMConfig c; bm_defaults(&c); BMDodgeStyle st;
    assert(bm_dodge_style(&c, N, &st) == 1 && st.level == 1.0f && !st.needThreat && st.maxMeters == 35 && st.stepMin == 8 && st.stepMax == 21 && st.restMax == 8);
    assert(bm_dodge_style(&c, E, &st) == 1 && st.needThreat && st.level < 1.0f);   // elite: the V14 short steps under fire, as before
    c.sideSteps = 0; assert(bm_dodge_style(&c, N, &st) == 0);
    printf("PASS: nightmare side steps whenever it trades shots within 35 m (SideSteps=0 still none); elite's as before\n");
    bm_defaults(&c); c.difficulty = 6; c.randomDifficulty = 1;
    char name[16]; for (int k = 0; k < 200; k++) { snprintf(name, sizeof name, "b%d", k); int l = bm_bot_difficulty(&c, name); assert(l >= 5 && l <= 6); }
    printf("PASS: RandomDifficulty at nightmare mixes elite and nightmare\n");
    assert(bm_set(&c, "Difficulty", "nightmare") == 1 && c.difficulty == 6);
    assert(bm_set(&c, "Hunt", "0") == 1 && c.hunt == 0 && bm_set(&c, "Hunt", "2") == -1);
    assert(bm_set(&c, "HuntersPerPlayer", "32") == 1 && bm_set(&c, "HuntersPerPlayer", "33") == -1 && bm_set(&c, "HuntersPerPlayer", "2.5") == -1);
    assert(bm_set(&c, "HuntIntelSeconds", "0") == 1 && c.huntIntelSeconds == 0 && bm_set(&c, "HuntIntelSeconds", "61") == -1);
    assert(bm_set(&c, "HuntIntelMeters", "100") == 1 && bm_set(&c, "HuntMaxMeters", "49") == -1 && bm_set(&c, "HuntMaxMeters", "3000") == 1);
    assert(bm_set(&c, "HearMeters", "0") == 1 && c.hearMeters == 0 && bm_set(&c, "HearMeters", "401") == -1);
    printf("PASS: the Hunt settings are read with their ranges (out of range: refused, the value kept)\n");
    return 0;
}
'''


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--sanitize', action='store_true'); a = ap.parse_args()
    san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-g'] if a.sanitize else ['-O1']
    env = {'ASAN_OPTIONS': 'detect_leaks=0'}
    with tempfile.TemporaryDirectory(prefix='bf3-hunt-') as tmp:
        tmp = Path(tmp)
        for name, body in (('levels', LEVELS), ('hunt', HARNESS)):
            c = tmp / (name + '.c'); c.write_text(body); exe = tmp / name
            subprocess.run(['gcc', *san, '-std=gnu11', '-Wall', '-Wextra', '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable',
                            '-I', str(SRC), str(c), str(SRC / 'enhanced.c'), str(SRC / 'air.c'), '-lm', '-o', str(exe)], check=True)
            r = subprocess.run([str(exe)], env=env, stdout=subprocess.PIPE, text=True)   # (sanitizer / assert reports go to stderr)
            print(r.stdout, end='', flush=True)
            if r.returncode: raise SystemExit(f'{name}: exit {r.returncode}')
    # wiring in core.c: where the hunt plugs in
    core = (SRC / 'core.c').read_text(encoding='utf-8-sig')
    assert 'if (b->task == TASK_NONE && hunt_brain_tick(i, soldier)) return;' in core
    assert core.index('hunt_brain_tick(i, soldier)) return;') < core.index('if (!mode_is_cq()) { mode_brain_tick(i,soldier); return; }')
    assert 'g_where="hunt";hunt_director_tick();' in core and '#include "hunt_native.inc"' in core
    assert 'static int is_recon(int i) { return strstr(BOT_ROSTER(H->bots[i]), "Recon") != NULL; }' in core
    assert 'hunt_owns(i)' in (SRC / 'director_native.inc').read_text()
    print('PASS: wiring - the hunt runs before the objective loop, the director leaves hunters alone, recon read from the roster identity')


if __name__ == '__main__':
    main()
