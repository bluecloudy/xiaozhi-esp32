# MCP Protocol IoT Control Usage Guide

> This document explains how to implement IoT control for ESP32 devices based on the MCP protocol. For detailed protocol flow, see [`mcp-protocol.md`](./mcp-protocol.md).

## Introduction

MCP (Model Context Protocol) is the recommended next-generation protocol for IoT control. It uses standard JSON-RPC 2.0 between backend and device to discover and invoke "tools", enabling flexible device control.

## Typical Workflow

1. After boot, the device establishes a connection with the backend over a base transport (such as WebSocket/MQTT).
2. The backend initializes the session with MCP method `initialize`.
3. The backend calls `tools/list` to obtain all tools (capabilities) supported by the device and parameter descriptions.
4. The backend calls `tools/call` to invoke specific tools and control the device.

For detailed protocol format and interaction, see [`mcp-protocol.md`](./mcp-protocol.md).

## Device-Side Tool Registration

The device registers tools that can be called by the backend through `McpServer::AddTool`. Common function signature:

```cpp
void AddTool(
    const std::string& name,           // Tool name; should be unique and preferably hierarchical, e.g. self.dog.forward
    const std::string& description,    // Tool description; concise functional summary for LLM understanding
    const PropertyList& properties,    // Input parameter list (can be empty); supported types: bool, int, string
    std::function<ReturnValue(const PropertyList&)> callback // Callback implementation when tool is invoked
);
```
- name: Unique tool identifier. The naming style `module.function` is recommended.
- description: Natural language description for AI/user readability.
- properties: Parameter list. Supported types are bool, int, and string, with optional ranges and default values.
- callback: Actual execution logic when a call request is received. Return value can be bool/int/string.

## Typical Registration Example (ESP-Hi)

```cpp
void InitializeTools() {
    auto& mcp_server = McpServer::GetInstance();
    // Example 1: no parameters, move robot forward
    mcp_server.AddTool("self.dog.forward", "机器人向前移动", PropertyList(), [this](const PropertyList&) -> ReturnValue {
        servo_dog_ctrl_send(DOG_STATE_FORWARD, NULL);
        return true;
    });
    // Example 2: with parameters, set RGB light color
    mcp_server.AddTool("self.light.set_rgb", "设置RGB颜色", PropertyList({
        Property("r", kPropertyTypeInteger, 0, 255),
        Property("g", kPropertyTypeInteger, 0, 255),
        Property("b", kPropertyTypeInteger, 0, 255)
    }), [this](const PropertyList& properties) -> ReturnValue {
        int r = properties["r"].value<int>();
        int g = properties["g"].value<int>();
        int b = properties["b"].value<int>();
        led_on_ = true;
        SetLedColor(r, g, b);
        return true;
    });
}
```

## Common JSON-RPC Tool Call Examples

### 1. Get Tool List
```json
{
  "jsonrpc": "2.0",
  "method": "tools/list",
  "params": { "cursor": "" },
  "id": 1
}
```

### 2. Move Chassis Forward
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.chassis.go_forward",
    "arguments": {}
  },
  "id": 2
}
```

### 3. Switch Light Mode
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.chassis.switch_light_mode",
    "arguments": { "light_mode": 3 }
  },
  "id": 3
}
```

### 4. Flip Camera
```json
{
  "jsonrpc": "2.0",
  "method": "tools/call",
  "params": {
    "name": "self.camera.set_camera_flipped",
    "arguments": {}
  },
  "id": 4
}
```

## Notes
- Tool names, parameters, and return values must match device-side `AddTool` registrations.
- It is recommended that all new projects standardize on MCP for IoT control.
- For detailed protocol and advanced usage, see [`mcp-protocol.md`](./mcp-protocol.md).
