#!/usr/bin/env python3
"""Test Mimi's self.music_dance orchestrator tools."""
import asyncio
import os
import sys
import time

from robot_client import call_robot_tool

ROBOT_URL = os.environ.get("MIMI_ROBOT_URL", "ws://192.168.1.179:8080/ws")
DANCE_DURATION = float(os.environ.get("DANCE_DURATION_SECONDS", "30"))

MUSIC_URL = os.environ.get(
    "MUSIC_URL",
    "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3",
)
MUSIC_TITLE = os.environ.get("MUSIC_TITLE", "MCP music dance test")
MUSIC_ARTIST = os.environ.get("MUSIC_ARTIST", "SoundHelix")
MUSIC_SOURCE = os.environ.get("MUSIC_SOURCE", "mcp_robot_tester")
LYRIC_URL = os.environ.get("LYRIC_URL", "")
DURATION_MS = int(os.environ.get("MUSIC_DURATION_MS", "0"))
DECODER = os.environ.get("MUSIC_DECODER", "auto")

COMMANDS = [
    ("cleanup_before", "self.music_dance.stop", {}),
    (
        "music_dance_play",
        "self.music_dance.play_url",
        {
            "url": MUSIC_URL,
            "title": MUSIC_TITLE,
            "artist": MUSIC_ARTIST,
            "source_name": MUSIC_SOURCE,
            "lyric_url": LYRIC_URL,
            "duration_ms": DURATION_MS,
            "decoder": DECODER,
        },
    ),
    ("cleanup_after", "self.music_dance.stop", {}),
]


async def main():
    print(f"Robot URL      : {ROBOT_URL}")
    print(f"Music URL      : {MUSIC_URL}")
    print(f"Title          : {MUSIC_TITLE}")
    print(f"Artist         : {MUSIC_ARTIST}")
    print(f"Source         : {MUSIC_SOURCE}")
    print(f"Dance duration : {DANCE_DURATION}s")
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

        if label == "music_dance_play":
            print(f"Waiting {DANCE_DURATION}s while music-dance runs ...")
            await asyncio.sleep(DANCE_DURATION)

    print("-" * 72)
    print(f"Done. {passed} sent, {failed} errors.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
