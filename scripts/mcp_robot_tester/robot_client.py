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
    """Connect to the robot, initialize MCP, call a tool, return the result text.

    The robot firmware executes commands but does not yet route responses back on
    the local WebSocket (port 8080). Responses go through the cloud protocol instead.
    We fire initialize + tools/call and wait briefly; fall back to "sent" on timeout.
    """
    async with websockets.connect(robot_url, open_timeout=5) as ws:
        # Send initialize (fire-and-forget — local WS response routing not yet implemented)
        init_req = {
            "jsonrpc": "2.0",
            "method": "initialize",
            "params": {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "mcp-robot-tester", "version": "1.0"},
            },
            "id": _next_id(),
        }
        await ws.send(json.dumps(init_req))

        # Call the tool
        call_req = {
            "jsonrpc": "2.0",
            "method": "tools/call",
            "params": {"name": tool_name, "arguments": arguments},
            "id": _next_id(),
        }
        await ws.send(json.dumps(call_req))

        # Wait for response; if none comes, the command still executed on the robot
        try:
            raw = await asyncio.wait_for(ws.recv(), timeout=5)
            call_resp = json.loads(raw)
        except asyncio.TimeoutError:
            return "sent"

    if "error" in call_resp:
        return f"Error: {call_resp['error'].get('message', str(call_resp['error']))}"

    result = call_resp.get("result", {})
    content = result.get("content", [])
    if content and isinstance(content, list):
        return content[0].get("text", "ok")
    return "ok"
