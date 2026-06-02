"""FastMCP stdio server exposing Mimi robot commands as Claude Code tools."""
import asyncio
import os
from mcp.server.fastmcp import FastMCP
from robot_client import call_robot_tool

ROBOT_URL = os.environ.get("MIMI_ROBOT_URL", "ws://192.168.1.179:8080/ws")

mcp = FastMCP("mimi-robot-tester")


def _run(tool_name: str, arguments: dict) -> str:
    return asyncio.run(call_robot_tool(ROBOT_URL, tool_name, arguments))


@mcp.tool()
def walk_forward() -> str:
    """Make the robot walk forward."""
    return _run("self.mimi.action", {"action": "walk", "direction": 1, "steps": 3, "speed": 700})


@mcp.tool()
def turn_left() -> str:
    """Make the robot turn left."""
    return _run("self.mimi.action", {"action": "turn", "direction": 1, "steps": 3, "speed": 700})


@mcp.tool()
def jump() -> str:
    """Make the robot jump."""
    return _run("self.mimi.action", {"action": "jump", "steps": 3, "speed": 700})


@mcp.tool()
def moonwalk() -> str:
    """Make the robot do a moonwalk."""
    return _run("self.mimi.action", {"action": "moonwalk", "steps": 3, "speed": 700})


@mcp.tool()
def showcase() -> str:
    """Make the robot perform a showcase routine."""
    return _run("self.mimi.action", {"action": "showcase"})


@mcp.tool()
def wave() -> str:
    """Make the robot wave its hand."""
    return _run("self.mimi.action", {"action": "hand_wave"})


@mcp.tool()
def hands_up() -> str:
    """Make the robot raise both hands."""
    return _run("self.mimi.action", {"action": "hands_up"})


@mcp.tool()
def windmill() -> str:
    """Make the robot do windmill arm spins."""
    return _run("self.mimi.action", {"action": "windmill", "steps": 3, "speed": 700})


@mcp.tool()
def takeoff() -> str:
    """Make the robot do a takeoff arm sequence."""
    return _run("self.mimi.action", {"action": "takeoff", "steps": 3, "speed": 700})


@mcp.tool()
def radio_calisthenics() -> str:
    """Make the robot perform radio calisthenics."""
    return _run("self.mimi.action", {"action": "radio_calisthenics"})


@mcp.tool()
def magic_circle() -> str:
    """Make the robot draw a magic circle with its arms."""
    return _run("self.mimi.action", {"action": "magic_circle"})


@mcp.tool()
def stop() -> str:
    """Stop all robot actions immediately and return to home position."""
    return _run("self.mimi.stop", {})


if __name__ == "__main__":
    mcp.run()
