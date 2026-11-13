"""Portable regression guard; Windows behavior lives in test_vcvars_import.ps1."""
from pathlib import Path
import unittest


class VcvarsCommandTest(unittest.TestCase):
    def test_no_legacy_nested_quotes_for_spaced_path(self):
        source = Path(__file__).with_name('build.ps1').read_text()
        setup = r'C:\Program Files\Test Toolchain\vcvars64.bat'
        old_command = '""{0}" >nul && set"'.format(setup)
        print('Legacy command for spaced path:', old_command, flush=True)
        self.assertNotIn("('" + '""{0}" >nul && set"' + "' -f $taskSetup)", source,
                         'PS 5.1 legacy quoting remains: cmd fails at \'""C:\\Program\' (Windows reproduction supplied separately)')


if __name__ == '__main__':
    unittest.main()
