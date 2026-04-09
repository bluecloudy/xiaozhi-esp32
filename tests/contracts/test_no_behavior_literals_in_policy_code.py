import pathlib
import unittest

POLICY_FILES = [
    pathlib.Path("main/policy/soul_engine.cc"),
    pathlib.Path("main/policy/extension_manager.cc"),
]

BANNED = [
    "TRY_AGAIN_SIMPLE",
    "TRY_AGAIN_WITH_NAME",
    '"stop"',
    '"cancel"',
]


class AntiHardcodeTests(unittest.TestCase):
    def test_no_behavior_literals(self):
        for path in POLICY_FILES:
            text = path.read_text(encoding="utf-8")
            for token in BANNED:
                self.assertNotIn(token, text, f"{token} found in {path}")


if __name__ == "__main__":
    unittest.main()
