#include "enhanced.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static int equal_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    }
    return !*a && !*b;
}
BMStuckHook bm_stuck_hook;   // AltBotMod V13 (see enhanced.h)
BMLearnHook bm_learn_hook; BMLearnAsk bm_learn_ask; BMLearnRoutes bm_learn_routes; BMLearnDanger bm_learn_danger; BMLearnPad bm_learn_pad;   // AltBotMod V21
void bm_defaults(BMConfig *c)
{
    memset(c, 0, sizeof *c);
    c->driving = c->light = c->boats = c->ballisticAim = 1;
    c->returnToBase = 1; c->groundDriversPerTeam = 16; c->returnMeters = 3000;
    c->passengerTakeover = 1;
    c->minTrip = 250; c->findRadius = 80; c->squadRadius = 90; c->stopShort = 55;
    c->humanRadius = 5;
    c->lightMaxSpeed = 34; c->boatMaxSpeed = 22;
    c->tankMaxSpeed = 16; c->ifvMaxSpeed = 20;
    c->stuckSeconds = 3; c->bailHealth = 0.1f;
    c->steerKp = 1.5f; c->steerKd = 0.08f;
    // AltBotMod V39 human style: fights out to 70 m, snipers 150 m, vehicle guns 375 m
    // (V37-V38: 300 / 450 m; V25: 800 / 400 m for armour)
    c->infantryRange = 70; c->reconRange = 150; c->reactionSeconds = 0.25f;
    c->infantryAimMargin = 0.20f; c->armorRange = 375; c->armorInfantryRange = 150;
    c->weaponVariety = c->weaponAttachments = 1;
    c->gadgetVariety = c->c4Attacks = c->lockOnLaunchers = 1;   // AltBotMod V16
    c->botTicketCost = 0.5f; c->vehicleCountermeasures = 1;
    c->weaponHandling = c->armorTactics = c->armorEscort = c->traffic = c->diagnostics = 1;
    c->driveStages=1;
    c->aggression = 1; c->savedRoutes = 1; c->botDamage = 1.0f; c->vehicleDamageTaken = c->vehicleDamageDealt = 1.0f;
    c->armorRepairHealth = 0.35f; c->armorTacticMeters = 70; c->trafficSpacing = 14;
    c->difficulty = 2; c->randomDifficulty = 0;   // AltBotMod V9: elite; V39: normal (with HumanStyle on every level plays the human profile anyway)
    c->sniperRange = 150; c->sniperOverwatch = 1;   // AltBotMod V10 (V39: 150 m, the human style's sniper reach; was 550)
    c->sniperPairs = 1; c->supplyMeters = 60; c->classSupport = 1;   // AltBotMod V11
    c->botNames = 1;   // AltBotMod V28: gamertags by default (BotNames=0: the "[BOT] Support RU" names)
    c->lockAssist = 1;   // AltBotMod V29: lock-ons lock (LockOnAssist=0: only the game's own lock, which never locks for a bot)
    c->lockOnSeconds = 0.5f;   // AltBotMod V30 (V29: the weapon's own lock time, 1.5-2.5 s)
    c->doorGunRange = 600.0f;   // AltBotMod V30
    c->objectiveMesh = 1; c->vehicleWalkMeters = 250.0f;   // AltBotMod V34
    c->liveMap = 1; c->liveMapSeconds = 1; c->liveMapTrailSeconds = 5; c->liveMapKeepMB = 64;   // AltBotMod V13
    c->learning = c->learnFromPlayers = 1;   // AltBotMod V21
    c->sideSteps = 3;   // AltBotMod V14 (V39 default: step and shoot - 1 s sideways, 1.5 s still)
    // AltBotMod V39 human style: bots that aim, react and move like people (the aim model: bm_haim_* below)
    c->humanStyle = 1; c->humanAimError = 6.0f; c->humanSniperAimError = 3.0f; c->humanVehicleAimError = 3.0f; c->humanAirAimError = 4.0f;
    c->humanAimSpread = 0.5f; c->humanRecoilBest = 0.9f; c->humanRecoilWorst = 0.5f;
    c->humanReaction = 0.25f; c->humanFirstShot = 0.2f; c->humanFov = 160.0f; c->humanShootBack = 150.0f; c->humanSniperShootBack = 400.0f;
    c->humanStrafe = 0.6f;
    c->routeActions = 1; c->reconBeacons = 1;   // AltBotMod V15
    c->hunt = 1; c->huntersPerPlayer = 8; c->huntIntelSeconds = 6; c->huntIntelMeters = 12; c->huntMaxMeters = 700;   // AltBotMod Hunt
    c->hearMeters = 120;
    c->snapAim = 1; c->snapAimDegrees = 720;   // AltBotMod Hunt v2
    bm_air_defaults(&c->air);
}
int bm_set(BMConfig *c, const char *key, const char *value)
{
    static const struct { const char *name; size_t offset; int boolean; float lo, hi; } keys[] = {
#define BOOL_KEY(name, field) {name, offsetof(BMConfig, field), 1, 0, 1}
#define FLOAT_KEY(name, field, lo, hi) {name, offsetof(BMConfig, field), 0, lo, hi}
#define INT_KEY(name, field, lo, hi) {name, offsetof(BMConfig, field), 2, lo, hi}
#define SIGN_KEY(name, field) {name, offsetof(BMConfig, field), 3, -1, 1}
        BOOL_KEY("Driving", driving), BOOL_KEY("LightVehicles", light), BOOL_KEY("Boats", boats),
        BOOL_KEY("ReturnToBase", returnToBase), INT_KEY("GroundDriversPerTeam", groundDriversPerTeam, 1, 32),
        BOOL_KEY("PassengerTakeover", passengerTakeover),
        FLOAT_KEY("VehicleReturnMeters", returnMeters, 100, 6000),
        BOOL_KEY("ArmoredVehicles", armored), BOOL_KEY("DriverWeapons", driverWeapons),
        BOOL_KEY("SmoothSteering", smoothSteering), BOOL_KEY("BallisticAim", ballisticAim),
        FLOAT_KEY("MinimumTripMeters", minTrip, 25, 2000),
        FLOAT_KEY("FindVehicleMeters", findRadius, 5, 250),
        FLOAT_KEY("SquadVehicleMeters", squadRadius, 5, 250),
        FLOAT_KEY("DismountMeters", stopShort, 20, 150),
        FLOAT_KEY("ReserveHumanMeters", humanRadius, 0, 100),
        FLOAT_KEY("LightSpeedMps", lightMaxSpeed, 3, 45),
        FLOAT_KEY("BoatSpeedMps", boatMaxSpeed, 3, 35),
        FLOAT_KEY("TankSpeedMps", tankMaxSpeed, 2, 20),
        FLOAT_KEY("IfvSpeedMps", ifvMaxSpeed, 2, 25),
        FLOAT_KEY("StuckSeconds", stuckSeconds, 1, 15),
        FLOAT_KEY("BailHealthFraction", bailHealth, 0.05f, 0.5f),
        FLOAT_KEY("SteerKp", steerKp, 0.2f, 4),
        FLOAT_KEY("SteerKd", steerKd, 0, 0.5f),
        FLOAT_KEY("InfantryEngageMeters", infantryRange, 40, 300),
        FLOAT_KEY("ReconEngageMeters", reconRange, 80, 500),
        FLOAT_KEY("SniperEngageMeters", sniperRange, 150, 900), BOOL_KEY("SniperOverwatch", sniperOverwatch),
        BOOL_KEY("SniperPairs", sniperPairs), FLOAT_KEY("SupplyMeters", supplyMeters, 20, 250), BOOL_KEY("ClassSupport", classSupport),
        BOOL_KEY("BotNames", botNames),   // AltBotMod V28: gamertags instead of "[BOT] Support RU"
        BOOL_KEY("LockOnAssist", lockAssist),   // AltBotMod V29
        FLOAT_KEY("LockOnSeconds", lockOnSeconds, 0, 6),   // AltBotMod V30
        FLOAT_KEY("DoorGunMeters", doorGunRange, 150, 1500),   // AltBotMod V30
        BOOL_KEY("ObjectiveMesh", objectiveMesh), FLOAT_KEY("VehicleWalkMeters", vehicleWalkMeters, 25, 6000),   // AltBotMod V34
        BOOL_KEY("LiveMap", liveMap), BOOL_KEY("Learning", learning), BOOL_KEY("LearnFromPlayers", learnFromPlayers), FLOAT_KEY("LiveMapSeconds", liveMapSeconds, 0.5f, 10),
        FLOAT_KEY("LiveMapTrailSeconds", liveMapTrailSeconds, 1, 60), INT_KEY("LiveMapKeepMB", liveMapKeepMB, 5, 1000),
        INT_KEY("SideSteps", sideSteps, 0, 3),
        // AltBotMod V39: human style - a human-like aim error, reaction, view and strafe pattern
        BOOL_KEY("HumanStyle", humanStyle), FLOAT_KEY("HumanAimErrorMrad", humanAimError, 0, 40), FLOAT_KEY("HumanSniperAimErrorMrad", humanSniperAimError, 0, 40),
        FLOAT_KEY("HumanVehicleAimErrorMrad", humanVehicleAimError, 0, 40), FLOAT_KEY("HumanAirAimErrorMrad", humanAirAimError, 0, 40),
        FLOAT_KEY("HumanAimSkillSpread", humanAimSpread, 0, 1), FLOAT_KEY("HumanRecoilControlBest", humanRecoilBest, 0, 1), FLOAT_KEY("HumanRecoilControlWorst", humanRecoilWorst, 0, 1),
        FLOAT_KEY("HumanReactionSeconds", humanReaction, 0, 2), FLOAT_KEY("HumanFirstShotSeconds", humanFirstShot, 0, 2),
        FLOAT_KEY("HumanFovDegrees", humanFov, 40, 360), FLOAT_KEY("HumanShootBackMeters", humanShootBack, 20, 600), FLOAT_KEY("HumanSniperShootBackMeters", humanSniperShootBack, 50, 900),
        FLOAT_KEY("HumanStrafe", humanStrafe, 0, 1),
        BOOL_KEY("Hunt", hunt), INT_KEY("HuntersPerPlayer", huntersPerPlayer, 0, 32),   // AltBotMod Hunt
        FLOAT_KEY("HuntIntelSeconds", huntIntelSeconds, 0, 60), FLOAT_KEY("HuntIntelMeters", huntIntelMeters, 0, 100),
        FLOAT_KEY("HuntMaxMeters", huntMaxMeters, 50, 3000), FLOAT_KEY("HearMeters", hearMeters, 0, 400),
        INT_KEY("SnapAim", snapAim, 0, 2), FLOAT_KEY("SnapAimDegreesPerSecond", snapAimDegrees, 90, 3600),   // AltBotMod Hunt v2
        BOOL_KEY("RouteActions", routeActions), BOOL_KEY("ReconBeacons", reconBeacons),   // AltBotMod V15
        FLOAT_KEY("InfantryReactionSeconds", reactionSeconds, 0.1f, 2),
        FLOAT_KEY("InfantryAimMarginMeters", infantryAimMargin, 0.08f, 0.5f),
        FLOAT_KEY("ArmorEngageMeters", armorRange, 150, 1000),
        FLOAT_KEY("ArmorInfantryMeters", armorInfantryRange, 80, 450),
        BOOL_KEY("WeaponVariety", weaponVariety), BOOL_KEY("WeaponAttachments", weaponAttachments),
        BOOL_KEY("GadgetVariety", gadgetVariety), BOOL_KEY("C4Attacks", c4Attacks), BOOL_KEY("LockOnLaunchers", lockOnLaunchers),
        FLOAT_KEY("BotTicketCost", botTicketCost, 0, 2), BOOL_KEY("VehicleCountermeasures", vehicleCountermeasures),
        BOOL_KEY("WeaponHandling", weaponHandling), BOOL_KEY("ArmorTactics", armorTactics),
        BOOL_KEY("DriveInStages",driveStages),
        INT_KEY("Aggression", aggression, 0, 2), BOOL_KEY("SavedRoutes", savedRoutes),
        FLOAT_KEY("BotDamage", botDamage, 0.25f, 4.0f),
        FLOAT_KEY("VehicleDamageTaken", vehicleDamageTaken, 0.25f, 4.0f), FLOAT_KEY("VehicleDamageDealt", vehicleDamageDealt, 0.25f, 4.0f),
        BOOL_KEY("ArmorInfantrySupport", armorEscort), BOOL_KEY("TrafficManagement", traffic),
        BOOL_KEY("VehicleDiagnostics", diagnostics),
        FLOAT_KEY("ArmorRepairHealthFraction", armorRepairHealth, 0.15f, 0.65f),
        FLOAT_KEY("ArmorTacticMeters", armorTacticMeters, 25, 120),
        FLOAT_KEY("ConvoySpacingMeters", trafficSpacing, 8, 35),
        BOOL_KEY("Jets", air.jets), BOOL_KEY("Helicopters", air.helicopters),
        BOOL_KEY("AircraftWeapons", air.weapons), BOOL_KEY("TransportDrops", air.transportDrops),
        BOOL_KEY("AircraftCountermeasures", air.countermeasures),
        BOOL_KEY("TransportLandings", air.landings), BOOL_KEY("LearnAircraftDeployment", air.deploymentMenu),
        BOOL_KEY("AircraftDebug", air.debug),
        INT_KEY("AirPilotsPerTeam", air.pilotsPerTeam, 1, 6),
        FLOAT_KEY("AircraftFindMeters", air.findRadius, 10, 250),
        FLOAT_KEY("JetCruiseMps", air.jetSpeed, 60, 130),
        FLOAT_KEY("JetHeightMeters", air.jetHeight, 150, 600),
        FLOAT_KEY("JetCeilingMeters", air.jetCeiling, 200, 1200),
        FLOAT_KEY("ChopperHeightMeters", air.chopperHeight, 40, 250),
        FLOAT_KEY("JetOrbitMeters", air.jetOrbit, 150, 600),
        FLOAT_KEY("JetAreaMarginMeters", air.jetAreaMargin, 0, 3000),   // AltBotMod V29
        FLOAT_KEY("ChopperOrbitMeters", air.chopperOrbit, 30, 150),
        FLOAT_KEY("AircraftWaitSeconds", air.waitSeconds, 0.5f, 25),
        FLOAT_KEY("AircraftAttackSeconds", air.attackSeconds, 2, 15),
        SIGN_KEY("AirYawSign", air.yawSign), SIGN_KEY("AirPitchSign", air.pitchSign),
        SIGN_KEY("AirRollSign", air.rollSign)
#undef BOOL_KEY
#undef FLOAT_KEY
#undef INT_KEY
#undef SIGN_KEY
    };
    if (equal_ci(key, "Difficulty")) {   // AltBotMod V9: a level name or number
        int level = bm_difficulty_parse(value);
        if (level < 0) return -1;
        c->difficulty = level;
        return 1;
    }
    if (equal_ci(key, "RandomDifficulty")) {
        if (!strcmp(value, "0") || !strcmp(value, "1")) { c->randomDifficulty = value[0] == '1'; return 1; }
        return -1;
    }
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        if (!equal_ci(key, keys[k].name)) continue;
        char *end;
        errno = 0;
        float v = strtof(value, &end);
        if (end == value || errno || !isfinite(v)) return -1;
        while (isspace((unsigned char)*end)) end++;
        int type = keys[k].boolean;
        if (*end || v < keys[k].lo || v > keys[k].hi ||
            (type == 1 && v != 0 && v != 1) || (type == 2 && v != floorf(v)) ||
            (type == 3 && v != -1 && v != 1)) return -1;
        if (type == 1 || type == 2) *(int *)((char *)c + keys[k].offset) = (int)v;
        else *(float *)((char *)c + keys[k].offset) = v;
        return 1;
    }
    return 0;
}
// Projectile constants are for the default primary weapon, never a guessed
// secondary weapon. Ground speed/clearance limits are conservative test values.
static const BMVehicleProfile unknown = {"unclassified", BM_UNKNOWN, 600, 0, 1.2f, 2.5f};
static const BMVehicleProfile light = {"light transport", BM_LIGHT, 600, 9.81f, 1.2f, 2.5f};
static const BMVehicleProfile boat = {"RHIB", BM_BOAT, 600, 9.81f, 1.2f, 5};
static const BMVehicleProfile tank = {"MBT", BM_TANK, 200, 9.81f, 1.8f, 4.5f};
static const BMVehicleProfile tankDestroyer = {"tank destroyer", BM_TANK, 200, 4.9f, 1.7f, 4};
static const BMVehicleProfile ifv = {"IFV", BM_IFV, 200, 4.9f, 1.7f, 4};
static const BMVehicleProfile air = {"aircraft (separate flight controller)", BM_AIR, 600, 0, 0, 0};
static const BMVehicleProfile aa = {"mobile AA cannon", BM_AA, 900, 0, 1.7f, 4};
static const BMVehicleProfile unsupportedAA = {"missile AA (unbound)", BM_UNKNOWN, 0, 0, 1.7f, 4};
static const BMVehicleProfile amtrac = {"AMTRAC (unarmed driver; gunner cannon)", BM_AMTRAC, 80, 7, 1.8f, 4.5f};

const BMVehicleProfile *bm_vehicle_profile(const char *name)
{
    char n[160]; size_t j = 0;
    if (!name) return &unknown;
    for (size_t i = 0; name[i] && j + 1 < sizeof n; i++)
        if (isalnum((unsigned char)name[i])) n[j++] = (char)toupper((unsigned char)name[i]);
    n[j] = 0;
    if (bm_air_profile(name)) return &air;
    static const char *const aircraft[] = {"AH1", "AH6", "UH1", "UH60", "MI28", "KA60", "Z11", "F16", "F18", "FA18", "F35", "SU25", "SU35", "SU37", "A10", "VENOM", "GUNSHIP", "AC130", "C130", NULL};
    static const char *const lights[] = {"GROWLER", "VDVBUGGY", "BUGGY", "DPV", "HMMWV", "HUMVEE", "VODNIK", "GAZ", "QUAD", "DIRTBIKE", "KLR650", NULL};
    for (int k = 0; aircraft[k]; k++) if (strstr(n, aircraft[k])) return &air;
    if (strstr(n, "ASRAD") || strstr(n, "VODNIKPHOENIX")) return &unsupportedAA;
    if (strstr(n, "TUNGUSKA") || strstr(n, "9K22") || strstr(n, "LAVAD")) return &aa;
    if (strstr(n,"AAV7A1") || strstr(n,"AMTRAC"))return &amtrac;
    if (strstr(n, "RHIB")) return &boat;
    if (strstr(n, "SPRUT") || strstr(n, "M1128") || strstr(n, "STRYKER")) return &tankDestroyer;
    if (strstr(n, "M1ABRAMS") || strstr(n, "M1A2") || strstr(n, "T90")) return &tank;
    if (strstr(n, "LAV25") || strstr(n, "BTR90") || strstr(n, "BMP2")) return &ifv;
    for (int k = 0; lights[k]; k++) if (strstr(n, lights[k])) return &light;
    return &unknown;
}
int bm_armored(const BMVehicleProfile *p) { return p && (p->kind == BM_TANK || p->kind == BM_IFV || p->kind==BM_AA || p->kind==BM_AMTRAC); }
int bm_hold_vehicle(const char *name,int passengers)
{
    const BMVehicleProfile *p=bm_vehicle_profile(name);
    if(bm_armored(p))return 1;
    if(p->kind!=BM_LIGHT || passengers<1 || !name)return 0;
    char n[256];size_t j=0;
    for(size_t i=0;name[i] && j+1<sizeof n;i++)if(isalnum((unsigned char)name[i]))n[j++]=(char)toupper((unsigned char)name[i]);
    n[j]=0;
    return !strstr(n,"QUAD") && !strstr(n,"ATV") && !strstr(n,"DIRTBIKE") && !strstr(n,"KLR650") && !strstr(n,"MOTORCYCLE");
}
int bm_vehicle_allowed(const BMConfig *c, const BMVehicleProfile *p)
{
    if (!c->driving || !p) return 0;
    return (p->kind == BM_LIGHT && c->light) || (p->kind == BM_BOAT && c->boats) || (bm_armored(p) && c->armored);
}
int bm_ranges_v25(BMConfig *c)
{
    int moved = 0;
    if (c->infantryRange == 180.0f) { c->infantryRange = 260.0f; moved = 1; }
    if (c->reconRange == 320.0f) { c->reconRange = 450.0f; moved = 1; }
    if (c->armorRange == 650.0f) { c->armorRange = 800.0f; moved = 1; }
    if (c->armorInfantryRange == 300.0f) { c->armorInfantryRange = 400.0f; moved = 1; }
    return moved;
}
float bm_speed_limit(const BMConfig *c, const BMVehicleProfile *p)
{
    if (!p) return c->lightMaxSpeed;
    switch (p->kind) {
        case BM_BOAT: return c->boatMaxSpeed;
        case BM_TANK: return c->tankMaxSpeed;
        case BM_IFV: case BM_AA: case BM_AMTRAC: return c->ifvMaxSpeed;
        default: return c->lightMaxSpeed;
    }
}
float bm_steer(BMSteering *s, float error, float dt, const BMConfig *c)
{
    if (!isfinite(error) || !isfinite(dt) || dt <= 0) { memset(s, 0, sizeof *s); return 0; }
    if (!c->smoothSteering) return clampf(-error * c->steerKp, -1, 1);
    float delta = error - s->previousError;
    // Crossing -pi/pi must not create a derivative impulse.
    while (delta > 3.14159265f) delta -= 6.2831853f;
    while (delta < -3.14159265f) delta += 6.2831853f;
    float derivative = s->ready ? clampf(delta / dt, -5, 5) : 0;
    s->filteredDerivative = 0.8f * s->filteredDerivative + 0.2f * derivative;
    s->previousError = error; s->ready = 1;
    return clampf(-c->steerKp * error - c->steerKd * s->filteredDerivative, -1, 1);
}
int bm_stuck_due(unsigned now, unsigned checkAt, int reversing, int backingOut, float seconds)
{
    return !reversing && !backingOut && now - checkAt >= (unsigned)(seconds * 30.0f);
}
void bm_ballistics(float speed, float gravity, float distance, float *time, float *rise)
{
    if (!isfinite(speed) || speed <= 0) speed = 600;
    if (!isfinite(distance) || distance < 0) distance = 0;
    if (!isfinite(gravity) || gravity < 0) gravity = 0;
    *time = clampf(distance / speed, 0, 2.5f);
    *rise = 0.5f * gravity * *time * *time;
}
void bm_forget_slot(void *array, size_t size, int index, int count)
{
    if (!array || !size || count <= 0 || index < 0 || index >= count) return;
    char *p = array;
    if (index + 1 < count) memmove(p + size * (size_t)index, p + size * (size_t)(index + 1), size * (size_t)(count - index - 1));
    memset(p + size * (size_t)(count - 1), 0, size);
}
float bm_target_score(int armored, enum BMTargetKind kind, float distance)
{
    if (!isfinite(distance) || distance<0) return INFINITY;
    if (!armored) return (kind==BM_TARGET_ENGINEER ? 0 : kind==BM_TARGET_GROUND || kind==BM_TARGET_AIR ? 1000 : 2000)+distance;
    // Caspian V5: tanks shoot helicopters too (a main-gun hit kills one); after close armour/engineers, before far infantry
    // (AltBotMod V7: aircraft after enemy armour and nearby infantry, so a tank does not spend the push on a helicopter)
    // (AltBotMod V8: soldiers out to 300 m too - only a helicopter within ~150 m beats them. On a new round's HQ the V7
    // tanks sat turning their guns after helicopters for minutes.)
    // AltBotMod V17: aircraft are the AA launchers' and AA vehicles' job. A tank / IFV gun takes a helicopter only when
    // it is close and low (within 100 m - it is attacking), after every soldier within 300 m and every enemy vehicle
    // within 400 m out to 200 m, and a far one (to 300 m) only with nothing else in sight (V8-V16: any aircraft within
    // 500 m beat soldiers past ~150 m, and tank gunners spent their time after helicopters).
    if (kind==BM_TARGET_AIR) return distance<=100 ? 450+distance*0.5f : distance<=200 ? 900+distance : distance<=300 ? 5000+distance : INFINITY;
    if (kind==BM_TARGET_ENGINEER && distance<=120) return 70+distance;   // (V23: 100 -> 70 - an RPG close by still beats a rifleman close by)
    // AltBotMod V23: a soldier within 60 m beats an enemy vehicle beyond ~120-150 m (a near enemy tank still comes first;
    // within 25 m the gunner's own rule puts him first of all)
    if (kind==BM_TARGET_INFANTRY && distance<=60) return 232+distance*0.6f;
    // AltBotMod V6: enemy armour and vehicles out to 400 m before infantry (V5: a tank 130 m away scored 1630 against
    // 440 for a soldier at 90 m, so a bot tank kept shooting soldiers while a player's tank drove up to it).
    if (kind==BM_TARGET_GROUND && distance<=400) return 150+distance*0.6f;
    if (kind==BM_TARGET_INFANTRY && distance<=100) return 350+distance;
    if (kind==BM_TARGET_INFANTRY && distance<=300) return 400+distance;
    return (kind==BM_TARGET_GROUND ? 1500 : kind==BM_TARGET_ENGINEER ? 1000 : 2000)+distance;
}

float bm_gun_range(const BMConfig *c, const BMVehicleProfile *p, int infantry)
{
    return bm_armored(p) ? (infantry ? c->armorInfantryRange : c->armorRange) : (infantry ? 220.0f : 375.0f);
}

// Low-angle ballistic solution, including an uphill/downhill target. The old
// 2.5-second drop approximation is insufficient once a tank fires past 500 m.
int bm_ground_ballistics(float speed, float gravity, float distance, float height, float *flight, float *lift)
{
    if (!flight || !lift || !isfinite(speed) || speed <= 0 || !isfinite(gravity) || gravity < 0 ||
        !isfinite(distance) || distance < 0 || !isfinite(height)) return 0;
    *flight = *lift = 0;
    if (distance < 0.01f) return 0;
    if (gravity < 0.001f) { *flight = hypotf(distance,height)/speed; return *flight <= 6; }
    double s2 = (double)speed*speed;
    double disc = s2*s2 - gravity*((double)gravity*distance*distance + 2.0*height*s2);
    if (disc < 0 || !isfinite(disc)) return 0;
    double tangent = ((double)gravity*distance*distance + 2.0*height*s2) /
                     ((double)distance*(s2 + sqrt(disc)));
    double time = distance*sqrt(1.0+tangent*tangent)/speed;
    double rise = distance*tangent-height;
    if (!isfinite(time) || !isfinite(rise) || time > 6 || rise < -0.001) return 0;
    *flight = (float)time; *lift = (float)fmax(0,rise); return 1;
}

int bm_infantry_burst(int recon, int sidearm, float d)
{
    return recon || sidearm ? 3 : d > 110 ? 6 : d > 55 ? 10 : 18;
}
int bm_infantry_pause(int recon, float d) { return recon ? 14 : d > 110 ? 6 : 4; }
float bm_infantry_tolerance(const BMConfig *c, int recon, float d)
{
    float margin = (recon ? fminf(c->infantryAimMargin,0.12f) : c->infantryAimMargin);   // Caspian V3: no wider aim window (V2's x1.8 fired while off target)
    return clampf(atanf(margin/fmaxf(1,d)),0.0025f,(recon ? 0.025f : 0.045f));
}
float bm_drive_horizon(float speed, int boat)
{
    speed = isfinite(speed) ? clampf(speed,0,50) : 0;
    return clampf(speed*speed/(boat ? 6.0f : 8.0f) + speed*0.15f + 5,12,90);
}
float bm_drive_obstacle_speed(float distance, float speed, int boat)
{
    if (!isfinite(distance) || !isfinite(speed)) return 0;
    float room = fmaxf(0,distance-2-fmaxf(0,speed)*0.15f);
    return sqrtf((boat ? 6.0f : 8.0f)*room);
}
float bm_drive_throttle(float vwant, float speed, int armored, float headingError)
{
    if (!isfinite(vwant) || !isfinite(speed)) return 0.0f;
    if (vwant <= 0.0f) return speed > 0.5f ? -0.6f : 0.0f;
    if (speed < vwant - 1.0f) return (!armored && fabsf(headingError) > 1.0f) ? 0.6f : 1.0f;   //
    if (speed < vwant + 1.0f) return 0.3f;                                                      // at the target: hold
    if (speed < vwant + 3.0f) return 0.0f;                                                      // a little over: off the throttle
    return -fmaxf(0.15f, fminf(0.8f, (speed - vwant - 1.0f) / 6.0f));                          // well over: brake, harder the further over
}
float bm_lane_limit(float along, float otherSpeedAlong, float frontLead)
{
    if (!isfinite(along) || !isfinite(otherSpeedAlong)) return 99.0f;
    float gap = along - frontLead - 6.0f;   // stops 6 m short of it - room to pull out round it
    if (otherSpeedAlong < 0.0f && gap < 15.0f) return 0.0f;   // head on and close: stop (then both pass on their right)
    float vo = fmaxf(0.0f, otherSpeedAlong), v = sqrtf(vo * vo + 7.0f * fmaxf(0.0f, gap));   // closes on it at 3.5 m/s^2 at most
    if (gap < 1.0f) v = fminf(v, fmaxf(0.0f, vo - 1.0f));
    return v;
}
int bm_lane_relation(const float *pts, int n, int next, const float *vp, const float fwd[2], const float *op, float half, float range,
                     float *along, float *lat)
{
    if (!vp || !op || !fwd || !isfinite(op[0]) || !isfinite(op[2])) return 0;
    float fl = hypotf(fwd[0], fwd[1]); if (fl < 1e-3f) return 0;
    float fx = fwd[0] / fl, fz = fwd[1] / fl, ex = op[0] - vp[0], ez = op[2] - vp[2];
    float a = ex * fx + ez * fz, l = ex * fz - ez * fx;   // ahead; right of the heading
    if (a > 0.5f && a <= 15.0f && fabsf(l) <= half) { *along = a; *lat = l; return 1; }
    if (!pts || n < 1 || next < 0 || next >= n) return 0;
    float px = vp[0], pz = vp[2], acc = 0;
    for (int k = next; k < n && acc <= range; k++) {
        float qx = pts[k * 3], qz = pts[k * 3 + 2], sx = qx - px, sz = qz - pz, sl = hypotf(sx, sz);
        if (sl > 0.05f) {
            float t = ((op[0] - px) * sx + (op[2] - pz) * sz) / (sl * sl); t = t < 0 ? 0 : t > 1 ? 1 : t;
            float cx = px + sx * t - op[0], cz = pz + sz * t - op[2], dd = hypotf(cx, cz), at = acc + t * sl;
            if (dd <= half && at > 15.0f && at <= range) {
                *along = at; *lat = ((op[0] - px) * sz - (op[2] - pz) * sx) / sl;   // (right of the road's direction)
                return 1;
            }
        }
        acc += sl; px = qx; pz = qz;
    }
    return 0;
}
float bm_bend_speed(const float pb[3], const float w[3], const float pf[3], int boat, int armored)
{
    float a1 = atan2f(w[0] - pb[0], w[2] - pb[2]), a2 = atan2f(pf[0] - w[0], pf[2] - w[2]), turn = a2 - a1;
    while (turn > 3.14159265f) turn -= 6.2831853f;
    while (turn < -3.14159265f) turn += 6.2831853f;
    float at = fabsf(turn), span = 0.5f * (hypotf(w[0] - pb[0], w[2] - pb[2]) + hypotf(pf[0] - w[0], pf[2] - w[2]));   // (chord to chord = the arc between their middles)
    if (!isfinite(at) || !isfinite(span)) return boat ? 6.0f : 4.0f;
    float radius = at > 0.02f ? fmaxf(2.0f, span / at) : 1e4f, grip = boat ? 5.0f : armored ? 4.0f : 6.5f;
    float vt = fmaxf(boat ? 6.0f : 4.0f, sqrtf(grip * radius));
    if (at > 1.4f) vt = fminf(vt, boat ? 8.0f : 5.0f);   // a hairpin / a junction turn: slow whatever the spacing
    return vt;
}
static float d2h(const float *a, const float *b) { float x = a[0] - b[0], z = a[2] - b[2]; return x * x + z * z; }
float bm_route_bend_limit(const float pb[3], const float w[3], const float pf[3], int boat, int armored)
{
    // AltBotMod V24: as the reference drives - flat out along the road; only a real bend (radius under 28 m: a junction, a
    // hairpin, a tight corner) is slowed for. Gentle curves and a recording's wobble are the vehicle's physics' business.
    // Both arms are the road itself (+-25 m of recorded points, never the car's own position): an arm under 12 m cannot
    // tell a bend from a wobble, so it is not slowed for.
    if (!pb || !pf || d2h(pb, w) < 12.0f * 12.0f || d2h(pf, w) < 12.0f * 12.0f) return 99.0f;
    float vt = bm_bend_speed(pb, w, pf, boat, armored), grip = boat ? 5.0f : armored ? 4.0f : 6.5f;
    return vt * vt / grip > 28.0f ? 99.0f : vt;
}
int bm_route_progress(const float *pts, int n, int next, const float pos[3], const float *fwd, float reach)
{
    if (!pts || n <= 0 || next >= n) return next < 0 ? 0 : next;
    if (next < 0) next = 0;
    int best = next; float bd = d2h(&pts[best * 3], pos), along = 0;
    for (int k = next + 1; k < n; k++) {
        along += sqrtf(d2h(&pts[k * 3], &pts[(k - 1) * 3]));
        if (along > 30.0f) break;
        float dd = d2h(&pts[k * 3], pos);
        if (dd < bd && fabsf(pts[k * 3 + 1] - pos[1]) < 6.0f) { bd = dd; best = k; }
    }
    next = best;
    while (next < n) {
        const float *w = &pts[next * 3]; float dx = w[0] - pos[0], dz = w[2] - pos[2], dd = dx * dx + dz * dz;
        int behind = fwd && dd < 15.0f * 15.0f && dx * fwd[0] + dz * fwd[2] < 0;   // already driven past it
        if (dd < reach * reach || behind) next++;
        else break;
    }
    return next;
}
void bm_route_aim(const float *pts, int n, int next, const float pos[3], float speed, float out[3])
{
    if (!pts || n <= 0) { memcpy(out, pos, 12); return; }
    if (next >= n) next = n - 1;
    if (next < 0) next = 0;
    const float *wp = &pts[next * 3];
    memcpy(out, wp, 12);
    // (V27: 15-30 m, was 8-25 m: a close aim point at low speed put a slow-turning tank 60-90 deg off it, its speed capped,
    // the aim stayed close - it crawled and turned round on the spot)
    float L = fminf(30.0f, fmaxf(15.0f, 10.0f + 0.6f * (isfinite(speed) ? speed : 0))), acc = sqrtf(d2h(wp, pos));
    // AltBotMod V28: stay on the road. Off the recorded line by more than 2 m (on the verge, among the fences), the aim
    // comes in closer - 8 m at 4 m off - so it steers back onto the road instead of running alongside it; and the aim stops
    // at a bend of 17 deg (was 34), so a corner is not cut across its inside verge.
    if (next > 0) {
        const float *a = &pts[(next - 1) * 3];
        float ax = wp[0] - a[0], az = wp[2] - a[2], l = ax * ax + az * az, t = l > 0 ? ((pos[0] - a[0]) * ax + (pos[2] - a[2]) * az) / l : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float off = hypotf(a[0] + ax * t - pos[0], a[2] + az * t - pos[2]);
        if (off > 2.0f) L = fmaxf(8.0f, L - (off - 2.0f) * 3.5f);
    }
    float first = atan2f(wp[0] - pos[0], wp[2] - pos[2]);
    for (int k = next; acc < L && k + 1 < n; k++) {
        const float *a = &pts[k * 3], *b = &pts[(k + 1) * 3]; float seg = sqrtf(d2h(a, b));
        if (seg < 0.01f) continue;
        float t = atan2f(b[0] - a[0], b[2] - a[2]) - first;
        while (t > 3.14159265f) t -= 6.2831853f;
        while (t < -3.14159265f) t += 6.2831853f;
        if (fabsf(t) > 0.3f) break;
        if (acc + seg >= L) { float f = (L - acc) / seg; for (int q = 0; q < 3; q++) out[q] = a[q] + (b[q] - a[q]) * f; return; }
        acc += seg; memcpy(out, b, 12);
    }
}
#include "tactics_policy.inc"

float bm_vehicle_target_score(const BMVehicleProfile *p,enum BMTargetKind kind,float distance)
{
    if(p && p->kind==BM_AA){
        if(!isfinite(distance) || distance<0)return INFINITY;
        // Close engineers take precedence; otherwise the AA cannon favors air.
        if(kind==BM_TARGET_ENGINEER && distance<40)return distance-500;
        if(kind==BM_TARGET_AIR)return distance;
        return (kind==BM_TARGET_GROUND?1300:900)+distance;
    }
    // AltBotMod V26: a helicopter's guns: an enemy aircraft within 900 m first (it is the one that kills a helicopter),
    // then armour and AA within 700 m, soldiers with launchers within 400 m, other soldiers within 500 m. V6-V25 ran a
    // helicopter gunner on the jeep table: engineers first, vehicles and aircraft level, so a gunner kept firing at a
    // soldier while an enemy helicopter shot it down.
    if(p && p->kind==BM_AIR){
        if(!isfinite(distance) || distance<0)return INFINITY;
        if(kind==BM_TARGET_AIR)return distance<=900?distance*0.5f:1200+distance;
        if(kind==BM_TARGET_GROUND)return distance<=700?250+distance*0.6f:1400+distance;
        if(kind==BM_TARGET_ENGINEER)return distance<=400?300+distance*0.8f:1500+distance;
        return distance<=500?500+distance:2000+distance;
    }
    // AltBotMod V17: a jeep / boat machine gun: aircraft only within 150 m and only when no soldier is in reach (V16 put
    // them level with ground vehicles, ahead of all soldiers)
    if(!bm_armored(p) && !(p && p->kind==BM_AIR) && kind==BM_TARGET_AIR)return isfinite(distance) && distance>=0 && distance<=150?2300+distance:INFINITY;
    return bm_target_score(bm_armored(p),kind,distance);
}

// ---- AltBotMod V9: difficulty profiles ----
// AltBotMod custom combat profiles: widened detection view to 175 degrees either side; infantry engagement and detection extended.
// Values exactly as in the dev's BotMod public preview (BotModCore.dll .rdata, 5 records of 32 dwords). Fields in the
// order of its difficulty.ini keys: ReactionSeconds BurstFrames PauseFrames AimToleranceRad AimMarginMeters
// AimLagSeconds AimTurnGain FightRangeMeters EngageRangeMeters DetectRangeMeters FieldOfViewDegrees
// EngageCooldownSeconds BreatherSeconds CoverBelowHealth AdvanceStopMeters CoverHoldSeconds SprintMeters
// FightSprintMeters FightStrafe JumpWhileRunningSeconds JumpWhileFightingSeconds FarCrouchMeters FarCrouchChance
// AimSettleFrames AdsRangeMeters GuardSeconds GuardSpreadSeconds AimWorseningMeters AimSkillSpread FireModel RangeFalloff
static const BMDifficulty kLevels[BM_LEVELS] = {
    // Reaction + settle is seven 30-Hz frames (~0.23 s) at every level: a fast human-like
    // response after detection, without the old 0.05 s elite snap reaction.
    // All profiles detect at 500 m and begin infantry engagements at 400 m. The
    // configured fight range also reaches 400 m; weapon-family max range / range
    // factor still limits actual shots (e.g. PDWs and shotguns remain close-range).
    // (Hunt v4 merge: Mr.Cat's V37 rows for easy..elite; the five Hunt fields 0 there; nightmare from AltBotMod Hunt)
    {0.2333f,  8, 16, 0.040f, 0.30f, 0.25f, 1.5f, 400, 400, 500,  175.00f, 15.0f, 4.00f, 55, 15, 6, 30,  0, 0,   0,   0,    0, 0,    0, 50, 30, 15, 0.9f, 0.6f, 1, 0,     0, 0, 0, 0, 0},
    {0.2333f, 12, 10, 0.050f, 0.35f, 0.15f, 2.0f, 400, 400, 500,  175.00f, 10.0f, 2.00f, 40, 15, 6, 20,  0, 0,   0,   0,    0, 0,    0, 50, 20, 10, 0,    0,    0, 0,     0, 0, 0, 0, 0},
    {0.0666f, 14,  6, 0.055f, 0.40f, 0.10f, 3.0f, 400, 400, 500, 175.00f,  5.0f, 1.00f, 30, 12, 4, 12, 35, 0.6f, 6.0f, 0,  35, 0.25f, 5, 35, 12,  8, 0,    0.3f, 1, 0.5f,  0, 0, 0, 0, 0},
    {0.1333f, 18,  4, 0.055f, 0.42f, 0.05f, 4.0f, 400, 400, 500, 175.00f,  2.0f, 0.40f, 20, 10, 3,  8, 30, 0.8f, 4.0f, 2.5f, 28, 0.45f, 3, 25,  8,  6, 0,    0.15f, 1, 0.9f, 0, 0, 0, 0, 0},
    {0.2000f, 26,  2, 0.050f, 0.45f, 0.015f, 6.0f, 400, 400, 500, 175.00f, 0.5f, 0.10f, 10,  8, 2,  6, 25, 1.0f, 3.0f, 1.8f, 22, 0.60f, 1, 18,  5,  4, 0,   0.03f, 1, 1.2f,  0, 0, 0, 0, 0},
    // AltBotMod Hunt: nightmare - elite's aim with no let-up. Engages and detects as elite (400 / 500 m), holds a fight to
    // 450 m and sees all round, longer bursts close in
    // (elite's pauses: the recoil still has to settle at range), next to no breather between fights; closes to 6 m and chases
    // 20 s; never runs for cover from an enemy within 40 m (further off: below 25 hp, losing the exchange, outnumbered or
    // reloading under fire); quick irregular side steps close in; looks round every 0.2 s.
    {0.03f, 30,  2, 0.050f, 0.45f, 0.010f, 8.0f, 450, 400, 500, 180.00f, 0.2f, 0.05f, 25,  6, 2,  6, 50, 1.0f, 3.0f, 1.5f, 25, 0.90f, 0, 15,  4,  3, 0,   0.0f,  1, 1.3f,
     40.0f, 20.0f, 1, 3, 0.2f},
};
static const char *const kLevelNames[BM_LEVELS + 1] = {"off", "easy", "normal", "hard", "veteran", "elite", "nightmare"};
// ---- AltBotMod V39: the human profile ----
// With HumanStyle=1 every difficulty level plays one profile built from the "normal" row: a person's reaction (HumanReactionSeconds 0.25 s) and
// first-shot delay (HumanFirstShotSeconds 0.2 s) before a burst, the weapon's own burst / pause rhythm, InfantryEngageMeters (70 m) as the range
// bots notice and fight at, a 160 degree view (80 either side), HumanStrafe while fighting, no hops while fighting or running (only
// a jump to get past an obstacle), and about every other bot crouching to shoot. The aim error itself (sway, settle, jolt) is the
// bm_haim_* model below, applied by core.c.
// (Hunt v4 merge: with Hunt on, the human profile also fights like a hunter - it closes in on a fight from 25 m in and holds
// and fires further out, chases one that ducked out of sight for 20 s and does not run for cover from an enemy within 40 m;
// SnapAim=1 aims for it too. Its aim error, reaction, first-shot delay, rhythm, view and strafe pattern stay the human style's.)
static BMDifficulty gHumanLevels[BM_LEVELS];
static int gHumanOn;
void bm_human_style_apply(const BMConfig *c)
{
    gHumanOn = c && c->humanStyle;
    if (!gHumanOn) return;
    for (int k = 0; k < BM_LEVELS; k++) {
        BMDifficulty P = kLevels[1];
        P.reaction = c->humanReaction;
        P.settle = (int)(c->humanFirstShot * 30.0f + 0.5f);
        P.burst = 12; P.pause = 10;
        P.aimTol = 0.060f; P.aimMargin = 0.60f;   // (the aim error is in the aim itself; the fire window only has to let a steady aim shoot)
        P.fightRange = P.engageRange = P.detectRange = c->infantryRange;
        P.fov = c->humanFov * 0.5f;                 // the profile's FOV is a half angle
        P.strafe = c->humanStrafe;
        P.jumpRun = P.jumpFight = 0;
        P.farCrouch = 5.0f; P.farCrouchChance = 0.5f;
        P.skillSpread = c->humanAimSpread;
        if (c->hunt) { P.push = 1; P.chaseSeconds = 20.0f; P.noCoverMeters = 40.0f; }   // (Hunt v4: a hunter's aggression)
        if (k == BM_LEVELS - 1) {   // Hunt v4: nightmare keeps what makes it nightmare - it never lets up, steps quick and irregular
            P.push = 1; P.chaseSeconds = kLevels[k].chaseSeconds; P.noCoverMeters = kLevels[k].noCoverMeters;   // close in, still with the human aim
            P.dodge = kLevels[k].dodge; P.detectSeconds = kLevels[k].detectSeconds;
        }
        gHumanLevels[k] = P;
    }
}
const BMDifficulty *bm_difficulty_level(int level)
{
    if (level < 1) level = 1;
    if (level > BM_LEVELS) level = BM_LEVELS;
    return gHumanOn ? &gHumanLevels[level - 1] : &kLevels[level - 1];
}
const char *bm_difficulty_name(int level) { return level >= 0 && level <= BM_LEVELS ? kLevelNames[level] : "?"; }
int bm_difficulty_parse(const char *text)
{
    if (!text) return -1;
    while (isspace((unsigned char)*text)) text++;
    char t[16]; size_t n = 0;
    while (text[n] && !isspace((unsigned char)text[n]) && n + 1 < sizeof t) { t[n] = text[n]; n++; }
    t[n] = 0;
    if (!n) return -1;
    for (int k = 0; k <= BM_LEVELS; k++) if (equal_ci(t, kLevelNames[k])) return k;
    if (n == 1 && t[0] >= '0' && t[0] <= '0' + BM_LEVELS) return t[0] - '0';
    return -1;
}
int bm_bot_difficulty(const BMConfig *c, const char *name)
{
    if (!c || c->difficulty <= 0) return 0;
    if (!c->randomDifficulty || !name) return c->difficulty;
    unsigned h = 2166136261u;   // FNV-1a of the bot's name: the same bot keeps its level for the session
    for (const char *p = name; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    int level = c->difficulty + (int)(h % 3) - 1;
    return level < 1 ? 1 : level > BM_LEVELS ? BM_LEVELS : level;
}
int bm_profile_burst(const BMDifficulty *d, int weaponBurst, float distance)
{
    if (weaponBurst < 1) weaponBurst = 1;
    if (!d) return weaponBurst;
    float k = d->burst / 12.0f;
    // recoil climbs: the far bursts stay taps (V5), but scaled with the level - an elite bot holds a little longer
    int cap = distance > 40 ? 4 : distance > 20 ? 7 : 12;
    // AltBotMod V23: at range the burst stays short whatever the level (recoil): the cap grows with the level only close in
    float kc = distance > 20 ? fminf(k, 1.25f) : k;
    int n = (int)(weaponBurst * k + 0.5f), c = (int)(cap * kc + 0.5f);
    if (n > c) n = c;
    return n < 1 ? 1 : n;
}
int bm_profile_pause(const BMDifficulty *d, int weaponPause)
{
    if (!d) return weaponPause < 1 ? 1 : weaponPause;
    int n = (int)(weaponPause * (d->pause / 10.0f) + 0.5f);
    return n < 1 ? 1 : n;
}
// AltBotMod V14: side steps. V9-V13 stepped left / right without a break the whole time a bot traded shots within
// 80 m (0.5-1.4 s each way at full strafe) - it looked like strutting. Now (1): a short step (0.3-0.55 s) at 60 % of
// the profile's strafe, then 1.2-3 s standing still, only while being shot at or with the enemy within 20 m, out to
// 45 m; hops only under fire and three times rarer. 2 keeps the old behaviour.
int bm_dodge_style(const BMConfig *c, const BMDifficulty *P, BMDodgeStyle *o)
{
    memset(o, 0, sizeof *o);
    int mode = c ? c->sideSteps : 1;
    if (!P || mode <= 0) { o->hopScale = 0; return 0; }
    if (P->strafe <= 0) { o->hopScale = mode >= 2 ? 1.0f : 3.0f; o->hopNeedsThreat = mode < 2; return 0; }
    if (P->dodge >= 3) {   // (Hunt v4 merge: nightmare's own steps first, then SideSteps=3's step and shoot)
        // AltBotMod Hunt (nightmare): what a good player does in a close gunfight - full-speed steps of 0.27-0.7 s, a pause of
        // at most a quarter second, mostly back the other way but not always (bm_dodge_next: 65 % reverse), from 20 m in
        // (v3: was 35 - steps spread the shots, and beyond 20 m they went everywhere), shooting or not; hops between bursts
        // at the profile's own rate, within the same 20 m
        o->level = P->strafe; o->maxMeters = 20; o->closeMeters = 20; o->stepMin = 8; o->stepMax = 21;
        o->restMin = 0; o->restMax = 8; o->hopScale = 1.0f; return 1;
    }
    if (mode >= 3) {   // AltBotMod V39: step and shoot - every 2.5 s a bot steps sideways for 1 s (random side), then stands for 1.5 s;
        // always while it fights, at the profile's strafe level, and no hops (a jump only to get past an obstacle)
        o->level = P->strafe; o->maxMeters = 400; o->closeMeters = 400; o->stepMin = o->stepMax = 30; o->restMin = o->restMax = 45;
        o->needThreat = 0; o->hopScale = 0; o->hopNeedsThreat = 0;
        return 1;
    }
    if (mode >= 2) {
        o->level = P->strafe * 0.9f; o->maxMeters = 80; o->closeMeters = 80; o->stepMin = 15; o->stepMax = 40;
        o->hopScale = 1.0f; return 1;
    }
    o->level = P->strafe * 0.6f; o->maxMeters = 45; o->closeMeters = 20; o->stepMin = 9; o->stepMax = 16;
    o->restMin = 36; o->restMax = 90; o->needThreat = 1; o->hopScale = 3.0f; o->hopNeedsThreat = 1;
    return 1;
}
int bm_dodge_next(const BMDodgeStyle *st, signed char *dir, signed char *last, unsigned *until, unsigned now, unsigned r)
{
    if (now < *until) return 0;
    if (*dir && st->restMax > 0) {   // the step is done: stand and shoot for a while
        *last = *dir; *dir = 0;
        *until = now + st->restMin + r % (unsigned)(st->restMax - st->restMin + 1);
        return 0;
    }
    signed char prev = *dir ? *dir : *last;
    *dir = !prev ? ((r & 1) ? 1 : -1) : st->restMax > 0 ? (signed char)(((r >> 8) % 100) < 65 ? -prev : prev) : (signed char)-prev;
    *until = now + st->stepMin + (r >> 16) % (unsigned)(st->stepMax - st->stepMin + 1);
    return 1;
}
// ---- AltBotMod Hunt v4: human aim (HumanStyle) - how a person's crosshair behaves in a fight. Three offsets are added to the
// bot's perfect aim (angles in radians, times in seconds):
//  - sway: a slow random wander around the target that always drifts back (an Ornstein-Uhlenbeck process); its size is sigma
//    sideways and HAIM_PITCH_SHARE of that up and down;
//  - settle: taking a new target, the swing ends a little past it (now and then short of it); the miss dies away in 0.15-0.35 s;
//  - jolt: a hit throws the aim up and to a side; it dies away in a quarter of a second.
// sigma (bm_haim_sigma) is HumanAimErrorMrad at 25 m times the bot's skill. On foot part of it is a fixed miss in metres (its
// angle shrinks with the distance) and part a fixed angle, so the miss in metres grows with the distance, but slower than it.
// Moving and a target crossing the view make it larger. A mounted gun keeps a steady angle (the vehicle holds it).
#define HAIM_REF_METERS       25.0f    // the distance HumanAimErrorMrad is given for
#define HAIM_FIXED_MISS_SHARE 0.7f     // at 25 m: the share of sigma squared that is a fixed miss in metres (the rest: a fixed angle)
#define HAIM_SWAY_SECONDS     0.45f    // how slowly the sway wanders (its correlation time)
#define HAIM_PITCH_SHARE      0.75f    // up / down sway against the sideways one
#define HAIM_JOLT_SECONDS     0.25f
static float haim_uniform(void) { return ((float)rand() + 0.5f) / ((float)RAND_MAX + 1.0f); }   // (0, 1)
static float haim_normal(void)   // a standard normal number (Box-Muller)
{
    float u = haim_uniform(), v = haim_uniform();
    return sqrtf(-2.0f * logf(u)) * cosf(6.2831853f * v);
}
float bm_haim_trait(const char *botName)
{
    uint32_t h = 5381u;   // djb2 over the name, then murmur3's finaliser so that "Bot 1" and "Bot 2" end up far apart
    for (const unsigned char *p = (const unsigned char *)(botName ? botName : ""); *p; p++) h = h * 33u + *p;
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return (float)(h >> 8) / 16777216.0f;
}
float bm_haim_skill(float trait, float spread)
{
    spread = clampf(spread, 0.0f, 1.0f);
    return 1.0f - spread + 2.0f * spread * clampf(trait, 0.0f, 1.0f);
}
float bm_haim_recoil_share(float best, float worst, float trait)
{
    float t = clampf(trait, 0.0f, 1.0f);
    return clampf(best + (worst - best) * t, 0.0f, 1.0f);
}
float bm_haim_sigma(float errorMrad, float distance, float ownSpeed, float crossing, float skill, int onFoot)
{
    if (!(errorMrad > 0.0f) || !(skill > 0.0f)) return 0.0f;
    float base = errorMrad * 0.001f * skill;
    if (!onFoot) return base;
    float closeness = HAIM_REF_METERS / fmaxf(isfinite(distance) ? distance : HAIM_REF_METERS, 3.0f);
    float s = base * sqrtf(HAIM_FIXED_MISS_SHARE * closeness * closeness + (1.0f - HAIM_FIXED_MISS_SHARE));
    s *= 1.0f + 0.12f * clampf(isfinite(ownSpeed) ? ownSpeed : 0.0f, 0.0f, 7.0f);      // walking at 4 m/s: x1.5
    s *= 1.0f + 2.0f * clampf(isfinite(crossing) ? crossing : 0.0f, 0.0f, 0.75f);    // a target crossing at 0.25 rad/s: x1.5
    return s;
}
void bm_haim_reset(BMHumanAim *a) { memset(a, 0, sizeof *a); }
void bm_haim_jolt(BMHumanAim *a, float skill)
{
    float k = 0.6f + 0.4f * clampf(skill, 0.5f, 1.5f);   // a shakier bot is jolted more
    a->jolt[1] = fminf(a->jolt[1] + k * (0.009f + 0.006f * haim_uniform()), 0.04f);   // up, 9-15 mrad
    a->jolt[0] = clampf(a->jolt[0] + k * (haim_uniform() < 0.5f ? -1.0f : 1.0f) * (0.003f + 0.005f * haim_uniform()), -0.03f, 0.03f);
}
void bm_haim_step(BMHumanAim *a, float dt, float sigma, float skill, float turnYaw, float turnPitch, float *yawError, float *pitchError)
{
    float sd[2] = {fmaxf(sigma, 0.0f), fmaxf(sigma, 0.0f) * HAIM_PITCH_SHARE}, turn[2] = {turnYaw, turnPitch};
    dt = clampf(isfinite(dt) ? dt : 0.0f, 0.0f, 0.25f);
    if (!a->started) {   // a new target: the sway is already somewhere, and the swing onto it misses by a part of itself
        a->started = 1;
        a->settleSeconds = 0.10f + 0.12f * clampf(skill, 0.4f, 2.0f);
        for (int k = 0; k < 2; k++) {
            a->sway[k] = sd[k] * haim_normal();
            float part = (0.04f + 0.16f * haim_uniform()) * clampf(skill, 0.3f, 2.0f) * (haim_uniform() < 0.75f ? 1.0f : -0.5f);
            a->settle[k] = isfinite(turn[k]) ? clampf(turn[k] * part, -0.08f, 0.08f) : 0.0f;
        }
    } else if (dt > 0.0f) {
        float keep = expf(-dt / HAIM_SWAY_SECONDS), fresh = sqrtf(fmaxf(0.0f, 1.0f - keep * keep));
        float settle = expf(-dt / fmaxf(a->settleSeconds, 0.05f)), jolt = expf(-dt / HAIM_JOLT_SECONDS);
        for (int k = 0; k < 2; k++) {
            a->sway[k] = a->sway[k] * keep + sd[k] * fresh * haim_normal();
            a->settle[k] *= settle; a->jolt[k] *= jolt;
        }
    }
    *yawError = a->sway[0] + a->settle[0] + a->jolt[0];
    *pitchError = a->sway[1] + a->settle[1] + a->jolt[1];
}
