"""Regression tests for the live connection fixture's environment ownership."""

import os
from unittest.mock import Mock

import pytest

import conftest


@pytest.mark.parametrize("project_dir", [None, "/caller/HostProject"])
@pytest.mark.parametrize("failure", [None, "construct", "connect"])
def test_tcp_fixture_preserves_project_environment(monkeypatch, project_dir, failure):
    if project_dir is None:
        monkeypatch.delenv("CORTEX_PROJECT_DIR", raising=False)
    else:
        monkeypatch.setenv("CORTEX_PROJECT_DIR", project_dir)

    observed = []
    connection = Mock()

    def construct():
        observed.append(os.environ.get("CORTEX_PROJECT_DIR"))
        if failure == "construct":
            raise ConnectionError("fixture connection failed")
        return connection

    if failure == "connect":
        connection.connect.side_effect = ConnectionError("fixture connection failed")
    monkeypatch.setattr(conftest, "UEConnection", construct)
    fixture = conftest.tcp_connection.__wrapped__()
    if failure:
        with pytest.raises(ConnectionError, match="fixture connection failed"):
            next(fixture)
    else:
        assert next(fixture) is connection
        actual_project_dir = os.environ.get("CORTEX_PROJECT_DIR")
        assert actual_project_dir == project_dir
        fixture.close()
        connection.disconnect.assert_called_once()

    actual_project_dir = os.environ.get("CORTEX_PROJECT_DIR")
    assert actual_project_dir == project_dir
    assert observed == [project_dir if project_dir is not None else str(conftest._PROJECT_ROOT)]
