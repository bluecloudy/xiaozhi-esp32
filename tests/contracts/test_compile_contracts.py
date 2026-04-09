import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPILER = ROOT / "tools" / "contracts" / "compile_contracts.py"
SCHEMA = ROOT / "tools" / "contracts" / "schema_v1.json"
SOUL = ROOT / "contracts" / "SOUL.md"
AGENTS = ROOT / "contracts" / "AGENTS.md"


class ContractCompileTests(unittest.TestCase):
    def test_compiler_generates_policy_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            out_header = pathlib.Path(tmp) / "policy_bundle_v1.h"
            proc = subprocess.run(
                [
                    "python3",
                    str(COMPILER),
                    "--soul",
                    str(SOUL),
                    "--agents",
                    str(AGENTS),
                    "--schema",
                    str(SCHEMA),
                    "--out",
                    str(out_header),
                ],
                text=True,
                capture_output=True,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertTrue(out_header.exists())
            text = out_header.read_text(encoding="utf-8")
            self.assertIn("kSttEvaluationOrder", text)
            self.assertIn("kIntentNames", text)
            self.assertIn("kIntentKeywordCsv", text)

    def test_compiler_is_deterministic(self):
        with tempfile.TemporaryDirectory() as tmp:
            out_one = pathlib.Path(tmp) / "policy_one.h"
            out_two = pathlib.Path(tmp) / "policy_two.h"
            cmd = [
                "python3",
                str(COMPILER),
                "--soul",
                str(SOUL),
                "--agents",
                str(AGENTS),
                "--schema",
                str(SCHEMA),
            ]
            first = subprocess.run(cmd + ["--out", str(out_one)], text=True, capture_output=True)
            second = subprocess.run(cmd + ["--out", str(out_two)], text=True, capture_output=True)
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(
                out_one.read_text(encoding="utf-8"),
                out_two.read_text(encoding="utf-8"),
            )


if __name__ == "__main__":
    unittest.main()
