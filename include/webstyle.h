#ifndef PARK_WEBSTYLE_H
#define PARK_WEBSTYLE_H

// Gemeinsames Stylesheet der Weboberflaechen von Kasse und Schranke (hell/dunkel).

static const char STYLE_CSS[] = R"CSS(
:root{--bg:#f3f5f8;--card:#fff;--ink:#17202b;--mute:#5a6676;--line:#dde2e9;--accent:#1458d6;--accent-ink:#fff;--ok:#0f7a55;--bad:#b3261e;--warn:#8a5a00}
@media(prefers-color-scheme:dark){:root{--bg:#0f141b;--card:#18202a;--ink:#e7ecf2;--mute:#97a3b3;--line:#2a3441;--accent:#6aa3ff;--accent-ink:#0b1220;--ok:#46c795;--bad:#ff8277;--warn:#f0b955}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:720px;margin:0 auto;padding:16px}
header{display:flex;justify-content:space-between;align-items:baseline;gap:12px;flex-wrap:wrap;margin-bottom:12px}
h1{font-size:1.4rem;margin:0}
h2{font-size:1.05rem;margin:0 0 10px}
.mute{color:var(--mute);font-size:.9rem}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:12px}
.big{font-size:3rem;font-weight:700;line-height:1;font-variant-numeric:tabular-nums}
.full{color:var(--bad)}
label{display:block;font-weight:600;margin-bottom:6px}
input[type=text],input[type=password]{width:100%;font:inherit;padding:10px 12px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--ink);text-transform:uppercase}
button{font:inherit;font-weight:600;padding:10px 16px;border:0;border-radius:8px;background:var(--accent);color:var(--accent-ink);cursor:pointer;min-height:44px}
button.secondary{background:transparent;color:var(--accent);border:1px solid var(--accent)}
button:disabled{opacity:.5;cursor:not-allowed}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:10px}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 16px;margin:12px 0 0}
dt{color:var(--mute)}dd{margin:0;text-align:right;font-variant-numeric:tabular-nums}
.due{font-size:1.6rem;font-weight:700}
.msg{margin-top:12px;padding:10px 12px;border-radius:8px;border:1px solid var(--line)}
.msg.ok{border-color:var(--ok);color:var(--ok)}.msg.bad{border-color:var(--bad);color:var(--bad)}.msg.warn{border-color:var(--warn);color:var(--warn)}
fieldset{border:1px solid var(--line);border-radius:8px;margin:12px 0 0;padding:8px 12px}
fieldset label{display:inline-block;font-weight:400;margin:0 16px 0 0}
table{width:100%;border-collapse:collapse;font-size:.92rem;font-variant-numeric:tabular-nums}
th,td{text-align:left;padding:6px 8px;border-bottom:1px solid var(--line)}
th{color:var(--mute);font-weight:600}
pre{margin:0;white-space:pre-wrap;font:.82rem/1.5 ui-monospace,Menlo,monospace;max-height:220px;overflow:auto}
a{color:var(--accent)}
[hidden]{display:none!important}
)CSS";

#endif
