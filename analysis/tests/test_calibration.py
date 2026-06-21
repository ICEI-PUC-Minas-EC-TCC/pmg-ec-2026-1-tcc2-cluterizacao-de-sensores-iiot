import inspect
from analysis import calibration, run
from analysis.calibration import parse_logs, RoleStats


def test_constantes_sao_floats():
    assert isinstance(calibration.LEADER_MA, float)
    assert isinstance(calibration.MEMBER_MA, float)
    assert isinstance(calibration.IDLE_MA, float)


def test_run_usa_as_constantes_centrais():
    for fn in (run.generate, run.hetero_frames):
        sig = inspect.signature(fn)
        assert sig.parameters["leader_ma"].default == calibration.LEADER_MA
        assert sig.parameters["member_ma"].default == calibration.MEMBER_MA
        assert sig.parameters["idle_ma"].default == calibration.IDLE_MA


def test_parse_logs_calib_agrupa_por_papel(tmp_path):
    log = tmp_path / "ttyUSB0.log"
    log.write_text(
        "I (3050) CALIB: role=IDLE I=8.0mA bat=100.0%\n"
        "I (4050) CALIB: role=LEADER I=120.0mA bat=99.9%\n"
        "I (5050) CALIB: role=LEADER I=122.0mA bat=99.8%\n"
        "I (6050) CALIB: role=MEMBER I=24.0mA bat=99.7%\n"
    )
    stats = parse_logs([log])
    assert isinstance(stats["LEADER"], RoleStats)
    assert stats["LEADER"].count == 2
    assert stats["LEADER"].mean_ma == 121.0
    assert stats["MEMBER"].mean_ma == 24.0
    assert stats["IDLE"].mean_ma == 8.0


def test_parse_logs_fallback_sem_calib(tmp_path):
    log = tmp_path / "ttyUSB1.log"
    log.write_text(
        "I (3000) AMMETER_INA219: I=8.0mA V=3.7V P=29mW Rem=100.0%\n"
        "I (5000) ROLE_SERVICE: Role: LEADER (aa)\n"
        "I (6000) AMMETER_INA219: I=120.0mA V=3.7V P=444mW Rem=99.9%\n"
        "I (7000) ROLE_SERVICE: Role: MEMBER (new leader: bb)\n"
        "I (8000) AMMETER_INA219: I=24.0mA V=3.7V P=88mW Rem=99.8%\n"
    )
    stats = parse_logs([log])
    assert stats["IDLE"].count == 1 and stats["IDLE"].mean_ma == 8.0
    assert stats["LEADER"].mean_ma == 120.0
    assert stats["MEMBER"].mean_ma == 24.0


def test_parse_logs_prefere_calib_sem_duplicar(tmp_path):
    log = tmp_path / "ttyUSB2.log"
    log.write_text(
        "I (6000) AMMETER_INA219: I=120.0mA V=3.7V P=444mW Rem=99.9%\n"
        "I (6005) CALIB: role=LEADER I=120.0mA bat=99.9%\n"
    )
    stats = parse_logs([log])
    assert stats["LEADER"].count == 1
    assert "IDLE" not in stats


def test_settle_descarta_transiente_pos_troca(tmp_path):
    log = tmp_path / "ttyUSB0.log"
    log.write_text(
        "I (5000) CALIB: role=LEADER I=200.0mA bat=99.9%\n"  # pico no instante da troca
        "I (9000) CALIB: role=LEADER I=120.0mA bat=99.8%\n"  # já acomodado (>=4000ms)
    )
    stats = parse_logs([log], settle_ms=4000)
    assert stats["LEADER"].count == 1
    assert stats["LEADER"].mean_ma == 120.0


def test_settle_zero_mantem_tudo(tmp_path):
    log = tmp_path / "ttyUSB0.log"
    log.write_text(
        "I (5000) CALIB: role=LEADER I=200.0mA bat=99.9%\n"
        "I (9000) CALIB: role=LEADER I=120.0mA bat=99.8%\n"
    )
    stats = parse_logs([log], settle_ms=0)
    assert stats["LEADER"].count == 2


def test_cli_imprime_pronto_para_colar(tmp_path, capsys):
    (tmp_path / "ttyUSB0.log").write_text(
        "I (4050) CALIB: role=LEADER I=120.0mA bat=99.9%\n"
        "I (5050) CALIB: role=LEADER I=120.0mA bat=99.9%\n"
        "I (6050) CALIB: role=MEMBER I=24.0mA bat=99.7%\n"
        "I (7050) CALIB: role=IDLE I=8.0mA bat=100.0%\n"
    )
    from analysis.calibration import main
    rc = main([str(tmp_path), "--settle-ms", "0"])
    out = capsys.readouterr().out
    assert rc == 0
    assert "LEADER_MA = 120.0" in out
    assert "MEMBER_MA = 24.0" in out
    assert "IDLE_MA = 8.0" in out


def test_cli_sem_logs_retorna_erro(tmp_path, capsys):
    from analysis.calibration import main
    rc = main([str(tmp_path)])
    assert rc == 1
    assert "Nenhum log" in capsys.readouterr().out
