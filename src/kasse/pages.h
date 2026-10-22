#ifndef KASSE_PAGES_H
#define KASSE_PAGES_H

#include "webstyle.h"

// Weboberflaeche der Kasse. Gast-Seite (US2, US4, US5) und Personal-Seite (US7, US8).
// Beide Seiten laden ihre Daten per fetch() von der JSON-Schnittstelle der Kasse.


static const char INDEX_HTML[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Parkhaus Kasse</title>
<link rel="stylesheet" href="/style.css">
</head><body><main>
<header><h1>Parkhaus Kasse</h1><span class="mute" id="clock">&nbsp;</span></header>

<section class="card" aria-live="polite">
  <h2>Freie Plätze</h2>
  <div class="big" id="free">–</div>
  <div class="mute" id="freeNote"></div>
</section>

<section class="card">
  <h2>Parkgebühr bezahlen</h2>
  <label for="id">Kennzeichen oder Ticketcode</label>
  <input type="text" id="id" placeholder="z. B. ZH 123456 oder TK0001" autocomplete="off" autocapitalize="characters" spellcheck="false">
  <div class="row"><button id="btnQuote">Gebühr anzeigen</button></div>

  <div id="quote" hidden>
    <dl>
      <dt>Einfahrt</dt><dd id="qEntry"></dd>
      <dt>Parkdauer</dt><dd id="qDur"></dd>
      <dt>Tarif</dt><dd id="qTariff"></dd>
      <dt>Gebühr bisher</dt><dd id="qFee"></dd>
      <dt>Bereits bezahlt</dt><dd id="qPaid"></dd>
      <dt>Offener Betrag</dt><dd class="due" id="qDue"></dd>
    </dl>
    <fieldset id="payBox">
      <legend>Zahlungsmittel</legend>
      <label><input type="radio" name="method" value="karte" checked> Kreditkarte</label>
      <label><input type="radio" name="method" value="twint"> Twint</label>
      <div class="row"><label style="font-weight:400"><input type="checkbox" id="decline"> Zahlung ablehnen lassen (Test der Fehlermeldung)</label></div>
      <div class="row"><button id="btnPay">Jetzt bezahlen</button></div>
    </fieldset>
  </div>
  <div class="msg" id="msg" role="status" hidden></div>
</section>

<p class="mute" id="foot"></p>
<p class="mute"><a href="/staff">Personalbereich</a></p>
</main>
<script>
const $ = id => document.getElementById(id);
let currentId = "";
function say(text, kind){ const m=$("msg"); m.textContent=text; m.className="msg "+kind; m.hidden=false; }
async function getJson(url, opt){ const r = await fetch(url, opt); return r.json(); }

async function refreshState(){
  try{
    const s = await getJson("/api/state");
    $("free").textContent = s.free === 0 ? "Besetzt" : s.free;
    $("free").className = "big" + (s.free === 0 ? " full" : "");
    $("freeNote").textContent = s.free === 0 ? "Keine Einfahrt möglich" : "von " + s.capacity + " Plätzen";
    $("clock").textContent = "Simulationszeit " + s.simTime + " (1 s = " + s.scale + " s)";
    $("foot").textContent = "Tarif: " + s.tariff + ". Bezahlung gilt " + s.validMinutes + " Minuten für die Ausfahrt.";
  }catch(e){ $("freeNote").textContent = "Keine Verbindung zur Kasse"; }
}

function renderQuote(q){
  $("qEntry").textContent = q.entry; $("qDur").textContent = q.duration;
  $("qTariff").textContent = q.tariff; $("qFee").textContent = "CHF " + q.fee;
  $("qPaid").textContent = "CHF " + q.paid; $("qDue").textContent = "CHF " + q.due;
  $("quote").hidden = false;
}

async function showQuote(){
  currentId = $("id").value;
  $("msg").hidden = true;
  const q = await getJson("/api/quote?id=" + encodeURIComponent(currentId));
  if(!q.found){ $("quote").hidden = true; say("Kein Fahrzeug mit diesem Kennzeichen im Parkhaus gefunden.", "bad"); return; }
  renderQuote(q);
  if(q.payValid){ say("Bezahlt. Die Ausfahrt ist noch " + q.validLeftMin + " Minuten gültig.", "ok"); }
}

async function pay(){
  const method = document.querySelector("input[name=method]:checked").value;
  const body = new URLSearchParams({id: currentId, method, decline: $("decline").checked ? "1" : "0"});
  $("btnPay").disabled = true;
  try{
    const r = await getJson("/api/pay", {method: "POST", body});
    say(r.message, r.result === "OK" ? "ok" : "bad");
    await showQuoteSilently();
  } finally { $("btnPay").disabled = false; }
}
async function showQuoteSilently(){
  const q = await getJson("/api/quote?id=" + encodeURIComponent(currentId));
  if(q.found) renderQuote(q);
}

$("btnQuote").addEventListener("click", showQuote);
$("id").addEventListener("keydown", e => { if(e.key === "Enter") showQuote(); });
$("btnPay").addEventListener("click", pay);
refreshState(); setInterval(refreshState, 2000);
</script></body></html>
)HTML";

static const char STAFF_HTML[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Parkhaus Personal</title>
<link rel="stylesheet" href="/style.css">
</head><body><main>
<header><h1>Personalbereich</h1><span class="mute" id="clock">&nbsp;</span></header>

<section class="card">
  <h2>Anlage</h2>
  <div>Freie Plätze: <strong id="free">–</strong> von <span id="cap">–</span> · Datensätze im System: <strong id="count">–</strong></div>
  <div class="row"><span class="mute">Zeitraffer (1 s echt =)</span>
    <button class="secondary" data-scale="1">1 s</button>
    <button class="secondary" data-scale="60">1 min</button>
    <button class="secondary" data-scale="600">10 min</button>
    <button class="secondary" data-scale="3600">1 h</button>
  </div>
  <div class="mute" id="scaleNote"></div>
</section>

<section class="card">
  <h2>Ruftaste-Anfragen</h2>
  <div id="calls" class="mute">Keine offenen Anfragen.</div>
  <label for="manualId" style="margin-top:12px">Fahrzeug an der Ausfahrt (für Anfragen „Ausfahrt“)</label>
  <input type="text" id="manualId" placeholder="Kennzeichen oder Ticketcode" autocomplete="off" spellcheck="false">
  <div class="msg" id="msg" role="status" hidden></div>
</section>

<section class="card">
  <h2>Fahrzeuge (Kennzeichen nur für Personal sichtbar)</h2>
  <table><thead><tr><th>Nr</th><th>Kennzeichen / Ticket</th><th>Einfahrt</th><th>Status</th><th>Bezahlt</th></tr></thead><tbody id="rows"></tbody></table>
  <p class="mute">Nach der Ausfahrt werden Datensätze nach 24 Stunden (Simulationszeit) automatisch gelöscht.</p>
</section>

<section class="card"><h2>Ereignisse</h2><pre id="log"></pre></section>
<p class="mute"><a href="/">Zur Kasse</a></p>
</main>
<script>
const $ = id => document.getElementById(id);
const esc = s => String(s).replace(/[&<>"]/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));
function say(text, kind){ const m=$("msg"); m.textContent=text; m.className="msg "+kind; m.hidden=false; }
async function post(url, params){ const r = await fetch(url, {method:"POST", body:new URLSearchParams(params)}); return r.json(); }

async function approve(call, outbound){
  const id = outbound ? $("manualId").value : "";
  if(outbound && !id){ say("Bitte zuerst Kennzeichen oder Ticketcode des Fahrzeugs eingeben.", "warn"); return; }
  const r = await post("/api/staff/approve", {call, id});
  say(r.message, r.result === "OK" ? "ok" : "bad"); refresh();
}
async function reject(call){ await post("/api/staff/reject", {call}); refresh(); }

function renderCalls(calls){
  const open = calls.filter(c => c.state === "pending");
  if(!open.length){ $("calls").innerHTML = "Keine offenen Anfragen."; return; }
  $("calls").innerHTML = open.map(c =>
    `<div class="row"><strong>Anfrage ${c.no}: ${c.outbound ? "Ausfahrt" : "Einfahrt"}</strong>
     <span class="${c.waitSec > 60 ? "full" : "mute"}">wartet seit ${c.waitSec} s${c.waitSec > 60 ? " (Ziel 60 s überschritten)" : ""}</span>
     <button onclick="approve(${c.no}, ${c.outbound})">${c.outbound ? "Ausfahrt prüfen und öffnen" : "Ticket ausgeben und öffnen"}</button>
     <button class="secondary" onclick="reject(${c.no})">Ablehnen</button></div>`).join("");
}

async function refresh(){
  const r = await fetch("/api/staff/state");
  if(!r.ok){ return; }
  const s = await r.json();
  $("clock").textContent = "Simulationszeit " + s.simTime;
  $("free").textContent = s.free; $("cap").textContent = s.capacity; $("count").textContent = s.records.length;
  $("scaleNote").textContent = "Aktuell 1 s echt = " + s.scale + " s simuliert.";
  renderCalls(s.calls);
  $("rows").innerHTML = s.records.map(x =>
    `<tr><td>${x.no}</td><td>${esc(x.id)}${x.ticket ? " (Ticket)" : ""}</td><td>${esc(x.entry)}</td>
     <td>${x.exited ? "ausgefahren " + esc(x.exit) : "im Parkhaus"}</td><td>${x.paid ? "CHF " + x.paid : "offen"}</td></tr>`).join("");
  $("log").textContent = s.log.join("\n");
}

document.querySelectorAll("[data-scale]").forEach(b => b.addEventListener("click", async () => {
  await post("/api/staff/scale", {value: b.dataset.scale}); refresh(); }));
refresh(); setInterval(refresh, 1500);
</script></body></html>
)HTML";

#endif
