"""CLI dispatch does not alter argument boundaries or subprocess status."""

import runpy
import sys
from pathlib import Path
from types import SimpleNamespace

import pytest

MAIN = Path(__file__).resolve().parents[2] / "src/python/osrm/__main__.py"


def test_help_lists_prepare(monkeypatch, capsys):
    monkeypatch.setattr(sys, "argv", ["osrm"])
    with pytest.raises(SystemExit) as exit_status:
        runpy.run_path(str(MAIN), run_name="__main__")
    assert exit_status.value.code == 1
    assert "prepare" in capsys.readouterr().out


def test_editable_root_build_is_found(monkeypatch, tmp_path):
    import shutil
    import subprocess

    main = tmp_path / "src/python/osrm/__main__.py"
    main.parent.mkdir(parents=True)
    main.write_text(MAIN.read_text())
    executable = tmp_path / "build/osrm-prepare"
    executable.parent.mkdir()
    executable.touch()
    monkeypatch.setattr(sys, "argv", ["osrm", "prepare", "--help"])
    monkeypatch.setattr(shutil, "which", lambda _: None)
    monkeypatch.setattr("platform.system", lambda: "Linux")
    calls = []

    def run(command, **kwargs):
        calls.append(command)
        return SimpleNamespace(returncode=0)

    monkeypatch.setattr(subprocess, "run", run)
    with pytest.raises(SystemExit) as exit_status:
        runpy.run_path(str(main), run_name="__main__")
    assert exit_status.value.code == 0
    assert calls == [[str(executable), "--help"]]


@pytest.mark.parametrize("system, suffix", [("Linux", ""), ("Windows", ".exe")])
def test_prepare_forwards_arguments_and_exit_status(monkeypatch, system, suffix):
    import platform
    import shutil
    import subprocess

    args = ["--config", "folder with spaces/ø.json", "--threads", "2"]
    monkeypatch.setattr(sys, "argv", ["osrm", "prepare", *args])
    monkeypatch.setattr(platform, "system", lambda: system)
    monkeypatch.setattr(shutil, "which", lambda name: f"/tools folder/{name}")
    calls = []

    def run(command, **kwargs):
        calls.append(command)
        return SimpleNamespace(returncode=7)

    monkeypatch.setattr(subprocess, "run", run)
    with pytest.raises(SystemExit) as exit_status:
        runpy.run_path(str(MAIN), run_name="__main__")
    assert exit_status.value.code == 7
    assert len(calls) == 1
    assert calls[0][0].endswith(f"osrm-prepare{suffix}")
    assert calls[0][1:] == args
