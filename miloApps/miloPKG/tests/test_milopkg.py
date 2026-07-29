#!/usr/bin/env python3

import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


MODULE_PATH = Path(__file__).resolve().parents[1] / "milopkg.py"
SPEC = importlib.util.spec_from_file_location("milopkg", MODULE_PATH)
assert SPEC and SPEC.loader
milopkg = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = milopkg
SPEC.loader.exec_module(milopkg)


class MiloPKGTests(unittest.TestCase):
    def test_output_directory_settings_round_trip(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config_home = root / "config"
            output_directory = root / "Applications"
            output_directory.mkdir()
            with patch.dict(
                os.environ,
                {"XDG_CONFIG_HOME": str(config_home)},
            ):
                self.assertTrue(
                    milopkg.save_output_directory(output_directory)
                )
                self.assertEqual(
                    milopkg.load_output_directory(),
                    output_directory.resolve(),
                )
                settings = config_home / "miloPKG/settings.ini"
                self.assertEqual(settings.stat().st_mode & 0o777, 0o600)

    def test_invalid_saved_output_directory_uses_home(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config_home = root / "config"
            settings = config_home / "miloPKG/settings.ini"
            settings.parent.mkdir(parents=True)
            settings.write_text(
                "[General]\n"
                f"output_directory = {root / 'missing'}\n",
                encoding="utf-8",
            )
            with patch.dict(
                os.environ,
                {"XDG_CONFIG_HOME": str(config_home)},
            ):
                self.assertEqual(
                    milopkg.load_output_directory(),
                    Path.home(),
                )

    def test_parse_deb822_continuation_and_records(self):
        records = milopkg.parse_deb822(
            "Package: example\n"
            "Version: 2.1\n"
            "Description-en: First line\n"
            " second line\n\n"
            "Package: other\nVersion: 1\n"
        )
        self.assertEqual(records[0]["Package"], "example")
        self.assertEqual(records[0]["Description-en"], "First line\nsecond line")
        self.assertEqual(records[1]["Package"], "other")

    def test_clean_versionless_filename(self):
        name = milopkg.clean_display_name(
            "Example 3.4.1", "example", "3.4.1"
        )
        self.assertEqual(name, "Example")
        self.assertEqual(
            milopkg.safe_output_filename(name), "Example.appimage"
        )

    def test_package_validation(self):
        self.assertEqual(
            milopkg.normalize_package("libfoo-2.0:amd64"),
            "libfoo-2.0:amd64",
        )
        with self.assertRaises(milopkg.MiloPKGError):
            milopkg.normalize_package("../../bad")

    def test_desktop_and_icon_selection(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary)
            applications = appdir / "usr/share/applications"
            icons = appdir / "usr/share/icons/hicolor/scalable/apps"
            applications.mkdir(parents=True)
            icons.mkdir(parents=True)
            (applications / "example.desktop").write_text(
                "[Desktop Entry]\n"
                "Type=Application\n"
                "Name=Example\n"
                "Exec=example %F\n"
                "Icon=example\n",
                encoding="utf-8",
            )
            (icons / "example.svg").write_text("<svg/>", encoding="utf-8")
            builder = milopkg.AppImageBuilder.__new__(
                milopkg.AppImageBuilder
            )
            path, entry = builder._select_desktop(appdir, "example")
            self.assertEqual(path.name, "example.desktop")
            self.assertEqual(entry["Name"], "Example")
            candidates = builder._icon_candidates(appdir, "example")
            self.assertEqual(candidates, [icons / "example.svg"])

    def test_exec_command_behind_env(self):
        command = milopkg.AppImageBuilder._exec_command(
            "env FOO=bar /usr/bin/example --new-window %U"
        )
        self.assertEqual(command, "example")

    def test_dependency_relations_keep_alternative_groups(self):
        groups = milopkg.AptRepository._relation_alternatives(
            {
                "Pre-Depends": "runtime (>= 1)",
                "Depends": "first | fallback, data:any",
            }
        )
        self.assertEqual(
            groups,
            [["runtime (>= 1)"], ["first", "fallback"], ["data:any"]],
        )
        self.assertEqual(
            milopkg.AptRepository._relation_package_name("data:any (>= 2)"),
            "data",
        )

    def test_apprun_rewrites_build_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary) / "example.AppDir"
            executable = appdir / "usr/bin/example"
            executable.parent.mkdir(parents=True)
            executable.touch()
            rendered = milopkg.AppImageBuilder._apprun_argument(
                appdir, str(executable)
            )
            self.assertEqual(rendered, '"$APPDIR/usr/bin/example"')
            self.assertNotIn(temporary, rendered)

    def test_apprun_enables_milo_panel_appmenu(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary) / "example.AppDir"
            executable = appdir / "usr/bin/example"
            executable.parent.mkdir(parents=True)
            executable.touch()
            builder = milopkg.AppImageBuilder.__new__(
                milopkg.AppImageBuilder
            )
            builder._write_apprun(
                appdir,
                [str(executable)],
                "example.desktop",
            )
            apprun = appdir / "AppRun"
            script = apprun.read_text(encoding="utf-8")
            self.assertIn("appmenu-gtk-module", script)
            self.assertIn("UBUNTU_MENUPROXY=1", script)
            self.assertIn(
                'GIO_LAUNCHED_DESKTOP_FILE="$APPDIR/example.desktop"',
                script,
            )
            subprocess.run(["sh", "-n", str(apprun)], check=True)
            self.assertEqual(apprun.stat().st_mode & 0o777, 0o755)

    def test_apprun_rewrites_absolute_runtime_library_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary) / "example.AppDir"
            private_lib = appdir / "usr/lib/example"
            private_lib.mkdir(parents=True)
            dynamic_section = (
                " 0x000000000000001d (RUNPATH) "
                "Library runpath: [/usr/lib/example:/outside:$ORIGIN]\n"
            )
            directories = (
                milopkg.AppImageBuilder._internal_runtime_library_dirs(
                    appdir,
                    dynamic_section,
                )
            )
            self.assertEqual(directories, ["usr/lib/example"])


if __name__ == "__main__":
    unittest.main()
