#!/usr/bin/env python3
"""Fire MCP online music, radio, and audio story commands one by one."""
import asyncio
import os
import sys
import time

from robot_client import call_robot_tool

ROBOT_URL = os.environ.get("MIMI_ROBOT_URL", "ws://192.168.1.179:8080/ws")
PAUSE = float(os.environ.get("PAUSE_SECONDS", "8"))

MUSIC_URL = os.environ.get(
    "MUSIC_URL",
    "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3",
)
RADIO_URL = os.environ.get(
    "RADIO_URL",
    "https://icecast.radiofrance.fr/fip-midfi.mp3",
)
STORY_URL = os.environ.get(
    "STORY_URL",
    "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-2.mp3",
)

COMMANDS = [
    ("display_spectrum", "self.music.set_display_mode", {"mode": "spectrum"}),
    ("music_simple", "self.music.play_url", {"url": MUSIC_URL}),
    (
        "typed_music",
        "self.audio.play_url",
        {
            "url": MUSIC_URL,
            "type": "music",
            "title": "MCP music test",
            "artist": "SoundHelix",
            "source_name": "mcp_robot_tester",
            "lyric_url": "",
            "duration_ms": 0,
            "decoder": "auto",
        },
    ),
    (
        "radio",
        "self.audio.play_url",
        {
            "url": RADIO_URL,
            "type": "radio",
            "title": "MCP radio test",
            "artist": "",
            "source_name": "FIP",
            "lyric_url": "",
            "duration_ms": 0,
            "decoder": "auto",
        },
    ),
    (
        "story_wrapper",
        "self.audio_story.play_url",
        {
            "url": STORY_URL,
            "title": "MCP story wrapper test",
            "source_name": "mcp_robot_tester",
            "duration_ms": 0,
            "decoder": "auto",
        },
    ),
    (
        "story_typed",
        "self.audio.play_url",
        {
            "url": STORY_URL,
            "type": "audio_story",
            "title": "MCP typed story test",
            "artist": "",
            "source_name": "mcp_robot_tester",
            "lyric_url": "",
            "duration_ms": 0,
            "decoder": "auto",
        },
    ),
    ("display_lyrics", "self.music.set_display_mode", {"mode": "lyrics"}),
]


async def main():
    print(f"Robot URL : {ROBOT_URL}")
    print(f"Pause     : {PAUSE}s between commands")
    print(f"Music URL : {MUSIC_URL}")
    print(f"Radio URL : {RADIO_URL}")
    print(f"Story URL : {STORY_URL}")
    print(f"Commands  : {len(COMMANDS)}")
    print("-" * 72)

    passed = 0
    failed = 0

    for i, (label, tool, args) in enumerate(COMMANDS, 1):
        print(f"[{i:02d}/{len(COMMANDS)}] {label} ({tool}) ... ", end="", flush=True)
        t0 = time.monotonic()
        try:
            result = await call_robot_tool(ROBOT_URL, tool, args)
            elapsed = time.monotonic() - t0
            print(f"{result}  ({elapsed:.2f}s)")
            passed += 1
        except Exception as exc:
            elapsed = time.monotonic() - t0
            print(f"ERROR: {exc}  ({elapsed:.2f}s)")
            failed += 1

        if i < len(COMMANDS):
            await asyncio.sleep(PAUSE)

    print("-" * 72)
    print(f"Done. {passed} sent, {failed} errors.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
