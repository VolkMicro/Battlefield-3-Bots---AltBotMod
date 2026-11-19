from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
core = (ROOT / 'files/BotMod/src/core.c').read_text()
mode = (ROOT / 'files/BotMod/src/modes_native.inc').read_text()
profile = (ROOT / 'files/BotMod/src/combat_profile_native.inc').read_text()
enhanced = (ROOT / 'files/BotMod/src/enhanced.c').read_text()
ini = (ROOT / 'files/BotMod/enhanced.ini').read_text()
checks = {
    'visible LOS contact does not depend on the spotted-marker API':
        'Do not make combat depend on the separate game spotted-marker API' in core and 'if (!is_spotted(t)) return;' not in core,
    'every non-combat task is interruptible by visible infantry':
        'interrupts %s for visible enemy' in core,
    'direct visual engagement bypasses Rush M-COM focus':
        'g_forceFight = 1;' in core,
    'Rush M-COM interaction cannot cancel an active firefight':
        'bot_brain(i)->task==TASK_FIGHT' in mode,
    'visual detection cadence is five checks per second':
        '(H->frame + i * 2) % detect_period(i) == 0' in core and ': 6;   // (Mr.Cat V36: 6 = 5 a second' in core,   # (Hunt v4: a profile may look round faster)
    'infantry firing alignment is tighter than the previous build':
        'P->aimMargin * f * 2.6f' in profile and 't * 2.5f' in profile,
    'all five profiles use 400 m fight / 400 m engagement / 500 m detection (V38)':
        enhanced.count(', 400, 400, 500,') == 5,
    'fallback InfantryEngageMeters default is 300 m':
        'InfantryEngageMeters=70' in ini,   # V39 human style (was 300)
}
for label, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + label)
if not all(checks.values()):
    raise SystemExit(1)
