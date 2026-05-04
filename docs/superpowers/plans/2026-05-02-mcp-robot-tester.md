# MCP Robot Tester Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a Python MCP server that lets Claude Code send robot commands to the Mimi robot over WebSocket for testing.

**Architecture:** A FastMCP stdio server (`scripts/mcp_robot_tester/server.py`) exposes 12 command tools. Each tool call opens a WebSocket to the robot at `ws://<MIMI_ROBOT_URL>/ws`, performs MCP handshake (`initialize` → `tools/call`), and returns the robot's response. A separate `robot_client.py` handles the WebSocket + JSON-RPC logic.

**Tech Stack:** Python 3.11+, `mcp[cli]` (FastMCP), `websockets`

---

## File Map

| File | Purpose |
|------|---------|
| `scripts/mcp_robot_tester/robot_client.py` | Async WebSocket client: connect, initialize, call tool, return result |
| `scripts/mcp_robot_tester/server.py` | FastMCP stdio server with 12 command tools |
| `scripts/mcp_robot_tester/requirements.txt` | Python dependencies |

---

## Command → Robot Action Mapping

| Tool name | Robot call |
|-----------|-----------|
| `walk_forward` | `self.mimi.action` `{action:"walk", direction:1, steps:3, speed:700}` |
| `turn_left` | `self.mimi.action` `{action:"turn", direction:1, steps:3, speed:700}` |
| `jump` | `self.mimi.action` `{action:"jump", steps:3, speed:700}` |
| `moonwalk` | `self.mimi.action` `{action:"moonwalk", steps:3, speed:700}` |
| `showcase` | `self.mimi.action` `{action:"showcase"}` |
| `wave` | `self.mimi.action` `{action:"hand_wave"}` |
| `hands_up` | `self.mimi.action` `{action:"hands_up"}` |
| `windmill` | `self.mimi.action` `{action:"windmill", steps:3, speed:700}` |
| `takeoff` | `self.mimi.action` `{action:"takeoff", steps:3, speed:700}` |
| `radio_calisthenics` | `self.mimi.action` `{action:"radio_calisthenics"}` |
| `magic_circle` | `self.mimi.action` `{action:"magic_circle"}` |
| `stop` | `self.mimi.stop` `{}` |

---

### Task 1: Create requirements.txt

**Files:**
- Create: `scripts/mcp_robot_tester/requirements.txt`

- [ ] **Step 1: Write requirements.txt**

```
mcp[cli]>=1.0.0
websockets>=12.0
```

- [ ] **Step 2: Verify pip can resolve the packages**

Run: `pip install -r scripts/mcp_robot_tester/requirements.txt --dry-run 2>&1 | tail -5`
Expected: No errors (or "Would install" lines)

- [ ] **Step 3: Commit**

```bash
git add scripts/mcp_robot_tester/requirements.txt
git commit -m "feat: add mcp robot tester requirements"
```

---

### Task 2: Create robot_client.py

**Files:**
- Create: `scripts/mcp_robot_tester/robot_client.py`

- [ ] **Step 1: Write robot_client.py**

```python
"""WebSocket client that connects to the Mimi robot's MCP server."""
import asyncio
import json
import websockets

_request_id = 0

def _next_id() -> int:
    global _request_id
    _request_id += 1
    return _request_id


async def call_robot_tool(robot_url: str, tool_name: str, arguments: dict) -> str:
    """Connect to the robot, initialize MCP, call a tool, return the result text."""
    async with websockets.connect(robot_url, open_timeout=5) as ws:
        # Initialize MCP session
        init_req = {
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {"capabilities": {}},
            "id": _next_id(),
        }
        await ws.send(json.dumps(init_req))
        raw = await asyncio.wait_for(ws.recv(), timeout=5)
        init_resp = json.loads(raw)
        if "error" in init_resp:
            raise RuntimeError(f"MCP initialize failed: {init_resp['error']}")

        # Call the tool
        call_req = {
            "jsonrpc": "2.0",
            "method": "tools/call",
            "params": {"name": tool_name, "arguments": arguments},
            "id": _next_id(),
        }
        await ws.send(json.dumps(call_req))
        raw = await asyncio.wait_for(ws.recv(), timeout=10)
        call_resp = json.loads(raw)

    if "error" in call_resp:
        return f"Error: {call_resp['error'].get('message', str(call_resp['error']))}"

    result = call_resp.get("result", {})
    content = result.get("content", [])
    if content and isinstance(content, list):
        return content[0].get("text", "ok")
    return "ok"
```

- [ ] **Step 2: Run a syntax check**

Run: `python -m py_compile scripts/mcp_robot_tester/robot_client.py && echo "OK"`
Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add scripts/mcp_robot_tester/robot_client.py
git commit -m "feat: add robot WebSocket MCP client"
```

---

### Task 3: Create server.py

**Files:**
- Create: `scripts/mcp_robot_tester/server.py`

- [ ] **Step 1: Write server.py**

```python
"""FastMCP stdio server exposing Mimi robot commands as Claude Code tools."""
import asyncio
import os
from mcp.server.fastmcp import FastMCP
from robot_client import call_robot_tool

ROBOT_URL = os.environ.get("MIMI_ROBOT_URL", "ws://192.168.4.1/ws")

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
```

- [ ] **Step 2: Run a syntax check**

Run: `python -m py_compile scripts/mcp_robot_tester/server.py && echo "OK"`
Expected: `OK`

- [ ] **Step 3: Verify FastMCP lists 12 tools**

```bash
cd scripts/mcp_robot_tester && pip install -r requirements.txt -q && \
  python -c "
import server
import asyncio

async def check():
    tools = await server.mcp.list_tools()
    print(f'Tools registered: {len(tools)}')
    for t in tools:
        print(' -', t.name)

asyncio.run(check())
"
```

Expected output (12 lines after "Tools registered: 12"):
```
Tools registered: 12
 - walk_forward
 - turn_left
 - jump
 - moonwalk
 - showcase
 - wave
 - hands_up
 - windmill
 - takeoff
 - radio_calisthenics
 - magic_circle
 - stop
```

- [ ] **Step 4: Commit**

```bash
git add scripts/mcp_robot_tester/server.py
git commit -m "feat: add mcp robot tester FastMCP server with 12 command tools"
```

---

### Task 4: Live robot test

This task is manual and requires the robot to be powered on and connected to Wi-Fi.

- [ ] **Step 1: Find the robot's IP address**

Check the robot's display or your router's DHCP table. The default hotspot address is `192.168.4.1`. Export it:

```bash
export MIMI_ROBOT_URL="ws://192.168.4.1/ws"
```

- [ ] **Step 2: Test `stop` (safe first command)**

```bash
cd scripts/mcp_robot_tester
python -c "
import asyncio, os
from robot_client import call_robot_tool
url = os.environ.get('MIMI_ROBOT_URL', 'ws://192.168.4.1/ws')
result = asyncio.run(call_robot_tool(url, 'self.mimi.stop', {}))
print('stop:', result)
"
```

Expected: `stop: true` (robot returns to home position)

- [ ] **Step 3: Test `walk_forward`**

```bash
python -c "
import asyncio, os
from robot_client import call_robot_tool
url = os.environ.get('MIMI_ROBOT_URL', 'ws://192.168.4.1/ws')
result = asyncio.run(call_robot_tool(url, 'self.mimi.action', {'action':'walk','direction':1,'steps':3,'speed':700}))
print('walk_forward:', result)
"
```

Expected: `walk_forward: true`

- [ ] **Step 4: Add as Claude Code MCP server**

Add to your Claude Code config (`~/.claude/mcp.json` or `.claude/settings.json`):

```json
{
  "mcpServers": {
    "mimi-robot-tester": {
      "command": "python",
      "args": ["scripts/mcp_robot_tester/server.py"],
      "cwd": "/path/to/xiaozhi-esp32-clean",
      "env": {
        "MIMI_ROBOT_URL": "ws://192.168.4.1/ws"
      }
    }
  }
}
```

- [ ] **Step 5: Verify in Claude Code**

Restart Claude Code. Confirm 12 tools from `mimi-robot-tester` appear in tool list. Call `stop` from Claude Code to confirm end-to-end flow.
