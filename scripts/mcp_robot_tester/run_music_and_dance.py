#!/usr/bin/env python3
"""Start online music playback, then run Mimi dance commands while it plays."""
import asyncio
import os
import sys
import time

from robot_client import call_robot_tool

ROBOT_URL = os.environ.get("MIMI_ROBOT_URL", "ws://192.168.1.179:8080/ws")
PAUSE = float(os.environ.get("PAUSE_SECONDS", "4"))
STARTUP_PAUSE = float(os.environ.get("MUSIC_STARTUP_SECONDS", "6"))
DANCE_STEPS = int(os.environ.get("DANCE_STEPS", "3"))
DANCE_SPEED = int(os.environ.get("DANCE_SPEED", "700"))
DANCE_AMOUNT = int(os.environ.get("DANCE_AMOUNT", "70"))

MUSIC_URL = os.environ.get(
    "MUSIC_URL",
    "https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3",
)

COMMANDS = [
    ("stop", "self.mimi.stop", {}),
    ("display_spectrum", "self.music.set_display_mode", {"mode": "spectrum"}),
    (
        "music",
        "self.audio.play_url",
        {
            "url": MUSIC_URL,
            "type": "music",
            "title": "MCP dance music test",
            "artist": "SoundHelix",
            "source_name": "mcp_robot_tester",
            "lyric_url": "",
            "duration_ms": 0,
            "decoder": "auto",
        },
    ),
    ("moonwalk", "self.mimi.action", {"action": "moonwalk", "steps": DANCE_STEPS, "speed": DANCE_SPEED}),
    ("hands_up", "self.mimi.action", {"action": "hands_up", "speed": DANCE_SPEED, "direction": 0}),
    ("wave_left", "self.mimi.action", {"action": "hand_wave", "direction": 1}),
    ("wave_right", "self.mimi.action", {"action": "hand_wave", "direction": -1}),
    (
        "windmill",
        "self.mimi.action",
        {"action": "windmill", "steps": DANCE_STEPS, "speed": DANCE_SPEED, "amount": DANCE_AMOUNT},
    ),
    ("takeoff", "self.mimi.action", {"action": "takeoff", "steps": DANCE_STEPS, "speed": DANCE_SPEED}),
    ("radio_calisthenics", "self.mimi.action", {"action": "radio_calisthenics"}),
    ("magic_circle", "self.mimi.action", {"action": "magic_circle"}),
    ("showcase", "self.mimi.action", {"action": "showcase"}),
    ("stop_final", "self.mimi.stop", {}),
]


async def main():
    print(f"Robot URL     : {ROBOT_URL}")
    print(f"Music URL     : {MUSIC_URL}")
    print(f"Pause         : {PAUSE}s between dance commands")
    print(f"Startup pause : {STARTUP_PAUSE}s after starting music")
    print(f"Dance params  : steps={DANCE_STEPS}, speed={DANCE_SPEED}, amount={DANCE_AMOUNT}")
    print(f"Commands      : {len(COMMANDS)}")
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
            delay = STARTUP_PAUSE if label == "music" else PAUSE
            await asyncio.sleep(delay)

    print("-" * 72)
    print(f"Done. {passed} sent, {failed} errors.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
