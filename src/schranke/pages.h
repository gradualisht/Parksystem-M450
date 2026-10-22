#ifndef SCHRANKE_PAGES_H
#define SCHRANKE_PAGES_H

#include "webstyle.h"

// Bedienseite des Schranken-Pico: simuliert Kennzeichenerkennung und Ruftaste an Ein- und Ausfahrt
// und zeigt die Anzeige der Einfahrt (US1, US2, US6, US7).

static const char SCHRANKE_HTML[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Schranken-Simulator</title>
<link rel="stylesheet" href="/style.css">
<style>
.gate{display:flex;align-items:center;gap:12px;margin:12px 0}
.post{width:10px;height:44px;background:var(--mute);border-radius:3px;position:relative}
.arm{position:absolute;left:5px;top:2px;width:90px;height:8px;background:var(--bad);border-radius:4px;transform-origin:0 50%;transition:transform .4s}
.arm.open{transform:rotate(-70deg);background:var(--ok)}
.gate .stage{width:110px;height:48px;position:relative}
.stat{font-weight:600}
</style>
</head><body><main>
<header><h1>Schranken-Simulator</h1><span class="mute" id="net">&nbsp;</span></header>

<section class="card" aria-live="polite">
  <h2>Einfahrt</h2>
  <div class="big" id="free">–</div>
  <div class="mute" id="freeNote">Freie Plätze</div>
  <div class="gate"><div class="stage"><div class="post"><div class="arm" id="armIn"></div></div></div><span class="stat" id="stIn">Schranke geschlossen</span></div>
  <div class="msg" id="txtIn">Bereit.</div>
  <label for="plateIn" style="margin-top:12px">Kamera erkennt Kennzeichen</label>
  <input type="text" id="plateIn" placeholder="z. B. ZH 123456" autocomplete="off" spellcheck="false">
  <div class="row">
    <button id="btnIn">Fahrzeug fährt ein</button>
    <button class="secondary" id="btnInNone">Kennzeichen nicht erkannt</button>
    <button class="secondary" id="btnCallIn">Ruftaste Einfahrt</button>
  </div>
</section>

<section class="card">
  <h2>Ausfahrt</h2>
  <div class="gate"><div class="stage"><div class="post"><div class="arm" id="armOut"></div></div></div><span class="stat" id="stOut">Schranke geschlossen</span></div>
  <div class="msg" id="txtOut">Bereit.</div>
  <label for="plateOut" style="margin-top:12px">Kamera erkennt Kennzeichen (oder Ticket scannen)</label>
  <input type="text" id="plateOut" placeholder="z. B. ZH 123456 oder TK0001" autocomplete="off" spellcheck="false">
  <div class="row">
    <button id="btnOut">Fahrzeug fährt aus</button>
    <button class="secondary" id="btnOutNone">Kennzeichen nicht erkannt</button>
    <button class="secondary" id="btnCallOut">Ruftaste Ausfahrt</button>
  </div>
</section>

<section class="card">
  <h2>Ereignisse</h2>
  <div class="mute" id="latency"></div>
  <pre id="log"></pre>
</section>
<p class="mute"><a id="kasseLink" href="http://192.168.4.1/">Zur Kasse (Bezahlung)</a></p>
</main>
<script>
const $ = id => document.getElementById(id);
async function act(url, params){ await fetch(url, {method:"POST", body:new URLSearchParams(params || {})}); refresh(); }

function gate(prefix, g){
  $("arm" + prefix).className = "arm" + (g.open ? " open" : "");
  $("st" + prefix).textContent = g.open ? "Schranke offen" : "Schranke geschlossen";
  $("txt" + prefix).textContent = g.text;
}

async function refresh(){
  let s;
  try{ s = await (await fetch("/api/status")).json(); }catch(e){ $("net").textContent = "Keine Verbindung zum Schranken-Pico"; return; }
  $("net").textContent = s.online ? "Kasse verbunden" : "Kasse nicht erreichbar";
  const full = s.free === 0;
  $("free").textContent = s.free < 0 ? "–" : (full ? "Besetzt" : s.free);
  $("free").className = "big" + (full ? " full" : "");
  $("freeNote").textContent = s.free < 0 ? "Freie Plätze unbekannt" : (full ? "Einfahrt gesperrt" : "Freie Plätze von " + s.capacity);
  gate("In", s.entry); gate("Out", s.exit);
  $("latency").textContent = s.latencyMs >= 0 ? "Letzte Reaktionszeit der Schranke: " + s.latencyMs + " ms (Ziel unter 3000 ms)" : "";
  $("log").textContent = s.log.join("\n");
}

$("btnIn").addEventListener("click", () => act("/api/enter", {id: $("plateIn").value}));
$("btnInNone").addEventListener("click", () => act("/api/enter", {id: ""}));
$("btnCallIn").addEventListener("click", () => act("/api/call", {dir: "in"}));
$("btnOut").addEventListener("click", () => act("/api/exit", {id: $("plateOut").value}));
$("btnOutNone").addEventListener("click", () => act("/api/exit", {id: ""}));
$("btnCallOut").addEventListener("click", () => act("/api/call", {dir: "out"}));
refresh(); setInterval(refresh, 1000);
</script></body></html>
)HTML";

#endif
