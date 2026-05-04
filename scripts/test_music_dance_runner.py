import importlib.util
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/mcp_robot_tester/run_music_and_dance.py"


def load_runner():
    sys.path.insert(0, str(SCRIPT.parent))
    spec = importlib.util.spec_from_file_location("run_music_and_dance", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_music_and_dance_runner_starts_music_before_dancing():
    module = load_runner()

    labels = [label for label, _, _ in module.COMMANDS]
    tools = [tool for _, tool, _ in module.COMMANDS]
    command_by_label = {label: (tool, args) for label, tool, args in module.COMMANDS}

    assert labels[:3] == ["stop", "display_spectrum", "music"]
    assert command_by_label["music"][0] == "self.audio.play_url"
    assert command_by_label["music"][1]["type"] == "music"
    assert "self.mimi.action" in tools
    assert tools[-1] == "self.mimi.stop"
    assert command_by_label["moonwalk"][1]["action"] == "moonwalk"
    assert command_by_label["windmill"][1]["action"] == "windmill"
    assert command_by_label["showcase"][1]["action"] == "showcase"
    assert module.MUSIC_URL
