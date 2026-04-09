import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / "tools" / "contracts" / "validate_contracts.py"
SCHEMA = ROOT / "tools" / "contracts" / "schema_v1.json"


class ContractValidationTests(unittest.TestCase):
    def run_validator(self, content: str) -> subprocess.CompletedProcess:
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "SOUL.md"
            path.write_text(content, encoding="utf-8")
            return subprocess.run(
                ["python3", str(VALIDATOR), "--file", str(path), "--schema", str(SCHEMA)],
                text=True,
                capture_output=True,
            )

    def test_rejects_prose_only_contract(self):
        proc = self.run_validator("# SOUL\n\nprose only")
        self.assertNotEqual(proc.returncode, 0)

    def test_rejects_missing_policy_data_block(self):
        text = "---\nname: SOUL\nversion: 1\nschema: soul-policy-v1\nmode: deterministic\n---\n\n## policy_data\n"
        proc = self.run_validator(text)
        self.assertNotEqual(proc.returncode, 0)

    def test_rejects_unknown_schema_version(self):
        text = (
            "---\n"
            "name: SOUL\n"
            "version: 1\n"
            "schema: soul-policy-v2\n"
            "mode: deterministic\n"
            "---\n\n"
            "## policy_data\n"
            "```json\n"
            "{\"stt\": {}, \"session\": {}}\n"
            "```\n"
        )
        proc = self.run_validator(text)
        self.assertNotEqual(proc.returncode, 0)

    def test_accepts_valid_soul_contract(self):
        text = (
            "---\n"
            "name: SOUL\n"
            "version: 1\n"
            "schema: soul-policy-v1\n"
            "mode: deterministic\n"
            "---\n\n"
            "## policy_data\n"
            "```json\n"
            "{\n"
            "  \"stt\": {\n"
            "    \"evaluation_order\": [\"empty_input\"],\n"
            "    \"rules\": {\n"
            "      \"empty_input\": {\n"
            "        \"decision\": {\n"
            "          \"action\": \"reprompt\",\n"
            "          \"reason\": \"NO_INPUT\",\n"
            "          \"reprompt_key\": \"TRY_AGAIN_SIMPLE\"\n"
            "        }\n"
            "      }\n"
            "    }\n"
            "  },\n"
            "  \"session\": {\n"
            "    \"listening_timeout\": {\n"
            "      \"source\": \"config_handoff\",\n"
            "      \"config_key\": \"voice.listening_timeout_ticks\"\n"
            "    },\n"
            "    \"timeout_decision\": {\n"
            "      \"action\": \"reprompt\",\n"
            "      \"reason\": \"LISTEN_TIMEOUT\",\n"
            "      \"reprompt_key\": \"TRY_AGAIN_SIMPLE\",\n"
            "      \"reset_clock\": true\n"
            "    }\n"
            "  }\n"
            "}\n"
            "```\n"
        )
        proc = self.run_validator(text)
        self.assertEqual(proc.returncode, 0, proc.stderr)


if __name__ == "__main__":
    unittest.main()
