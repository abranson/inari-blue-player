#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Jolla Mobile Ltd
"""Exercise real package configuration writes under disposable filesystem roots."""
import importlib.util
from importlib.machinery import SourceFileLoader
import os
from pathlib import Path
import tempfile
import unittest

helper = Path(__file__).with_name("manage-config.py")
if not helper.exists():
    helper = Path("/usr/libexec/ngfd-plugin-inari-blue-config")
spec = importlib.util.spec_from_loader("config", SourceFileLoader("config", str(helper)))
config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config)

BASE = b"# Keep this comment\n[general]\nplugins = dbus;mce\nplugins-optional = first;last\nsink-order = gst\n\n[keytypes]\ncustom = value\n"


class ConfigTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / config.CONFIGS[0]
        self.path.parent.mkdir(parents=True)
        self.path.write_bytes(BASE)

    def manage(self, action):
        config.manage(self.root, action)

    def test_install_upgrade_remove_roundtrip(self):
        self.manage("install")
        expected = BASE.replace(b"first;last", b"first;last;inari-blue")
        self.assertEqual(self.path.read_bytes(), expected)
        self.manage("install")
        self.assertEqual(self.path.read_bytes(), expected)
        self.manage("remove")
        self.assertEqual(self.path.read_bytes(), BASE)
        self.manage("remove")
        self.assertFalse((self.root / config.STATE / "managed.json").exists())

    def test_existing_entry_is_not_owned(self):
        original = BASE.replace(b"first;last", b"first;inari-blue;last")
        self.path.write_bytes(original)
        self.manage("install")
        self.manage("remove")
        self.assertEqual(self.path.read_bytes(), original)

    def test_preserves_user_changes_on_remove(self):
        self.manage("install")
        changed = self.path.read_bytes().replace(b";inari-blue", b";inari-blue;third").replace(b"custom = value", b"custom = edited")
        self.path.write_bytes(changed)
        self.manage("remove")
        self.assertEqual(self.path.read_bytes(), changed.replace(b";inari-blue", b""))

    def test_trigger_after_system_package_overwrites_config(self):
        self.manage("install")
        replacement = BASE.replace(b"first;last", b"first;system-new")
        self.path.write_bytes(replacement)
        self.manage("refresh")
        self.assertIn(b"system-new;inari-blue", self.path.read_bytes())
        self.manage("remove")
        self.assertEqual(self.path.read_bytes(), replacement)

    def test_distribution_adopts_previously_managed_entry(self):
        self.manage("install")
        distribution = self.path.read_bytes()
        self.path.write_bytes(distribution)
        self.manage("refresh")
        self.manage("remove")
        self.assertEqual(self.path.read_bytes(), distribution)

    def test_refresh_retains_ownership_of_admin_override(self):
        override = self.root / config.CONFIGS[1]
        override.parent.mkdir(parents=True)
        override.write_bytes(BASE)
        self.manage("install")
        self.path.write_bytes(BASE.replace(b"first;last", b"new-stock"))
        self.manage("refresh")
        self.manage("remove")
        self.assertEqual(override.read_bytes(), BASE)
        self.assertIn(b"new-stock", self.path.read_bytes())

    def test_interrupted_install_remains_reversible(self):
        self.manage("install")
        # Ownership was written but pretend the config replacement never happened.
        self.path.write_bytes(BASE)
        self.manage("remove")
        self.assertEqual(self.path.read_bytes(), BASE)

    def test_effective_override_and_cleanup_both_paths(self):
        self.manage("install")
        override = self.root / config.CONFIGS[1]
        override.parent.mkdir(parents=True)
        override.write_bytes(BASE.replace(b"first;last", b"override"))
        self.manage("install")
        self.assertIn(b"override;inari-blue", override.read_bytes())
        self.manage("remove")
        self.assertEqual(self.path.read_bytes(), BASE)
        self.assertEqual(override.read_bytes(), BASE.replace(b"first;last", b"override"))

    def test_override_prevents_vendor_edit(self):
        override = self.root / config.CONFIGS[1]
        override.parent.mkdir(parents=True)
        override.write_bytes(BASE)
        self.manage("install")
        self.assertEqual(self.path.read_bytes(), BASE)
        self.assertIn(b";inari-blue", override.read_bytes())

    def test_missing_key_and_empty_and_trailing_separator(self):
        variants = [BASE.replace(b"plugins-optional = first;last\n", b""),
                    BASE.replace(b"first;last", b""), BASE.replace(b"first;last", b"first;last;"),
                    BASE.replace(b"first;last", b"first;last \t"),
                    BASE.replace(b"\n", b"\r\n"), BASE.rstrip(b"\n")]
        for original in variants:
            with self.subTest(original=original):
                self.path.write_bytes(original)
                self.manage("install")
                self.manage("remove")
                self.assertEqual(self.path.read_bytes(), original)

    def test_missing_key_with_later_addition(self):
        self.path.write_bytes(BASE.replace(b"plugins-optional = first;last\n", b""))
        self.manage("install")
        self.path.write_bytes(self.path.read_bytes().replace(b"= inari-blue", b"= inari-blue;later"))
        self.manage("remove")
        self.assertIn(b"plugins-optional = later\n", self.path.read_bytes())

    def test_no_substring_or_comment_matching(self):
        original = BASE.replace(b"first;last", b"inari-blue-other;not-inari-blue") + b"# inari-blue\n"
        self.path.write_bytes(original)
        self.manage("install")
        self.assertIn(b"not-inari-blue;inari-blue", self.path.read_bytes())
        self.manage("remove")
        self.assertEqual(self.path.read_bytes(), original)

    def test_missing_file_on_uninstall(self):
        self.manage("install")
        self.path.unlink()
        self.manage("remove")
        self.assertFalse(self.path.exists())

    def test_user_removed_token(self):
        self.manage("install")
        self.path.write_bytes(BASE)
        self.manage("remove")
        self.assertEqual(self.path.read_bytes(), BASE)

    def test_rejects_ambiguous_config_without_writes(self):
        for original in (BASE + b"[general]\n", BASE.replace(b"plugins-optional = first;last", b"plugins-optional = a\nplugins-optional = b"), b"[wrong]\nkey=value\n"):
            self.path.write_bytes(original)
            with self.assertRaises(ValueError):
                self.manage("install")
            self.assertEqual(self.path.read_bytes(), original)

    def test_rejects_symlink_without_touching_target(self):
        target = self.path.with_name("target")
        self.path.rename(target)
        self.path.symlink_to(target.name)
        with self.assertRaises(ValueError):
            self.manage("install")
        self.assertEqual(target.read_bytes(), BASE)

    def test_preserves_permissions_and_xattrs(self):
        self.path.chmod(0o640)
        os.setxattr(str(self.path), "user.inari-test", b"keep")
        self.manage("install")
        self.assertEqual(self.path.stat().st_mode & 0o777, 0o640)
        self.assertEqual(os.getxattr(str(self.path), "user.inari-test"), b"keep")
        self.manage("remove")
        self.assertEqual(self.path.stat().st_mode & 0o777, 0o640)

    def test_concurrent_edit_is_not_overwritten(self):
        with self.assertRaises(ValueError):
            config.atomic_write(self.path, b"unwanted", b"stale read")
        self.assertEqual(self.path.read_bytes(), BASE)


if __name__ == "__main__":
    unittest.main()
