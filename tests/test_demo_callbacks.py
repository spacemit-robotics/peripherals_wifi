#!/usr/bin/env python3
# Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
# SPDX-License-Identifier: Apache-2.0
"""Exercise the demo callback option with a fake nmcli, never a real radio."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


DEMO = str(Path(sys.argv.pop(1)).resolve())


class DemoCallbackTests(unittest.TestCase):
    def setUp(self):
        self.work = tempfile.TemporaryDirectory(prefix="wifi-demo-callback-")
        self.addCleanup(self.work.cleanup)
        nmcli = Path(self.work.name) / "nmcli"
        nmcli.write_text(
            "#!/usr/bin/env python3\n"
            "import os, sys\n"
            "args = sys.argv[1:]\n"
            "if args == ['-t', '-f', 'RUNNING', 'general']:\n"
            "    print('running')\n"
            "elif args == ['radio', 'wifi', 'on']:\n"
            "    sys.exit(1 if os.environ.get('WIFI_DEMO_FAIL') == '1' else 0)\n"
            "elif args == ['device', 'wifi', 'connect', 'fixture', 'password', 'fixture']:\n"
            "    pass\n"
            "else:\n"
            "    print('Unexpected nmcli call', args, file=sys.stderr)\n"
            "    sys.exit(9)\n"
        )
        nmcli.chmod(0o755)
        self.env = os.environ.copy()
        self.env['PATH'] = self.work.name + os.pathsep + self.env['PATH']
        self.env.pop('WIFI_DEMO_FAIL', None)

    def invoke(self, *args):
        return subprocess.run([DEMO, *args], env=self.env, capture_output=True,
                              text=True, timeout=15)

    def test_cb_on_emits_device_event(self):
        result = self.invoke('cb', 'on', 'station')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         ['[cb] dev_status=1', 'wifi_on: 0'])

    def test_without_cb_has_no_events(self):
        result = self.invoke('on', 'station')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ['wifi_on: 0'])

    def test_cb_connect_reports_ordered_events(self):
        result = self.invoke('cb', 'connect', 'fixture', 'fixture')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), [
            '[cb] dev_status=1', '[cb] sta_state=4',
            '[cb] sta_event=8', 'wifi_sta_connect: 0'])

    def test_failed_operation_emits_no_success_event(self):
        self.env['WIFI_DEMO_FAIL'] = '1'
        result = self.invoke('cb', 'on', 'station')
        self.assertIn('wifi_on: -1', result.stdout)
        self.assertNotIn('[cb]', result.stdout)

    def test_cb_requires_command(self):
        result = self.invoke('cb')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('[cb]', result.stdout)


if __name__ == '__main__':
    unittest.main()
