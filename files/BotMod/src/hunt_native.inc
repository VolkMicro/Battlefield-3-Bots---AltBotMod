// AltBotMod Hunt: bots that go looking for the human players.
//
// Before this the bots only ever ran flag to flag (Team Deathmatch: to the nearest enemy, bot or human alike), so a human
// could spend a round on a quiet flank and meet only the bots that happened by. Now, for every human on foot, up to
// HuntersPerPlayer bots of the other team - the nearest free ones on foot - go after it: the first straight in, the others
// round its flanks and from behind (a point 25-40 m out on their own side of it, then in), so it is caught from several
// sides at once. What they know of where it is: whatever a bot of their team sees (exact), its gunfire (the minimap's red
// dot) and a spot (exact), otherwise a rough fix every HuntIntelSeconds (HuntIntelMeters off). A fresh life gets 6 s
// before the first rough fix. A hunter is let go when it dies, takes a vehicle, gets a player's order or a recovery
// order, operates an M-COM, finds no route, or when the human dies or takes to the air; the nearest free bots take the
// places. The rest of the team plays the objectives: at most 60 % of a team hunts at a time. The fights themselves are
// the ordinary ones (detect_tick / fight_tick) - a hunter that sees its human prefers it to any other target.
#define HUNT_TARGETS       16
#define HUNT_GRACE_FRAMES  (6 * 30)    // a fresh life: no rough fix for this long
#define HUNT_STALE_FRAMES  (45 * 30)   // nothing learned for this long (HuntIntelSeconds=0): back to the objectives meanwhile
typedef struct HuntTarget {
    char *player, *soldier;      // the human and its current life
    float pos[3];                // where its hunters think it is
    unsigned at, pingAt;         // when that was learned (0: nothing yet this life) / the next rough fix
    unsigned char exact;         // the last fix was exact (seen, heard, spotted)
    int hunters;                 // bots after it (counted by the director each pass)
} HuntTarget;
static HuntTarget g_huntT[HUNT_TARGETS];
static LONG g_huntEpoch = -1;
static char *g_hunterOf[MAX_BOTS], *g_hunterSoldier[MAX_BOTS];   // the human a bot hunts, and the bot's life it was given to
static unsigned char g_hunterRole[MAX_BOTS], g_huntDriving[MAX_BOTS], g_huntPhase[MAX_BOTS], g_huntPending[MAX_BOTS], g_huntFails[MAX_BOTS];
static float g_huntAngle[MAX_BOTS], g_huntGoal[MAX_BOTS][3];
static unsigned g_huntPlanAt[MAX_BOTS], g_huntScanAt[MAX_BOTS], g_huntCooldown[MAX_BOTS], g_huntRoutedAt[MAX_BOTS];
static unsigned g_huntAssigned, g_huntReleased, g_huntPlans, g_huntRough, g_huntExact;
static const char *const kHuntRole[4] = {"straight in", "round its left", "round its right", "from behind"};

static HuntTarget *hunt_target_of(char *pl)
{
    if (!pl) return NULL;
    for (int k = 0; k < HUNT_TARGETS; k++) if (g_huntT[k].player == pl) return &g_huntT[k];
    return NULL;
}
// Someone of the hunters' side saw the human at pos (detect_tick, fight_tick) or it shot at a hunter from afar.
static void hunt_saw(char *pl, const float *pos)
{
    HuntTarget *T = g_enhanced.hunt ? hunt_target_of(pl) : NULL;
    if (!T || !T->soldier || !pos || !isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[2])) return;
    memcpy(T->pos, pos, 12); T->at = H->frame ? H->frame : 1; T->exact = 1;
}
static char *hunt_quarry(int i)
{
    if (!g_enhanced.hunt || i < 0 || i >= H->botCount || !g_hunterOf[i]) return NULL;
    return g_hunterSoldier[i] == *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER) ? g_hunterOf[i] : NULL;
}
// The hunt drives this bot's legs right now (the objective director leaves it alone meanwhile).
static int hunt_owns(int i) { return i >= 0 && i < H->botCount && g_huntDriving[i] && hunt_quarry(i) != NULL; }

// The objective loop gets the bot back and plans afresh: nothing of a route to a flag / station survives the hunt.
static void hunt_stop_driving(int i)
{
    if (!g_huntDriving[i]) return;
    g_huntDriving[i] = 0; g_huntPending[i] = 0;
    BotBrain *b = bot_brain(i);
    if (b->task == TASK_NONE) { nav_cancel_path(i); bot_nav(i)->mode = 0; memset(H->botMove[i], 0, sizeof H->botMove[i]); }
    if (b->task != TASK_NONE && b->task != TASK_FIGHT) return;   // (a player's order / a job: its own task drives, end_task resets)
    b->targetFlag = -1; b->enroute = b->waiting = 0; b->retryAt = 0;
    memset(&g_flagRoute[i], 0, sizeof g_flagRoute[i]);
    g_modeBot[i].goalReady = 0; g_modeBot[i].routePending = 0; g_modeBot[i].plannedAt = 0;
}
static void hunt_take_over(int i)
{
    BotBrain *b = bot_brain(i);
    g_huntDriving[i] = 1; g_huntPending[i] = 0; g_huntPlanAt[i] = 0; g_huntFails[i] = 0; g_huntRoutedAt[i] = 0;
    g_huntGoal[i][0] = g_huntGoal[i][2] = 1e9f; g_huntGoal[i][1] = 0;
    nav_cancel_path(i); memset(&g_flagRoute[i], 0, sizeof g_flagRoute[i]);
    b->targetFlag = -1; b->enroute = b->waiting = 0; g_hasSector[i] = 0;
    if (b->crouched) { bot_stand(i); b->crouched = 0; }
    bot_nav(i)->mode = 0;
}
static void hunt_release(int i, const char *why)
{
    if (!g_hunterOf[i]) return;
    char *q = g_hunterOf[i];
    hunt_stop_driving(i);
    g_hunterOf[i] = g_hunterSoldier[i] = NULL;
    g_huntReleased++;
    if (g_huntReleased <= 200 || g_huntReleased % 50 == 0)
        bot_log("hunt: %s stops hunting %s (%s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, player_exists(q) ? q + OFF_PLAYER_ONLINE_NAME : "a player who left", why);
}

// Why this bot cannot hunt now (NULL: it can). `held`: it hunts already (a fight or a short job keeps it on).
static const char *hunt_unfit(int i, int held)
{
    char *pl = H->bots[i], *s = *(char **)(pl + OFF_PLAYER_SOLDIER);
    BotBrain *b = bot_brain(i);
    if (!b->enabled) return "brain off";
    if (!s || *(float *)(s + OFF_SOLDIER_HEALTH) <= 0 || (held && s != g_hunterSoldier[i])) return "died";
    if (player_vehicle(pl) || g_drive[i].phase || g_air[i].phase || g_fleet[i].vehicle || g_gliding[i]) return "took a vehicle";
    if (b->commander || b->prevCommander || b->task == TASK_DEFEND || b->task == TASK_FOLLOW) return "a player's order";
    if (local_order_active(i)) return "a recovery order";
    if (rush_operator_holding(i) || rush_operator_focus(i, NULL)) return "at an M-COM";
    if (held) return NULL;
    // a new hunter: free, on foot, not a sniper with a perch to go to, settled after its spawn, not just let go
    if (b->task != TASK_NONE && !(b->task == TASK_FIGHT && b->prevTask == TASK_NONE)) return "busy";
    if ((is_recon(i) && g_enhanced.sniperOverwatch) || sniper_holding(i)) return "a sniper";
    if (H->frame - g_lastSpawnedAt[i] < 10 * 30 && g_lastSpawnedBot[i] == pl) return "just spawned";
    if (H->frame < g_huntCooldown[i]) return "just let go";
    return NULL;
}

static void hunt_release_all(const char *why)
{
    for (int i = 0; i < H->botCount; i++) if (g_hunterOf[i]) hunt_release(i, why);
    memset(g_huntT, 0, sizeof g_huntT);
}

static void hunt_assign(int i, HuntTarget *T, float meters)
{
    char *s = *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER);
    const float *me = (float *)(s + OFF_SOLDIER_POSITION), *tp = (float *)(T->soldier + OFF_SOLDIER_SPAWN_POSITION);
    static const float kTurn[4] = {0.0f, 1.2f, -1.2f, 2.6f};   // straight, ~70 deg either side, ~150 deg (behind it)
    int role = T->hunters % 4;
    g_hunterOf[i] = T->player; g_hunterSoldier[i] = s; g_hunterRole[i] = (unsigned char)role;
    // the approach side is fixed now, on a compass bearing from the human: it keeps it while the human moves
    g_huntAngle[i] = atan2f(me[0] - tp[0], me[2] - tp[2]) + kTurn[role];
    g_huntPhase[i] = role == 0;   // the first one goes straight in, the rest to their flank point first
    g_huntDriving[i] = 0; g_huntPending[i] = 0; g_huntFails[i] = 0; g_huntPlanAt[i] = 0;
    g_huntAssigned++;
    if (g_huntAssigned <= 200 || g_huntAssigned % 50 == 0)
        bot_log("hunt: %s goes after %s (%.0f m away, %s)", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, T->player + OFF_PLAYER_ONLINE_NAME, meters, kHuntRole[role]);
}

// 3 times a second: who the humans are, what the bots know of where they are, who hunts whom.
static void hunt_director_tick(void)
{
    static unsigned at, wasOn;
    if (g_huntEpoch != g_roundEpoch) {   // a new round / level: nobody hunts anybody yet
        memset(g_huntT, 0, sizeof g_huntT); memset(g_hunterOf, 0, sizeof g_hunterOf); memset(g_hunterSoldier, 0, sizeof g_hunterSoldier);
        memset(g_huntDriving, 0, sizeof g_huntDriving); memset(g_huntCooldown, 0, sizeof g_huntCooldown);
        g_huntEpoch = g_roundEpoch; at = 0;
    }
    if (!level_ready() || (at && H->frame - at < 10)) return;
    at = H->frame ? H->frame : 1;
    if (!g_enhanced.hunt || g_enhanced.huntersPerPlayer <= 0) {
        if (wasOn) { hunt_release_all("hunting switched off"); wasOn = 0; bot_log("hunt: off - the bots play the objectives only"); }
        return;
    }
    if (!wasOn) { wasOn = 1; bot_log("hunt: on - up to %d bot(s) hunt each human player", g_enhanced.huntersPerPlayer); }
    char *pm = player_manager();
    if (!pm) return;
    void **arr = NULL; unsigned count = 0;
    memcpy(&arr, pm + 0xac, sizeof arr); memcpy(&count, pm + 8, sizeof count);
    unsigned char live[HUNT_TARGETS] = {0};
    // 1. the humans worth hunting: alive, on a team, not in an aircraft (the AA's job)
    for (unsigned k = 0; arr && k < count && k < 64; k++) {
        char *pl = arr[k];
        if (!pl || is_bot(pl)) continue;
        int team = player_team(pl);
        char *s = team >= 1 && team <= 2 ? *(char **)(pl + OFF_PLAYER_SOLDIER) : NULL;
        if (!s || !(*(float *)(s + OFF_SOLDIER_HEALTH) > 0)) continue;
        char *veh = player_vehicle(pl);
        if (veh && bm_vehicle_profile(vehicle_name(veh))->kind == BM_AIR) continue;
        HuntTarget *T = hunt_target_of(pl);
        for (int t = 0; !T && t < HUNT_TARGETS; t++) if (!g_huntT[t].player) { T = &g_huntT[t]; memset(T, 0, sizeof *T); T->player = pl; }
        if (!T) continue;
        live[T - g_huntT] = 1;
        float *p = (float *)(s + OFF_SOLDIER_SPAWN_POSITION);
        if (!isfinite(p[0]) || !isfinite(p[1]) || !isfinite(p[2])) continue;
        if (T->soldier != s) {   // a new life: nobody knows yet where it spawned; last life's hunters are across the map now
            for (int i = 0; i < H->botCount; i++) if (g_hunterOf[i] == pl) hunt_release(i, "its quarry respawned");
            T->soldier = s; T->at = 0; T->exact = 0; T->pingAt = H->frame + HUNT_GRACE_FRAMES;
        }
        if (fired_within(pl, 15) || spotted_cached(pl)) {   // its gunfire on the minimap, or spotted: exact
            memcpy(T->pos, p, 12); T->at = H->frame ? H->frame : 1; T->exact = 1; g_huntExact++;
        } else if (g_enhanced.huntIntelSeconds > 0 && H->frame >= T->pingAt) {   // a rough fix, HuntIntelMeters off
            float r = g_enhanced.huntIntelMeters * sqrtf((float)(rand() % 1000) / 1000.0f), a = (float)(rand() % 6283) / 1000.0f;
            T->pos[0] = p[0] + sinf(a) * r; T->pos[1] = p[1]; T->pos[2] = p[2] + cosf(a) * r;
            T->at = H->frame ? H->frame : 1; T->exact = 0; g_huntRough++;
            T->pingAt = H->frame + (unsigned)(g_enhanced.huntIntelSeconds * 30);
        }
    }
    for (int t = 0; t < HUNT_TARGETS; t++) {   // dead, gone, flying: its hunters go back to the objectives
        if (!g_huntT[t].player || live[t]) continue;
        char *pl = g_huntT[t].player;
        for (int i = 0; i < H->botCount; i++) if (g_hunterOf[i] == pl) hunt_release(i, "its quarry is dead, gone or flying");
        memset(&g_huntT[t], 0, sizeof g_huntT[t]);
    }
    // 2. the hunters still fit for it
    int hunting[3] = {0}, bots[3] = {0};
    for (int t = 0; t < HUNT_TARGETS; t++) g_huntT[t].hunters = 0;
    for (int i = 0; i < H->botCount; i++) {
        int team = player_team(H->bots[i]);
        if (team >= 1 && team <= 2) bots[team]++;
        if (!g_hunterOf[i]) continue;
        HuntTarget *T = hunt_target_of(g_hunterOf[i]);
        const char *why = !T ? "no quarry" : team == player_team(T->player) ? "on its quarry's team now" : hunt_unfit(i, 1);
        if (why) { hunt_release(i, why); if (strcmp(why, "died")) g_huntCooldown[i] = H->frame + 20 * 30; continue; }
        T->hunters++;
        if (team >= 1 && team <= 2) hunting[team]++;
    }
    // more than HuntersPerPlayer (lowered by "!hunt <n>" or a settings reload): the furthest go back to the objectives
    for (int t = 0; t < HUNT_TARGETS; t++) {
        HuntTarget *T = &g_huntT[t];
        while (T->player && T->soldier && T->hunters > g_enhanced.huntersPerPlayer) {
            const float *tp = (float *)(T->soldier + OFF_SOLDIER_SPAWN_POSITION);
            int worst = -1; float furthest = -1;   // (not "far": a macro in the Windows headers)
            for (int i = 0; i < H->botCount; i++) {
                char *s = g_hunterOf[i] == T->player ? *(char **)(H->bots[i] + OFF_PLAYER_SOLDIER) : NULL;
                float d = s ? dist2((float *)(s + OFF_SOLDIER_POSITION), tp) : -1;
                if (s && d > furthest) { furthest = d; worst = i; }
            }
            if (worst < 0) break;
            int team = player_team(H->bots[worst]);
            hunt_release(worst, "more hunters than HuntersPerPlayer");
            T->hunters--;
            if (team >= 1 && team <= 2) hunting[team]--;
        }
    }
    // 3. short of hunters: the nearest free bot of the other team joins, one per human per pass (no burst of routes)
    for (int t = 0; t < HUNT_TARGETS; t++) {
        HuntTarget *T = &g_huntT[t];
        if (!T->player || !T->soldier) continue;
        int team = player_team(T->player), other = 3 - team;
        if (team < 1 || team > 2 || T->hunters >= g_enhanced.huntersPerPlayer || hunting[other] >= bots[other] * 6 / 10) continue;
        const float *tp = (float *)(T->soldier + OFF_SOLDIER_SPAWN_POSITION);
        int best = -1; float bestD = g_enhanced.huntMaxMeters * g_enhanced.huntMaxMeters;
        for (int i = 0; i < H->botCount; i++) {
            if (g_hunterOf[i] || player_team(H->bots[i]) != other || hunt_unfit(i, 0)) continue;
            float d = dist2((float *)(*(char **)(H->bots[i] + OFF_PLAYER_SOLDIER) + OFF_SOLDIER_POSITION), tp);
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best < 0) continue;
        hunt_assign(best, T, sqrtf(bestD));
        T->hunters++; hunting[other]++;
    }
}

// The brain of a hunter with nothing else to do (task none): towards the human - its flank point first, then in. Returns
// 1 when it drove the bot this tick (the objective loop is skipped), 0 to leave the bot to the objectives meanwhile.
static int hunt_brain_tick(int i, char *soldier)
{
    char *q = g_hunterOf[i];
    if (!g_enhanced.hunt || !q || g_hunterSoldier[i] != soldier) { hunt_stop_driving(i); return 0; }
    if (!player_exists(q)) { hunt_stop_driving(i); return 0; }   // left the server since the last pass (the director lets it go)
    HuntTarget *T = hunt_target_of(q);
    // nothing known of this life yet, nothing new for 45 s, or it sits in a vehicle and this bot has no launcher for it
    int armed = !player_vehicle(q) || launcher_slot(i) >= 0;
    if (!T || !T->soldier || !T->at || H->frame - T->at > HUNT_STALE_FRAMES || !armed) { hunt_stop_driving(i); return 0; }
    if (!g_huntDriving[i]) hunt_take_over(i);
    BotNav *nav = bot_nav(i);
    float *me = (float *)(soldier + OFF_SOLDIER_POSITION), *tp = T->pos, d2t = dist2(me, tp);
    // close: no sprinting in - a bot that walks the last 30 m can shoot the moment it sees it
    if (d2t < 30.0f * 30.0f) g_sprintUntil[i] = 0;
    // where it is believed to be and nobody there: look round until the next fix moves it
    if (g_huntPhase[i] && d2t < 6.0f * 6.0f && fabsf(me[1] - tp[1]) < 3.0f) {
        if (nav->mode == 3) nav_cancel_path(i);
        nav->mode = 1;
        if (H->frame >= g_huntScanAt[i]) { g_huntScanAt[i] = H->frame + 20; nav->targetYaw = *(float *)(soldier + OFF_SOLDIER_YAW) + 1.3f; }
        memcpy(g_huntGoal[i], tp, 12); g_huntPending[i] = 0;
        return 1;
    }
    if (H->frame < g_huntPlanAt[i]) return 1;
    g_huntPlanAt[i] = H->frame + 15;
    float goal[3];
    memcpy(goal, tp, 12);
    if (!g_huntPhase[i]) {   // its flank point: 25-40 m out from the human on its own bearing, on the human's floor
        float R = 25.0f + (float)(i % 3) * 7.5f, ap[3] = {tp[0] + sinf(g_huntAngle[i]) * R, tp[1], tp[2] + cosf(g_huntAngle[i]) * R};
        NavGrid *g = *nav_slot();
        if (d2t < R * R * 0.64f || !g || g->phase != 2 || !nav_snap(g, ap[0], ap[1], ap[2], goal) || fabsf(goal[1] - tp[1]) > 5.0f ||
            nav_is_water(g, goal[0], goal[1], goal[2])) { g_huntPhase[i] = 1; memcpy(goal, tp, 12); }
        else if (dist2(me, goal) < 10.0f * 10.0f) {
            g_huntPhase[i] = 1; memcpy(goal, tp, 12);
            bot_log("hunt: %s is %s %s - goes in", (char *)H->bots[i] + OFF_PLAYER_ONLINE_NAME, kHuntRole[g_hunterRole[i]], q + OFF_PLAYER_ONLINE_NAME);
        }
    }
    int moved = dist2(goal, g_huntGoal[i]) > 8.0f * 8.0f || fabsf(goal[1] - g_huntGoal[i][1]) > 3.0f;
    if (!g_huntPending[i] && !moved) {
        if (nav->mode == 3) return 1;   // on its way, the human about where it was
        if (g_huntRoutedAt[i] && H->frame - g_huntRoutedAt[i] < 4 * 30) {   // the route ended short of it (a roof, a ledge): look round there
            nav->mode = 1;
            if (H->frame >= g_huntScanAt[i]) { g_huntScanAt[i] = H->frame + 20; nav->targetYaw = *(float *)(soldier + OFF_SOLDIER_YAW) + 1.3f; }
            return 1;
        }
    }
    if (!plan_budget_left()) { g_huntPlanAt[i] = H->frame + 1; return 1; }
    memcpy(g_huntGoal[i], goal, 12);
    if (plan_path(i, soldier, goal[0], goal[1], goal[2])) {
        nav->mode = 3; g_huntPending[i] = 0; g_huntFails[i] = 0; g_huntPlans++; g_huntRoutedAt[i] = H->frame ? H->frame : 1;
        g_huntPlanAt[i] = H->frame + 2 * 30;   // the next look at the route: 2 s (a human that moved 8 m gets a new one)
    } else if (g_lastPlanDeferred) {
        g_huntPending[i] = 1; g_huntPlanAt[i] = H->frame + 3;   // queued: the old route (if any) is followed meanwhile
    } else {
        g_huntPending[i] = 0;
        if (!g_huntPhase[i]) { g_huntPhase[i] = 1; g_huntPlanAt[i] = H->frame + 1; }   // no way to the flank point: straight in
        else if (++g_huntFails[i] >= 4) { hunt_release(i, "no route to its quarry"); g_huntCooldown[i] = H->frame + 30 * 30; return 0; }
        else g_huntPlanAt[i] = H->frame + 45;
    }
    return 1;
}

static void hunt_status(void)
{
    int n = 0;
    for (int i = 0; i < H->botCount; i++) if (hunt_quarry(i)) n++;
    bot_log("hunt: %s - %d bot(s) per human player (at most 60%% of a team), rough fix every %.0f s (%.0f m off; 0 = sight, gunfire and spots only), "
            "not from further than %.0f m; gunfire heard all round within %.0f m; %d hunting now - %u sent, %u let go, %u routes, %u exact / %u rough fixes",
            g_enhanced.hunt ? "on" : "off", g_enhanced.huntersPerPlayer, g_enhanced.huntIntelSeconds, g_enhanced.huntIntelMeters, g_enhanced.huntMaxMeters,
            g_enhanced.hearMeters, n, g_huntAssigned, g_huntReleased, g_huntPlans, g_huntExact, g_huntRough);
}
// Chat (!hunt) and console (hunt): status; admins switch it - on / off / a number of hunters per human. Until a restart
// or "!botconfig reload": Hunt= and HuntersPerPlayer= in enhanced.ini keep it.
static void hunt_command(const char *who, const char *arg)
{
    while (arg && *arg == ' ') arg++;
    if (arg && *arg) {
        if (who && !chat_is_admin(who)) { chat_say("[AltBotMod] %s: only admins can do that", who); return; }
        if (!_stricmp(arg, "on")) { g_enhanced.hunt = 1; if (g_enhanced.huntersPerPlayer <= 0) g_enhanced.huntersPerPlayer = 8; }
        else if (!_stricmp(arg, "off")) g_enhanced.hunt = 0;
        else if (arg[0] >= '0' && arg[0] <= '9') { int n = atoi(arg); g_enhanced.huntersPerPlayer = n < 0 ? 0 : n > 32 ? 32 : n; g_enhanced.hunt = n > 0; }
        else { if (who) chat_say("[AltBotMod] !hunt, !hunt on, !hunt off, !hunt <bots per player 0-32>"); return; }
    }
    hunt_status();
    int n = 0;
    for (int i = 0; i < H->botCount; i++) if (hunt_quarry(i)) n++;
    if (who) chat_say("[AltBotMod] hunt %s: %d bot(s) go after each human player; %d hunting now", g_enhanced.hunt ? "on" : "off", g_enhanced.huntersPerPlayer, n);
}
