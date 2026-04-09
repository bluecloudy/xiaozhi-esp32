import argparse
import json
import pathlib
import re
import sys
from typing import Any, Dict


_FRONTMATTER_RE = re.compile(r"\A---\n(.*?)\n---\n", re.DOTALL)
_POLICY_DATA_RE = re.compile(
    r"##\s+policy_data\s*\n```json\n(.*?)\n```", re.DOTALL | re.IGNORECASE
)


def parse_frontmatter(markdown: str) -> Dict[str, str]:
    match = _FRONTMATTER_RE.search(markdown)
    if not match:
        raise ValueError("frontmatter required")

    frontmatter: Dict[str, str] = {}
    for raw_line in match.group(1).splitlines():
        line = raw_line.strip()
        if not line or ":" not in line:
            continue
        key, value = line.split(":", 1)
        frontmatter[key.strip()] = value.strip()
    return frontmatter


def extract_policy_data(markdown: str) -> Dict[str, Any]:
    match = _POLICY_DATA_RE.search(markdown)
    if not match:
        raise ValueError("policy_data json block required")
    try:
        return json.loads(match.group(1))
    except json.JSONDecodeError as exc:
        raise ValueError(f"policy_data parse error: {exc}") from exc


def _require_fields(container: Dict[str, Any], required: list, name: str) -> None:
    for key in required:
        if key not in container:
            raise ValueError(f"missing {name}: {key}")


def validate_contract(contract_path: pathlib.Path, schema_path: pathlib.Path) -> None:
    text = contract_path.read_text(encoding="utf-8")
    schema = json.loads(schema_path.read_text(encoding="utf-8"))

    frontmatter = parse_frontmatter(text)
    _require_fields(frontmatter, schema["required_frontmatter"], "frontmatter key")

    if frontmatter["schema"] != schema["schema_name"]:
        raise ValueError("unknown schema version")
    if frontmatter["mode"] != "deterministic":
        raise ValueError("mode must be deterministic")

    policy_data = extract_policy_data(text)
    name_upper = frontmatter["name"].upper()

    if name_upper == "SOUL":
        _require_fields(policy_data, schema["required_top_level"], "policy_data key")
        _require_fields(policy_data["stt"], schema["required_stt_fields"], "stt field")
        _require_fields(
            policy_data["session"], schema["required_session_fields"], "session field"
        )
    elif name_upper == "AGENTS":
        _require_fields(policy_data, schema["required_agents_fields"], "policy_data key")
        _require_fields(policy_data["intent"], schema["required_intent_fields"], "intent field")
    else:
        raise ValueError("unsupported contract name")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--file", required=True)
    parser.add_argument("--schema", required=True)
    args = parser.parse_args()

    try:
        validate_contract(pathlib.Path(args.file), pathlib.Path(args.schema))
    except Exception as exc:
        sys.stderr.write(f"{exc}\n")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
