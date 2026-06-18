import inspect
from analysis import calibration, run


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
