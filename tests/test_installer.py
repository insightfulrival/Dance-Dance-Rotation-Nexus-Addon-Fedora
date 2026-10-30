import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('ddr_install', Path(__file__).resolve().parents[1] / 'install.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.game = Path(self.tmp.name) / 'Game'
        self.config = self.game / 'addons/Nexus/AddonConfig.json'
        self.config.parent.mkdir(parents=True)
        (self.game / 'Gw2-64.exe').write_bytes(b'game')
        (self.game / 'd3d11.dll').write_bytes(b'nexus')
        self.other = {'Signature': 7, 'Name': 'Other addon', 'IsLoaded': False, 'custom': 'preserve'}
        self.config.write_text(json.dumps([self.other]))
        self.binary = Path(self.tmp.name) / installer.DLL
        self.binary.write_bytes(b'test-addon')

    def run_install(self):
        return installer.install(self.game, self.binary, check_running=False)

    def test_enable_and_preserve_other_addons(self):
        before = self.config.read_bytes()
        target, backup = self.run_install()
        records = json.loads(self.config.read_text())
        self.assertEqual(records[0], self.other)
        self.assertEqual(records[1]['Signature'], 0x44445250)
        self.assertTrue(records[1]['IsLoaded'])
        self.assertFalse(records[1]['IsDisabledUntilUpdate'])
        self.assertEqual(target.read_bytes(), self.binary.read_bytes())
        self.assertEqual(backup.read_bytes(), before)

    def test_reinstallation_has_one_record_and_binary_backup(self):
        self.run_install()
        self.binary.write_bytes(b'updated-addon')
        target, _ = self.run_install()
        self.assertEqual(len(json.loads(self.config.read_text())), 2)
        self.assertEqual(target.read_bytes(), b'updated-addon')
        self.assertEqual(next(target.parent.glob(installer.DLL + '.before-*.bak')).read_bytes(), b'test-addon')

    def test_collision_changes_nothing(self):
        self.config.write_text(json.dumps([{'Signature': installer.SIGNATURE, 'Name': 'Unexpected'}]))
        before = self.config.read_bytes()
        with self.assertRaises(ValueError):
            self.run_install()
        self.assertEqual(self.config.read_bytes(), before)
        self.assertFalse((self.game / 'addons' / installer.DLL).exists())

    def test_running_game_changes_nothing(self):
        with patch.object(installer, 'game_running', return_value=True):
            with self.assertRaisesRegex(ValueError, 'Close GW2'):
                installer.install(self.game, self.binary)
        self.assertFalse((self.game / 'addons' / installer.DLL).exists())

    def test_running_game_with_allow_running_copies_only_the_dll(self):
        before = self.config.read_bytes()
        with patch.object(installer, 'game_running', return_value=True):
            target, backup = installer.install(self.game, self.binary, allow_running=True)
        self.assertIsNone(backup)
        self.assertEqual(target.read_bytes(), b'test-addon')
        self.assertEqual(self.config.read_bytes(), before)

    def test_missing_nexus_changes_nothing(self):
        (self.game / 'd3d11.dll').unlink()
        with self.assertRaises(ValueError):
            self.run_install()
        self.assertFalse((self.game / 'addons' / installer.DLL).exists())


if __name__ == '__main__':
    unittest.main()
