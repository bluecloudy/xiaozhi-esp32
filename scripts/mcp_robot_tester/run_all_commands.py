#!/usr/bin/env python3
"""Fire every MCP robot command one by one with a pause between each."""
import asyncio
import os
import sys
import time

from robot_client import call_robot_tool

ROBOT_URL = os.environ.get("MIMI_ROBOT_URL", "ws://192.168.1.179:8080/ws")
PAUSE = float(os.environ.get("PAUSE_SECONDS", "4"))

COMMANDS = [
    ("stop",              "self.mimi.stop",   {}),
    ("walk_forward",      "self.mimi.action", {"action": "walk",              "direction": 1, "steps": 3, "speed": 700}),
    ("turn_left",         "self.mimi.action", {"action": "turn",              "direction": 1, "steps": 3, "speed": 700}),
    ("jump",              "self.mimi.action", {"action": "jump",                              "steps": 3, "speed": 700}),
    ("moonwalk",          "self.mimi.action", {"action": "moonwalk",                          "steps": 3, "speed": 700}),
    ("wave",              "self.mimi.action", {"action": "hand_wave"}),
    ("hands_up",          "self.mimi.action", {"action": "hands_up"}),
    ("windmill",          "self.mimi.action", {"action": "windmill",                          "steps": 3, "speed": 700}),
    ("takeoff",           "self.mimi.action", {"action": "takeoff",                           "steps": 3, "speed": 700}),
    ("radio_calisthenics","self.mimi.action", {"action": "radio_calisthenics"}),
    ("magic_circle",      "self.mimi.action", {"action": "magic_circle"}),
    ("showcase",          "self.mimi.action", {"action": "showcase"}),
    ("stop (final)",      "self.mimi.stop",   {}),
]


async def main():
    print(f"Robot URL : {ROBOT_URL}")
    print(f"Pause     : {PAUSE}s between commands")
    print(f"Commands  : {len(COMMANDS)}")
    print("-" * 48)

    passed = 0
    failed = 0

    for i, (label, tool, args) in enumerate(COMMANDS, 1):
        print(f"[{i:02d}/{len(COMMANDS)}] {label} ... ", end="", flush=True)
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

    print("-" * 48)
    print(f"Done. {passed} sent, {failed} errors.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
