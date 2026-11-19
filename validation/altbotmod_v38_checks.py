from pathlib import Path
p=Path(__file__).resolve().parents[1]/'files/BotMod/src/core.c'
s=p.read_text()
checks={
 '70 m emergency threat scan (V39: 15 m with HumanStyle)':'#define CLOSE_THREAT_RANGE     (g_enhanced.humanStyle ? 15.0f : 70.0f)' in s,
 'direct pursuit watchdog shortened to 1 s':'H->frame - pg->frame > 30) { // V38: 1.0 s' in s,
 'general stuck recovery begins at 0.8 s':'moved2 < 0.09f && H->frame - pg->frame > 24' in s,
 'ground vehicle acceleration prediction enabled':'airframe || profile->kind==BM_AA || isVeh' in s,
 'moving target tolerance updated':'isVeh?1.20f:0.22f' in s,
}
for name,ok in checks.items():
 print(('PASS: ' if ok else 'FAIL: ')+name)
assert all(checks.values())
