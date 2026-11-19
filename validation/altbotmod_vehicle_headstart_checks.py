#!/usr/bin/env python3
"""Ensure bots leave vehicles available during the human join window."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'files/BotMod/src'
fleet = (SRC / 'fleet_native.inc').read_text()
state = (SRC / 'fleet_state.h').read_text()
air = (SRC / 'air_native.inc').read_text()
air_dispatch = (SRC / 'air_dispatch.inc').read_text()
abandoned = (SRC / 'abandoned_dispatch.inc').read_text()
spawn_vehicle = (SRC / 'spawn_vehicle.inc').read_text()
deployment = (SRC / 'deployment_native.inc').read_text()
gunship = (SRC / 'gunship_native.inc').read_text()
emplacement = (SRC / 'emplacement_native.inc').read_text()
core = (SRC / 'core.c').read_text()
checks = {
    'head-start is 90 seconds from level readiness':
        '#define FLEET_HUMAN_HEADSTART_FRAMES (90u * 30u)' in fleet and
        'g_levelReadyAt = H->frame ? H->frame : 1;' in core,
    'shared head-start helper is declared for every vehicle subsystem':
        'static int fleet_human_headstart(void);' in state and
        'static int fleet_human_headstart(void)' in fleet,
    'ground fleet reservation, respawn selection and walking dispatch are blocked':
        'if(fleet_human_headstart())return 0;' in fleet and
        'fleet_human_headstart()) return NULL;' in fleet and
        'if(fleet_human_headstart())return;' in fleet,
    'aircraft claims and opportunistic aircraft acquisition are blocked':
        air.count('if(fleet_human_headstart())') >= 2,
    'team aircraft dispatch is blocked':
        'if(fleet_human_headstart() || !mode_uses_vehicles()' in air_dispatch,
    'abandoned ground-vehicle dispatch is blocked':
        'if (fleet_human_headstart() || !mode_uses_vehicles()' in abandoned,
    'spawn-screen vehicle picks are blocked':
        'if (fleet_human_headstart() || !mode_uses_vehicles()' in spawn_vehicle,
    'passenger seat assignments and pending crew boarding are blocked':
        'if(fleet_human_headstart())return NULL;' in core and 'if (fleet_human_headstart()) { g_crewVehicle[i] = NULL;' in core and 'if (fleet_human_headstart()) return;' in core,
    'spawn-menu aircraft deployment is blocked':
        'if(fleet_human_headstart())return 0;' in deployment,
    'gunship and stationary-emplacement seats are left available':
        'if (fleet_human_headstart() || !mode_uses_vehicles()' in gunship and 'if (fleet_human_headstart() || (H->frame + i) % 30' in emplacement,
    'squad-spawn passenger rides are blocked':
        'if (fleet_human_headstart()) return;' in core,
    'old two-minute vehicle rush has been removed':
        'FLEET_ROUND_RUSH' not in fleet and 'FLEET_ROUND_RUSH' not in core,
}
for label, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + label)
if not all(checks.values()):
    raise SystemExit(1)
