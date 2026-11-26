#!/usr/bin/env python3
# Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
# SPDX-License-Identifier: Apache-2.0
"""Exercise the public callback API against a mock nmcli, never a real radio."""
import ctypes as ct
import os
from pathlib import Path
import sys
import tempfile
import unittest


class Data(ct.Union):
    _fields_ = [("value", ct.c_int)]


class Message(ct.Structure):
    _fields_ = [("id", ct.c_int), ("data", Data), ("private_data", ct.c_void_p)]


class Connect(ct.Structure):
    _fields_ = [("ssid", ct.c_char_p), ("password", ct.c_char_p),
                ("bssid", ct.c_uint8 * 6), ("security", ct.c_int),
                ("fast_connect", ct.c_bool)]


CALLBACK = ct.CFUNCTYPE(None, ct.POINTER(Message))
LIBRARY = str(Path(sys.argv.pop(1)).resolve())


class CallbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="wifi-callback-test-")
        executable = Path(cls.work.name) / "nmcli"
        executable.write_text(
            "#!/usr/bin/env python3\n"
            "import os,sys\n"
            "if os.environ.get('WIFI_CALLBACK_FAIL') == '1': sys.exit(1)\n"
            "args = sys.argv[1:]\n"
            "if args == ['-t', '-f', 'RUNNING', 'general']:\n"
            "    print('running')\n"
            "elif args == ['-t', '-f', 'NAME,TYPE,DEVICE', 'connection', 'show', '--active']:\n"
            "    print('fixture:wifi:wlan0')\n"
            "elif args in [['radio', 'wifi', 'on'], ['radio', 'wifi', 'off'],\n"
            "              ['connection', 'down', 'fixture'],\n"
            "              ['-w', '0', 'connection', 'down', 'fixture'],\n"
            "              ['device', 'wifi', 'connect', 'fixture', 'password', 'fixture']]:\n"
            "    pass\n"
            "else:\n"
            "    print('Unexpected nmcli call', args, file=sys.stderr)\n"
            "    sys.exit(9)\n"
        )
        executable.chmod(0o755)
        cls.original_path = os.environ["PATH"]
        os.environ["PATH"] = cls.work.name + os.pathsep + cls.original_path
        cls.lib = ct.CDLL(LIBRARY)
        cls.lib.wifi_register_msg_cb.argtypes = [CALLBACK, ct.c_void_p]
        cls.lib.wifi_register_msg_cb.restype = ct.c_int
        cls.lib.wifi_sta_connect.argtypes = [ct.POINTER(Connect)]

    @classmethod
    def tearDownClass(cls):
        cls.lib.wifi_deinit()
        os.environ["PATH"] = cls.original_path
        os.environ.pop("WIFI_CALLBACK_FAIL", None)
        cls.work.cleanup()

    def setUp(self):
        self.lib.wifi_deinit()
        os.environ.pop("WIFI_CALLBACK_FAIL", None)
        self.events = []
        self.callback = CALLBACK(lambda msg: self.events.append(
            (msg.contents.id, msg.contents.data.value, msg.contents.private_data)))

    def ready(self):
        self.assertEqual(self.lib.wifi_init(), 0)
        self.assertEqual(self.lib.wifi_register_msg_cb(self.callback, 12345), 0)

    def test_requires_initialization(self):
        self.assertEqual(self.lib.wifi_register_msg_cb(self.callback, None), -2)
        self.assertEqual(self.lib.wifi_on(1), -2)
        self.assertEqual(self.events, [])

    def test_device_event_and_context(self):
        self.ready()
        self.assertEqual(self.lib.wifi_on(1), 0)
        self.assertEqual(self.events, [(0, 1, 12345)])
        self.assertEqual(self.lib.wifi_off(1), 0)
        self.assertEqual(self.events, [(0, 1, 12345), (0, 0, 12345)])

    def test_replace_unregister_and_null_context(self):
        self.ready()
        other = []
        cb = CALLBACK(lambda msg: other.append(msg.contents.private_data))
        self.assertEqual(self.lib.wifi_register_msg_cb(cb, None), 0)
        self.assertEqual(self.lib.wifi_on(1), 0)
        self.assertEqual(other, [None])
        self.assertEqual(self.events, [])
        self.assertEqual(self.lib.wifi_register_msg_cb(CALLBACK(), 999), 0)
        self.assertEqual(self.lib.wifi_on(1), 0)
        self.assertEqual(other, [None])

    def test_deinit_discards_old_registration(self):
        self.ready()
        self.assertEqual(self.lib.wifi_deinit(), 0)
        self.assertEqual(self.lib.wifi_init(), 0)
        self.assertEqual(self.lib.wifi_on(1), 0)
        self.assertEqual(self.events, [])

    def test_failure_does_not_emit(self):
        self.ready()
        os.environ["WIFI_CALLBACK_FAIL"] = "1"
        self.assertEqual(self.lib.wifi_on(1), -1)
        param = Connect(ssid=b"fixture", password=b"fixture")
        self.assertEqual(self.lib.wifi_sta_connect(ct.byref(param)), -1)
        self.assertEqual(self.events, [])
        os.environ.pop("WIFI_CALLBACK_FAIL")
        self.assertEqual(self.lib.wifi_on(1), 0)
        self.assertEqual(len(self.events), 1)

    def test_station_event_sequence(self):
        self.ready()
        param = Connect(ssid=b"fixture", password=b"fixture")
        self.assertEqual(self.lib.wifi_sta_connect(ct.byref(param)), 0)
        self.assertEqual(self.events, [(2, 4, 12345), (1, 8, 12345)])
        self.assertEqual(self.lib.wifi_sta_disconnect(), 0)
        self.assertEqual(self.events, [
            (2, 4, 12345), (1, 8, 12345),
            (2, 7, 12345), (1, 9, 12345)])


if __name__ == "__main__":
    unittest.main()
