import sqlite3
from analysis.adapters import hardware
from analysis import contract

def _make_db(path):
    conn = sqlite3.connect(path); c = conn.cursor()
    c.execute("""CREATE TABLE leituras (id INTEGER PRIMARY KEY AUTOINCREMENT,
                 topico TEXT, corrente_ma REAL, bateria_pct REAL,
                 measured_time TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)""")
    c.executemany("INSERT INTO leituras (topico,corrente_ma,bateria_pct,timestamp) VALUES (?,?,?,?)",
                  [("/tcc/main/aa", 120.0, 90.0, "2026-06-17 10:00:00"),
                   ("/tcc/main/aa", 118.0, 80.0, "2026-06-17 10:00:02"),
                   ("/tcc/main/bb", 24.0, 95.0, "2026-06-17 10:00:02")])
    conn.commit(); conn.close()

def test_load_maps_to_contract(tmp_path):
    db = str(tmp_path / "s.db"); _make_db(db)
    df = hardware.load(db)
    contract.validate(df)
    assert set(df["source"]) == {"hw"}
    assert set(df["event"]) == {"sample"}
    assert df["t_ms"].min() == 0
    assert set(df["node_id"]) == {"/tcc/main/aa", "/tcc/main/bb"}
    assert df["t_ms"].max() == 2000          # 10:00:02 está 2000 ms após 10:00:00
    assert set(df["policy"]) == {"unknown"}  # política default p/ dados de hardware

def test_load_empty_table(tmp_path):
    db = str(tmp_path / "empty.db")
    conn = sqlite3.connect(db); c = conn.cursor()
    c.execute("""CREATE TABLE leituras (id INTEGER PRIMARY KEY AUTOINCREMENT,
                 topico TEXT, corrente_ma REAL, bateria_pct REAL,
                 measured_time TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP)""")
    conn.commit(); conn.close()
    df = hardware.load(db)
    assert list(df.columns) == contract.COLUMNS
    assert len(df) == 0
    contract.validate(df)
