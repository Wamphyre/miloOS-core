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
        self.assertEqual(
            milopkg.clean_display_name(
                "GNU Image Manipulation Program",
                "gimp",
                "3.0.4",
            ),
            "GIMP",
        )

    def test_package_validation(self):
        self.assertEqual(
            milopkg.normalize_package("libfoo-2.0:amd64"),
            "libfoo-2.0:amd64",
        )
        with self.assertRaises(milopkg.MiloPKGError):
            milopkg.normalize_package("../../bad")

    def test_managed_appimages_support_current_and_legacy_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data_home = root / "data"
            cache_home = root / "cache"
            applications = data_home / "applications"
            applications.mkdir(parents=True)

            legacy_appimage = root / "Firefox ESR.appimage"
            legacy_appimage.touch()
            (applications / "milopkg-appimage-legacy.desktop").write_text(
                "[Desktop Entry]\n"
                "Name=Firefox ESR\n"
                f"X-miloOS-AppImage={legacy_appimage}\n"
                "X-miloOS-AppImage-CacheKey=legacy-key\n"
                "X-AppImage-Version=128.0-1\n",
                encoding="utf-8",
            )
            cached = (
                cache_home
                / "milofiles/appimages/legacy-key/application.desktop"
            )
            cached.parent.mkdir(parents=True)
            cached.write_text(
                "[Desktop Entry]\n"
                "Icon=firefox-esr\n"
                "X-AppImage-Version=128.0-1\n",
                encoding="utf-8",
            )

            current_appimage = root / "GIMP.appimage"
            current_appimage.touch()
            (applications / "milopkg-appimage-current.desktop").write_text(
                "[Desktop Entry]\n"
                "Name=GIMP\n"
                f"X-miloOS-AppImage={current_appimage}\n"
                "X-AppImage-Version=3.0.4-1\n"
                "X-miloPKG-Package=gimp\n",
                encoding="utf-8",
            )
            (applications / "milopkg-appimage-stale.desktop").write_text(
                "[Desktop Entry]\n"
                f"X-miloOS-AppImage={root / 'missing.appimage'}\n"
                "X-AppImage-Version=1\n"
                "X-miloPKG-Package=missing\n",
                encoding="utf-8",
            )

            apps = milopkg.discover_managed_appimages(
                data_home=data_home,
                cache_home=cache_home,
            )
            self.assertEqual(
                [(app.package, app.version) for app in apps],
                [
                    ("firefox-esr", "128.0-1"),
                    ("gimp", "3.0.4-1"),
                ],
            )

    def test_debian_versions_and_update_detection(self):
        self.assertTrue(
            milopkg.debian_version_is_newer("1:2.0-1", "2.0-9")
        )
        self.assertFalse(
            milopkg.debian_version_is_newer("2.0-1", "2.0-1")
        )

        root = Path("/tmp")
        apps = [
            milopkg.ManagedAppImage(
                path=root / "Example.appimage",
                package="example",
                version="1.0-1",
                display_name="Example",
                desktop_path=root / "example.desktop",
            ),
            milopkg.ManagedAppImage(
                path=root / "Current.appimage",
                package="current",
                version="2.0-1",
                display_name="Current",
                desktop_path=root / "current.desktop",
            ),
        ]

        class Repository:
            @staticmethod
            def details(package):
                versions = {"example": "1.1-1", "current": "2.0-1"}
                return milopkg.PackageInfo(
                    package=package,
                    version=versions[package],
                )

        updates = milopkg.find_appimage_updates(Repository(), apps)
        self.assertEqual(len(updates), 1)
        self.assertEqual(updates[0].app.package, "example")
        self.assertEqual(updates[0].available.version, "1.1-1")

    def test_root_desktop_records_milopkg_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary)
            desktop = milopkg.AppImageBuilder._write_root_desktop(
                appdir,
                {"Exec": "example %U", "Categories": "Utility;"},
                "Example",
                "example",
                milopkg.PackageInfo(
                    package="example",
                    version="2.4-1",
                    architecture="amd64",
                ),
            )
            entry = milopkg.read_desktop_entry(desktop)
            self.assertEqual(entry["X-miloPKG-Package"], "example")
            self.assertEqual(entry["X-miloPKG-Format"], "1")
            self.assertEqual(entry["X-AppImage-Version"], "2.4-1")

    def test_update_rebuilds_in_place_and_refreshes_registration(self):
        path = Path("/tmp/Example.appimage")
        update = milopkg.AppImageUpdate(
            app=milopkg.ManagedAppImage(
                path=path,
                package="example",
                version="1.0-1",
                display_name="Example",
                desktop_path=Path("/tmp/example.desktop"),
            ),
            available=milopkg.PackageInfo(
                package="example",
                version="1.1-1",
            ),
        )
        expected = milopkg.BuildResult(
            path=path,
            display_name="Example",
            package="example",
            version="1.1-1",
            bundled_packages=4,
        )

        class Builder:
            def build(self, *args, **kwargs):
                self.args = args
                self.kwargs = kwargs
                return expected

        builder = Builder()
        with patch.object(
            milopkg,
            "refresh_appimage_registration",
            return_value=True,
        ) as refresh:
            result, registered = milopkg.rebuild_appimage_update(
                builder,
                update,
            )
        self.assertEqual(result, expected)
        self.assertTrue(registered)
        self.assertEqual(builder.args, ("example", path.parent))
        self.assertEqual(
            builder.kwargs,
            {"overwrite": True, "target_path": path},
        )
        refresh.assert_called_once_with(path)

    def test_registration_uses_milofiles_command(self):
        path = Path("/tmp/Application with spaces.appimage")
        completed = subprocess.CompletedProcess(
            args=[],
            returncode=0,
        )
        with patch.object(
            milopkg.shutil,
            "which",
            return_value="/usr/local/bin/milofiles",
        ), patch.object(
            milopkg.subprocess,
            "run",
            return_value=completed,
        ) as run:
            self.assertTrue(
                milopkg.refresh_appimage_registration(path)
            )
        self.assertEqual(
            run.call_args.args[0],
            [
                "/usr/local/bin/milofiles",
                "--register-appimage",
                str(path.resolve()),
            ],
        )

    def test_repository_refresh_uses_system_authentication(self):
        class Process:
            stdout = iter(["Hit: repository\n"])

            @staticmethod
            def wait():
                return 0

        logs = []
        with patch.object(
            milopkg.shutil,
            "which",
            side_effect=lambda command: f"/usr/bin/{command}",
        ), patch.object(
            milopkg.subprocess,
            "Popen",
            return_value=Process(),
        ) as popen:
            milopkg.refresh_repository_indexes(logs.append)
        self.assertEqual(logs, ["Hit: repository"])
        self.assertEqual(
            popen.call_args.args[0],
            ["/usr/bin/pkexec", "/usr/bin/apt-get", "update"],
        )

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

    def test_system_theme_icon_precedes_package_icon(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            appdir = root / "Example.AppDir"
            package_icons = (
                appdir / "usr/share/icons/hicolor/scalable/apps"
            )
            package_icons.mkdir(parents=True)
            (package_icons / "example.svg").write_text(
                "package-icon",
                encoding="utf-8",
            )

            icon_root = root / "icons"
            themed_icons = icon_root / "TestTheme/apps/scalable"
            themed_icons.mkdir(parents=True)
            (icon_root / "TestTheme/index.theme").write_text(
                "[Icon Theme]\n"
                "Name=TestTheme\n"
                "Directories=apps/scalable\n\n"
                "[apps/scalable]\n"
                "Size=64\n"
                "Type=Scalable\n"
                "Context=Applications\n",
                encoding="utf-8",
            )
            themed_icon = themed_icons / "example.svg"
            themed_icon.write_text("theme-icon", encoding="utf-8")

            builder = milopkg.AppImageBuilder.__new__(
                milopkg.AppImageBuilder
            )
            with patch.object(
                milopkg.AppImageBuilder,
                "_active_icon_theme",
                return_value="TestTheme",
            ), patch.object(
                milopkg.AppImageBuilder,
                "_icon_theme_roots",
                return_value=[icon_root],
            ):
                installed = builder._install_icon(
                    appdir,
                    {"Icon": "example"},
                    "example",
                )
            self.assertEqual(
                installed.read_text(encoding="utf-8"),
                "theme-icon",
            )
            self.assertEqual(
                (appdir / ".DirIcon").resolve(),
                installed.resolve(),
            )

    def test_system_icon_theme_inheritance(self):
        with tempfile.TemporaryDirectory() as temporary:
            icon_root = Path(temporary) / "icons"
            child = icon_root / "ChildTheme"
            parent_icons = icon_root / "ParentTheme/apps/scalable"
            child.mkdir(parents=True)
            parent_icons.mkdir(parents=True)
            (child / "index.theme").write_text(
                "[Icon Theme]\n"
                "Name=ChildTheme\n"
                "Inherits=ParentTheme\n",
                encoding="utf-8",
            )
            (icon_root / "ParentTheme/index.theme").write_text(
                "[Icon Theme]\n"
                "Name=ParentTheme\n"
                "Directories=apps/scalable\n",
                encoding="utf-8",
            )
            inherited = parent_icons / "example.svg"
            inherited.write_text("<svg/>", encoding="utf-8")
            self.assertEqual(
                milopkg.AppImageBuilder._system_icon_candidates(
                    "example",
                    theme="ChildTheme",
                    icon_roots=[icon_root],
                ),
                [inherited],
            )

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
                "Recommends": "recommended | optional",
            }
        )
        self.assertEqual(
            groups,
            [
                ["runtime (>= 1)"],
                ["first", "fallback"],
                ["data:any"],
                ["recommended", "optional"],
            ],
        )
        self.assertEqual(
            milopkg.AptRepository._relation_package_name("data:any (>= 2)"),
            "data",
        )

    def test_privileged_helpers_stay_on_host(self):
        self.assertIn("pkexec", milopkg.HOST_RUNTIME_PACKAGES)

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
            self.assertIn('export MLT_REPOSITORY="', script)
            self.assertIn('export MLT_DATA="', script)
            self.assertIn('export LV2_PATH="', script)
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

    def test_runtime_library_scan_includes_dependency_runpaths(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary) / "example.AppDir"
            executable = appdir / "usr/bin/example"
            private_library = (
                appdir / "usr/lib/x86_64-linux-gnu/libexample.so.1"
            )
            executable.parent.mkdir(parents=True)
            private_library.parent.mkdir(parents=True)
            executable.write_bytes(b"\x7fELF")
            private_library.write_bytes(b"\x7fELF")
            dynamic_section = (
                " 0x000000000000001d (RUNPATH) "
                "Library runpath: [/usr/lib/example-private]\n"
            )
            (appdir / "usr/lib/example-private").mkdir(parents=True)
            completed = subprocess.CompletedProcess(
                args=[],
                returncode=0,
                stdout=dynamic_section,
                stderr="",
            )
            with patch.object(
                milopkg.shutil,
                "which",
                return_value="/usr/bin/readelf",
            ), patch.object(
                milopkg.subprocess,
                "run",
                return_value=completed,
            ) as run:
                directories = (
                    milopkg.AppImageBuilder._runtime_library_dirs(
                        appdir,
                        executable,
                    )
                )
            self.assertEqual(directories, ["usr/lib/example-private"])
            scanned = run.call_args.args[0]
            self.assertIn(str(executable), scanned)
            self.assertIn(str(private_library), scanned)

    def test_runtime_layout_links_packaged_application_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary) / "hydrogen.AppDir"
            executable = appdir / "usr/bin/hydrogen"
            packaged_data = appdir / "usr/share/hydrogen/data"
            executable.parent.mkdir(parents=True)
            executable.touch()
            packaged_data.mkdir(parents=True)
            milopkg.AppImageBuilder._prepare_runtime_layout(
                appdir,
                executable,
            )
            adjacent_data = executable.parent / "data"
            self.assertTrue(adjacent_data.is_symlink())
            self.assertEqual(adjacent_data.resolve(), packaged_data.resolve())
            builder = milopkg.AppImageBuilder.__new__(
                milopkg.AppImageBuilder
            )
            builder._write_apprun(
                appdir,
                [str(executable)],
                executable=executable,
            )
            self.assertIn(
                ' --data "$APPDIR/usr/share/hydrogen/data/" "$@"',
                (appdir / "AppRun").read_text(encoding="utf-8"),
            )

    def test_gparted_uses_host_pkexec_and_packaged_binary(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary) / "gparted.AppDir"
            executable = appdir / "usr/sbin/gparted"
            binary = appdir / "usr/libexec/gpartedbin"
            executable.parent.mkdir(parents=True)
            binary.parent.mkdir(parents=True)
            executable.write_text(
                '#!/bin/sh\n'
                'BASE_CMD="/usr/libexec/gpartedbin $*"\n'
                "ENABLE_XHOST_ROOT=no\n"
                "pkexec --disable-internal-agent "
                "'/usr/sbin/gparted' \"$@\"\n",
                encoding="utf-8",
            )
            binary.touch()

            milopkg.AppImageBuilder._prepare_runtime_layout(
                appdir,
                executable,
            )
            wrapper = executable.read_text(encoding="utf-8")
            self.assertIn(
                'BASE_CMD="${APPDIR}/usr/libexec/gpartedbin $*"',
                wrapper,
            )
            self.assertIn(
                '/usr/bin/pkexec --disable-internal-agent '
                '/usr/bin/env DISPLAY="${DISPLAY:-}" '
                'XAUTHORITY="${XAUTHORITY:-}" GDK_BACKEND=x11 '
                '"${APPIMAGE:-$0}" --milopkg-gparted-root "$@"',
                wrapper,
            )
            self.assertIn("ENABLE_XHOST_ROOT=yes", wrapper)

            builder = milopkg.AppImageBuilder.__new__(
                milopkg.AppImageBuilder
            )
            builder._write_apprun(
                appdir,
                [str(executable)],
                executable=executable,
            )
            script = (appdir / "AppRun").read_text(encoding="utf-8")
            self.assertIn(
                '[ "${1:-}" = "--milopkg-gparted-root" ]',
                script,
            )
            subprocess.run(
                ["sh", "-n", str(appdir / "AppRun")],
                check=True,
            )

    def test_chrome_uses_namespace_sandbox(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary) / "chrome.AppDir"
            executable = appdir / "usr/bin/google-chrome-stable"
            chrome = appdir / "opt/google/chrome/chrome"
            sandbox = appdir / "opt/google/chrome/chrome-sandbox"
            executable.parent.mkdir(parents=True)
            chrome.parent.mkdir(parents=True)
            executable.touch()
            chrome.touch()
            sandbox.touch()

            milopkg.AppImageBuilder._prepare_runtime_layout(
                appdir,
                executable,
            )
            self.assertFalse(sandbox.exists())

            builder = milopkg.AppImageBuilder.__new__(
                milopkg.AppImageBuilder
            )
            builder._write_apprun(
                appdir,
                [str(executable)],
                executable=executable,
            )
            script = (appdir / "AppRun").read_text(encoding="utf-8")
            self.assertNotIn("--no-sandbox", script)
            self.assertIn(
                'set -- "--lang=$MILO_CHROME_LOCALE" "$@"',
                script,
            )
            self.assertIn(
                '""|C|C.*|POSIX) continue',
                script,
            )

    def test_guitarix_uses_packaged_resources_and_plugins(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary) / "guitarix.AppDir"
            executable = appdir / "usr/bin/guitarix"
            data = appdir / "usr/share/gx_head"
            executable.parent.mkdir(parents=True)
            (data / "builder").mkdir(parents=True)
            (data / "skins").mkdir()
            executable.write_bytes(
                b"prefix:/usr/share/gx_head/skins:suffix"
            )

            milopkg.AppImageBuilder._prepare_runtime_layout(
                appdir,
                executable,
            )
            self.assertIn(
                b"usr/share//gx_head/skins",
                executable.read_bytes(),
            )
            self.assertNotIn(
                b"/usr/share/gx_head",
                executable.read_bytes(),
            )

            builder = milopkg.AppImageBuilder.__new__(
                milopkg.AppImageBuilder
            )
            builder._write_apprun(
                appdir,
                [str(executable)],
                executable=executable,
            )
            script = (appdir / "AppRun").read_text(encoding="utf-8")
            self.assertIn(
                '--builder-dir="$APPDIR/usr/share/gx_head/builder"',
                script,
            )
            self.assertIn(
                '--style-dir="$APPDIR/usr/share/gx_head/skins"',
                script,
            )
            self.assertIn('\ncd "$APPDIR"\n', script)
            self.assertIn('export LV2_PATH="', script)
            subprocess.run(
                ["sh", "-n", str(appdir / "AppRun")],
                check=True,
            )

    def test_gimp_runtime_uses_packaged_resources(self):
        with tempfile.TemporaryDirectory() as temporary:
            appdir = Path(temporary) / "gimp.AppDir"
            executable = appdir / "usr/bin/gimp-3.0"
            plugin = (
                appdir
                / "usr/lib"
                / milopkg.multiarch_triplet()
                / "gimp/3.0/plug-ins/example/example.py"
            )
            executable.parent.mkdir(parents=True)
            executable.touch()
            plugin.parent.mkdir(parents=True)
            plugin.write_text(
                "#!/usr/bin/python3\nprint('example')\n",
                encoding="utf-8",
            )
            (appdir / "usr/share/gimp/3.0").mkdir(parents=True)

            milopkg.AppImageBuilder._prepare_runtime_layout(
                appdir,
                executable,
            )
            self.assertTrue(
                plugin.read_text(encoding="utf-8").startswith(
                    "#!/usr/bin/env python3\n"
                )
            )

            builder = milopkg.AppImageBuilder.__new__(
                milopkg.AppImageBuilder
            )
            builder._write_apprun(
                appdir,
                [str(executable)],
                executable=executable,
            )
            script = (appdir / "AppRun").read_text(encoding="utf-8")
            self.assertIn(
                'GIMP3_DATADIR="$APPDIR/usr/share/gimp/3.0"',
                script,
            )
            self.assertIn(
                f'GIMP3_PLUGINDIR="$APPDIR/usr/lib/'
                f'{milopkg.multiarch_triplet()}/gimp/3.0"',
                script,
            )
            self.assertIn(
                f'GEGL_PATH="$APPDIR/usr/lib/'
                f'{milopkg.multiarch_triplet()}/gegl-0.4"',
                script,
            )


if __name__ == "__main__":
    unittest.main()
