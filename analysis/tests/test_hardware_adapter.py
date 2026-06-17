import sqlite3
from analysis.adapters import hardware
from analysis import contract

def _make_db(path):
    conn = sqlite3.connect(path); c = conn.cursor()
    c.execute("""CREATE TABLE leituras (id INTEGER PRIMARY KEY AUTOINCREMENT,
                 topico TEXT, temperatura REAL, corrente_ma REAL, bateria_pct REAL,
                 measured_time TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)""")
    c.executemany("INSERT INTO leituras (topico,temperatura,corrente_ma,bateria_pct,timestamp) VALUES (?,?,?,?,?)",
                  [("/tcc/main/aa", 25.0, 120.0, 90.0, "2026-06-17 10:00:00"),
                   ("/tcc/main/aa", 25.0, 118.0, 80.0, "2026-06-17 10:00:02"),
                   ("/tcc/main/bb", 25.0, 24.0, 95.0, "2026-06-17 10:00:02")])
    conn.commit(); conn.close()

def test_load_maps_to_contract(tmp_path):
    db = str(tmp_path / "s.db"); _make_db(db)
    df = hardware.load(db)
    contract.validate(df)
    assert set(df["source"]) == {"hw"}
    assert set(df["event"]) == {"sample"}
    assert df["t_ms"].min() == 0
    assert set(df["node_id"]) == {"/tcc/main/aa", "/tcc/main/bb"}
