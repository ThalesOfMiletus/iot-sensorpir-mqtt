from __future__ import annotations
import sqlite3, os
from datetime import datetime, timezone
from flask import Flask, request, jsonify, Response

APP_PORT = int(os.getenv("PORT", "5000"))
DB_PATH = os.getenv("DB_PATH", "events.db")

app = Flask(__name__)

PAGE_HTML = """<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>ESP32 PIR - Painel</title>
  <style>
    body{font-family:system-ui,Arial;margin:24px}
    table{border-collapse:collapse;width:100%}
    th,td{border:1px solid #ddd;padding:8px} th{background:#f5f5f5;text-align:left}
    .row{display:flex;gap:8px;margin:12px 0;flex-wrap:wrap}
    button{padding:8px 12px;border:1px solid #ccc;border-radius:8px;background:#fff;cursor:pointer}
    .muted{color:#666}
    code{background:#f3f3f3;padding:1px 4px;border-radius:4px}
    .badge{display:inline-block;padding:2px 10px;border-radius:999px;font-size:0.9em;margin-left:8px}
    .on{background:#e6ffe6;border:1px solid #b6e3b6}
    .off{background:#ffe6e6;border:1px solid #e3b6b6}
  </style>
</head>
<body>
  <h2>ESP32 PIR — Painel</h2>
  <p>Endpoints: <code>/api/events</code>, <code>/api/sensor-state</code>, <code>/api/light-state</code>, <code>/api/clear</code></p>

  <div class="row">
    <button onclick="setEnabled(true)">Ativar sensor</button>
    <button onclick="setEnabled(false)">Desativar sensor</button>
    <button onclick="clearEvents()">Limpar eventos</button>
    <button onclick="load()">Atualizar</button>
  </div>

  <p>
    Sensor: <b id="state">—</b>
    <span id="lightBadge" class="badge off">Luz: —</span>
    | Total eventos: <b id="count">0</b>
  </p>

  <table>
    <thead><tr><th>Timestamp (UTC)</th><th>Device</th><th>Tipo</th><th>Detalhe</th></tr></thead>
    <tbody id="tbody"><tr><td colspan="4" class="muted">Carregando…</td></tr></tbody>
  </table>

<script>
async function setEnabled(enabled){
  const r = await fetch('/api/sensor-state', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({enabled})
  });
  const j = await r.json();
  document.getElementById('state').textContent = j.enabled ? 'ATIVO' : 'INATIVO';
}

async function clearEvents(){
  await fetch('/api/clear', {method:'POST'});
  load();
}

function setLightBadge(isOn){
  const b = document.getElementById('lightBadge');
  b.textContent = 'Luz: ' + (isOn ? 'LIGADA' : 'DESLIGADA');
  b.classList.toggle('on',  isOn);
  b.classList.toggle('off', !isOn);
}

async function load(){
  const [s,e,l] = await Promise.all([
    fetch('/api/sensor-state').then(r=>r.json()),
    fetch('/api/events?limit=200').then(r=>r.json()),
    fetch('/api/light-state').then(r=>r.json()).catch(()=>({on:false}))
  ]);
  document.getElementById('state').textContent = s.enabled ? 'ATIVO' : 'INATIVO';
  document.getElementById('count').textContent = e.total;
  setLightBadge(!!l.on);

  const tb = document.getElementById('tbody'); tb.innerHTML='';
  if(e.items.length===0){ tb.innerHTML = '<tr><td colspan="4" class="muted">Sem eventos.</td></tr>'; return; }
  for (const it of e.items){
    const tr = document.createElement('tr');
    tr.innerHTML = `<td><code>${it.ts}</code></td><td>${it.device_id||''}</td><td>${it.type}</td><td>${it.detail||''}</td>`;
    tb.appendChild(tr);
  }
}

load(); setInterval(load, 5000);
</script>
</body></html>
"""

# --- DB helpers ---
def db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn

def init_db():
    with db() as con:
        con.execute("""
          CREATE TABLE IF NOT EXISTS events(
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ts TEXT NOT NULL,
            device_id TEXT,
            type TEXT NOT NULL,
            detail TEXT
          )
        """)
        con.execute("""
          CREATE TABLE IF NOT EXISTS settings(
            id INTEGER PRIMARY KEY CHECK (id=1),
            sensor_enabled INTEGER NOT NULL DEFAULT 1
          )
        """)
        # ensure settings row
        cur = con.execute("SELECT COUNT(*) c FROM settings WHERE id=1")
        if cur.fetchone()["c"] == 0:
            con.execute("INSERT INTO settings(id, sensor_enabled) VALUES(1,1)")
        con.commit()

def now_utc_iso():
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")

# --- Routes ---
@app.get("/")
def home():
    return Response(PAGE_HTML, mimetype="text/html; charset=utf-8")

@app.get("/api/health")
def health():
    return {"ok": True}

@app.get("/api/sensor-state")
def get_state():
    with db() as con:
        row = con.execute("SELECT sensor_enabled FROM settings WHERE id=1").fetchone()
        return {"enabled": bool(row["sensor_enabled"])}

@app.post("/api/sensor-state")
def set_state():
    body = request.get_json(silent=True) or {}
    enabled = bool(body.get("enabled", True))
    with db() as con:
        con.execute("UPDATE settings SET sensor_enabled=? WHERE id=1", (1 if enabled else 0,))
        con.commit()
    return {"enabled": enabled}

@app.get("/api/light-state")
def light_state():
    """
    Estado atual da luz = último evento em events onde type='light':
      detail='ON'  -> on=True
      detail='OFF' -> on=False
    Se não houver evento, assume False (desligada).
    """
    with db() as con:
        row = con.execute(
            "SELECT detail FROM events WHERE type='light' ORDER BY id DESC LIMIT 1"
        ).fetchone()
    on = False
    if row and row["detail"]:
        on = str(row["detail"]).strip().upper() == "ON"
    return {"on": on}

@app.get("/api/events")
def list_events():
    limit = int(request.args.get("limit", 100))
    with db() as con:
        items = con.execute(
            "SELECT id, ts, device_id, type, detail FROM events ORDER BY id DESC LIMIT ?",
            (limit,)
        ).fetchall()
        total = con.execute("SELECT COUNT(*) c FROM events").fetchone()["c"]
    return jsonify({
        "total": total,
        "items": [dict(x) for x in items]
    })

@app.post("/api/events")
def add_event():
    body = request.get_json(silent=True) or {}
    ts = body.get("ts") or now_utc_iso()
    device_id = body.get("device_id")
    type_ = (body.get("type") or "motion").strip().lower()
    detail = body.get("detail")
    with db() as con:
        con.execute(
            "INSERT INTO events(ts, device_id, type, detail) VALUES(?,?,?,?)",
            (ts, device_id, type_, detail)
        )
        con.commit()
    return {"ok": True}, 201

@app.post("/api/clear")
def clear_events():
    with db() as con:
        con.execute("DELETE FROM events")
        con.commit()
    return {"ok": True}

if __name__ == "__main__":
    init_db()
    app.run(host="0.0.0.0", port=APP_PORT, debug=True)
