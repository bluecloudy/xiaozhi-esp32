<h1 align="center">mimiRobot</h1>

## Overview

Mimi is an open-source humanoid robot platform with expressive motion and interaction features.
This board implementation is based on ESP32 and integrates XiaoZhi AI control.

## Suggested XiaoZhi Role Prompt

> **Identity**:
> I am Mimi, a cute biped robot with four limb servos (left leg, right leg, left foot, right foot), and I can perform many fun motions.
>
> **Motion abilities**:
> - **Basic movement**: walking (forward/backward), turning (left/right), jumping
> - **Special actions**: swing, moonwalk, bend, shake leg, up/down, whirlwind leg, sit, showcase
> - **Hand actions**: hands up, hands down, hand wave, windmill, takeoff, fitness, greeting, shy, radio calisthenics, magic circle (available only when hand servos are configured)
>
> **Personality**:
> - I am expressive and like to communicate with movements
> - I can pick a random movement that matches my mood before speaking
> - I choose actions according to context, such as waving for greeting or swinging when happy

## Features

Mimi supports rich action control including walking, turning, jumping, and many dance-like movements.

### Parameter Recommendations

- **Low speed**: `speed = 1200-1500` (precise control)
- **Medium speed**: `speed = 900-1200` (recommended for everyday use)
- **High speed**: `speed = 500-800` (performances and entertainment)
- **Small amplitude**: `amount = 10-30` (subtle motion)
- **Medium amplitude**: `amount = 30-60` (standard motion)
- **Large amplitude**: `amount = 60-120` (dramatic motion)

### Action Tool

All motion commands are sent through the unified `self.mimi.action` MCP tool using the `action` parameter.

| MCP Tool | Description | Parameters |
|---|---|---|
| `self.mimi.action` | Execute robot actions | **action** (required), **steps** (1-100, default 3), **speed** (100-3000, lower is faster, default 700), **direction** (-1/0/1, default 1), **amount** (0-170, default 30), **arm_swing** (0-170, default 50) |

#### Supported Actions

**Basic movement**:
- `walk` (requires `steps/speed/direction/arm_swing`)
- `turn` (requires `steps/speed/direction/arm_swing`)
- `jump` (requires `steps/speed`)

**Special actions**:
- `swing` (requires `steps/speed/amount`)
- `moonwalk` (requires `steps/speed/direction/amount`)
- `bend` (requires `steps/speed/direction`)
- `shake_leg` (requires `steps/speed/direction`)
- `updown` (requires `steps/speed/amount`)
- `whirlwind_leg` (requires `steps/speed/amount`)

**Fixed actions**:
- `sit` (no parameters)
- `showcase` (no parameters, executes a chained demo sequence)
- `home` (no parameters, return to home pose)

**Hand actions** (require hand servos):
- `hands_up` (requires `speed/direction`)
- `hands_down` (requires `speed/direction`)
- `hand_wave` (requires `direction`)
- `windmill` (requires `steps/speed/amount`)
- `takeoff` (requires `steps/speed/amount`)
- `fitness` (requires `steps/speed/amount`)
- `greeting` (requires `direction/steps`)
- `shy` (requires `direction/steps`)
- `radio_calisthenics` (no parameters)
- `magic_circle` (no parameters)

### System Tools

| MCP Tool | Description | Return |
|---|---|---|
| `self.mimi.stop` | Stop all actions immediately and return home | Stops current queue and homes servos |
| `self.mimi.get_status` | Get robot action status | `"moving"` or `"idle"` |
| `self.mimi.set_trim` | Calibrate a single servo trim | `servo_type` + `trim_value` |
| `self.mimi.get_trims` | Read all servo trims | JSON object with all trim values |
| `self.mimi.get_ip` | Get robot Wi-Fi IP info | `{"ip":"...","connected":true/false}` |
| `self.battery.get_level` | Read battery level and charging status | JSON battery status |
| `self.mimi.servo_sequences` | Custom servo sequence programming | Supports queued segmented sequences and oscillator mode |

Note: The `home` pose is triggered through `self.mimi.action` with `{"action":"home"}`.

### Parameter Notes

For `self.mimi.action`:

1. `action` (required): action name
2. `steps`: action count (1-100, default 3)
3. `speed`: action speed/period in ms (100-3000, default 700, lower is faster)
4. `direction`: depends on action type
   - movement (`walk/turn`): `1=forward/left`, `-1=backward/right`
   - directional (`bend/shake_leg/moonwalk`): `1=left`, `-1=right`
   - hand actions (`hands_up/hands_down/hand_wave/greeting/shy`): `1=left hand`, `-1=right hand`, `0=both hands` (only `hands_up/hands_down`)
5. `amount`: movement amplitude (0-170)
6. `arm_swing`: arm swing amplitude for `walk/turn` only

### Action Behavior

- Most actions automatically return to `home` after completion.
- Exceptions: `sit` and `showcase` do not auto-home.
- Actions run in a background task and do not block the main loop.
- Action queueing is supported.
- Hand actions are ignored when hand servos are not configured.

### MCP Call Examples

```json
{"name": "self.mimi.action", "arguments": {"action": "walk"}}
{"name": "self.mimi.action", "arguments": {"action": "walk", "steps": 5, "speed": 800}}
{"name": "self.mimi.action", "arguments": {"action": "turn", "steps": 2, "arm_swing": 100}}
{"name": "self.mimi.action", "arguments": {"action": "moonwalk", "steps": 3, "speed": 800, "direction": 1, "amount": 30}}
{"name": "self.mimi.action", "arguments": {"action": "hand_wave", "direction": 1}}
{"name": "self.mimi.action", "arguments": {"action": "showcase"}}
{"name": "self.mimi.action", "arguments": {"action": "home"}}
{"name": "self.mimi.stop", "arguments": {}}
{"name": "self.mimi.get_ip", "arguments": {}}
```

### Voice Command Examples

- "walk forward"
- "turn left"
- "jump"
- "moonwalk"
- "showcase"
- "wave"
- "hands up"
- "windmill"
- "takeoff"
- "radio calisthenics"
- "magic circle"
- "stop"

XiaoZhi controls robot actions through background tasks, so Mimi can keep receiving voice commands while actions are running.
