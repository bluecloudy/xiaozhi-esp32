import importlib.util
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/mcp_robot_tester/run_music_radio_story.py"


def load_runner():
    sys.path.insert(0, str(SCRIPT.parent))
    spec = importlib.util.spec_from_file_location("run_music_radio_story", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_music_radio_story_runner_declares_expected_tools_and_overrides():
    module = load_runner()

    tools = [tool for _, tool, _ in module.COMMANDS]
    labels = [label for label, _, _ in module.COMMANDS]
    command_by_label = {label: (tool, args) for label, tool, args in module.COMMANDS}

    assert "self.music.set_display_mode" in tools
    assert "self.music.play_url" in tools
    assert "self.audio.play_url" in tools
    assert "self.audio_story.play_url" in tools
    assert "radio" in labels
    assert "story_wrapper" in labels
    assert command_by_label["typed_music"][1]["type"] == "music"
    assert command_by_label["radio"][1]["type"] == "radio"
    assert command_by_label["story_typed"][1]["type"] == "audio_story"
    assert module.MUSIC_URL
    assert module.RADIO_URL
    assert module.STORY_URL
