#!/usr/bin/env python3
"""
miloPKG

Turn applications from the configured Debian repositories into portable
AppImage files.  The module contains both the reusable packaging backend and
the GTK/terminal frontends.
"""

from __future__ import annotations

import argparse
import configparser
import locale
import os
import platform
import re
import selectors
import shlex
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Optional, Sequence


APP_VERSION = "1.0.0"
APPIMAGETOOL_URL = (
    "https://github.com/AppImage/appimagetool/releases/download/continuous/"
    "appimagetool-{arch}.AppImage"
)
PACKAGE_RE = re.compile(
    r"^[a-z0-9][a-z0-9+.-]*(?::[a-z0-9][a-z0-9-]*)?$"
)
FIELD_CODE_RE = re.compile(r"%[fFuUdDnNickvm]")

# These packages belong to the host runtime.  Bundling glibc, the dynamic
# loader, systemd or a shell inside LD_LIBRARY_PATH makes an AppImage less
# portable and can prevent it from starting at all.
HOST_RUNTIME_PACKAGES = {
    "apt",
    "base-files",
    "base-passwd",
    "bash",
    "coreutils",
    "dash",
    "debconf",
    "debianutils",
    "diffutils",
    "dpkg",
    "e2fsprogs",
    "findutils",
    "gcc-14-base",
    "grep",
    "gzip",
    "init-system-helpers",
    "libc-bin",
    "libc6",
    "libcrypt1",
    "libnss-systemd",
    "libpam-systemd",
    "login",
    "mount",
    "ncurses-base",
    "passwd",
    "perl-base",
    "sed",
    "systemd",
    "systemd-sysv",
    "tar",
    "tzdata",
    "util-linux",
}

FALLBACK_ICON = """\
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <defs>
    <linearGradient id="g" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#6f8cff"/>
      <stop offset="1" stop-color="#4c63d2"/>
    </linearGradient>
  </defs>
  <rect x="18" y="18" width="220" height="220" rx="52" fill="url(#g)"/>
  <path d="M67 88l61-34 61 34-61 35z" fill="#fff" opacity=".96"/>
  <path d="M67 88v73l61 36v-74z" fill="#e8ecff"/>
  <path d="M189 88v73l-61 36v-74z" fill="#cbd4ff"/>
  <path d="M151 45l-61 35 22 13 61-35z" fill="#9daeff"/>
  <path d="M128 123v74" stroke="#fff" stroke-width="7" opacity=".55"/>
</svg>
"""


TRANSLATIONS = {
    "en": {
        "window_title": "miloPKG",
        "title": "Debian to AppImage",
        "subtitle": "Search the Debian repositories and create a portable application.",
        "search_placeholder": "Application or package name…",
        "search": "Search",
        "package": "Package",
        "version": "Version",
        "architecture": "Architecture",
        "description": "Description",
        "results": "{count} packages found",
        "no_results": "No packages were found.",
        "select_package": "Select a package from the list.",
        "output": "Save in",
        "choose": "Choose…",
        "convert": "Create AppImage",
        "cancel": "Cancel",
        "ready": "Ready",
        "searching": "Searching repositories…",
        "working": "Preparing the package…",
        "success_title": "AppImage created",
        "success": "{name} is ready to use.",
        "open_folder": "Open Folder",
        "close": "Close",
        "error_title": "Could not create the AppImage",
        "error_search": "Could not search the Debian repositories.",
        "existing": "The destination file already exists. Remove it or choose another folder.",
        "selected": "{package} · {version} · {size}",
        "log": "Packaging details",
    },
    "es": {
        "window_title": "miloPKG",
        "title": "De Debian a AppImage",
        "subtitle": "Busca en los repositorios Debian y crea una aplicación portátil.",
        "search_placeholder": "Nombre de la aplicación o del paquete…",
        "search": "Buscar",
        "package": "Paquete",
        "version": "Versión",
        "architecture": "Arquitectura",
        "description": "Descripción",
        "results": "{count} paquetes encontrados",
        "no_results": "No se ha encontrado ningún paquete.",
        "select_package": "Selecciona un paquete de la lista.",
        "output": "Guardar en",
        "choose": "Elegir…",
        "convert": "Crear AppImage",
        "cancel": "Cancelar",
        "ready": "Listo",
        "searching": "Buscando en los repositorios…",
        "working": "Preparando el paquete…",
        "success_title": "AppImage creado",
        "success": "{name} está listo para usar.",
        "open_folder": "Abrir carpeta",
        "close": "Cerrar",
        "error_title": "No se pudo crear el AppImage",
        "error_search": "No se pudo buscar en los repositorios Debian.",
        "existing": "El archivo de destino ya existe. Elimínalo o elige otra carpeta.",
        "selected": "{package} · {version} · {size}",
        "log": "Detalles del empaquetado",
    },
}


def language() -> str:
    try:
        current = (
            os.environ.get("LC_ALL")
            or os.environ.get("LC_MESSAGES")
            or os.environ.get("LANG")
            or locale.getlocale()[0]
            or ""
        )
        return "es" if current.lower().startswith("es") else "en"
    except Exception:
        return "en"


def tr(key: str, **values: object) -> str:
    value = TRANSLATIONS[language()].get(key, key)
    return value.format(**values) if values else value


def localized(spanish: str, english: str) -> str:
    return spanish if language() == "es" else english


class MiloPKGError(RuntimeError):
    """Expected packaging failure suitable for showing to the user."""


class BuildCancelled(MiloPKGError):
    """The user cancelled the current packaging job."""


class OutputExists(MiloPKGError):
    """The clean, versionless output filename already exists."""


@dataclass(frozen=True)
class PackageInfo:
    package: str
    version: str = ""
    architecture: str = ""
    description: str = ""
    installed_size_kib: int = 0
    section: str = ""


@dataclass(frozen=True)
class BuildResult:
    path: Path
    display_name: str
    package: str
    version: str
    bundled_packages: int


def parse_deb822(text: str) -> list[dict[str, str]]:
    """Parse the subset of Debian control syntax returned by apt-cache."""
    records: list[dict[str, str]] = []
    current: dict[str, str] = {}
    last_key: Optional[str] = None
    for raw_line in text.splitlines():
        if not raw_line.strip():
            if current:
                records.append(current)
                current = {}
            last_key = None
            continue
        if raw_line[0].isspace() and last_key:
            current[last_key] = current[last_key] + "\n" + raw_line[1:]
            continue
        if ":" not in raw_line:
            continue
        key, value = raw_line.split(":", 1)
        current[key] = value.strip()
        last_key = key
    if current:
        records.append(current)
    return records


def normalize_package(package: str) -> str:
    package = package.strip().lower()
    if package.endswith(":any") or package.endswith(":native"):
        package = package.rsplit(":", 1)[0]
    if not PACKAGE_RE.fullmatch(package):
        raise MiloPKGError(
            localized(
                f"Nombre de paquete no válido: {package!r}",
                f"Invalid package name: {package!r}",
            )
        )
    return package


def base_package_name(package: str) -> str:
    return package.split(":", 1)[0]


def human_size(byte_count: int) -> str:
    value = float(max(0, byte_count))
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if value < 1024.0 or unit == "TB":
            return f"{value:.1f} {unit}"
        value /= 1024.0
    return f"{value:.1f} TB"


def clean_display_name(name: str, package: str, version: str = "") -> str:
    """Return a readable app name without a package version or architecture."""
    value = re.sub(r"\s+", " ", (name or "").strip())
    if not value:
        value = package.replace("-", " ").strip().title()
    if version:
        escaped = re.escape(version)
        value = re.sub(
            rf"(?:\s*[-–—]\s*|\s+|\s*\(\s*)v?{escaped}\s*\)?$",
            "",
            value,
            flags=re.IGNORECASE,
        ).strip()
    value = re.sub(
        r"\s+\((?:amd64|arm64|armhf|i386|x86_64|aarch64)\)$",
        "",
        value,
        flags=re.IGNORECASE,
    ).strip()
    return value or package


def safe_output_filename(display_name: str) -> str:
    name = re.sub(r"[/\\:\x00-\x1f]", "-", display_name)
    name = re.sub(r"\s+", " ", name).strip(" .-")
    return f"{name or 'Application'}.appimage"


def safe_icon_key(package: str) -> str:
    key = re.sub(r"[^a-z0-9+.-]+", "-", base_package_name(package).lower())
    return key.strip(".-") or "application"


def appimage_arch() -> str:
    machine = platform.machine().lower()
    mapping = {
        "x86_64": "x86_64",
        "amd64": "x86_64",
        "aarch64": "aarch64",
        "arm64": "aarch64",
        "armv7l": "armhf",
        "armv7": "armhf",
        "i386": "i686",
        "i486": "i686",
        "i586": "i686",
        "i686": "i686",
    }
    if machine not in mapping:
        raise MiloPKGError(
            localized(
                f"Arquitectura no compatible con appimagetool: {machine or 'desconocida'}",
                f"Architecture not supported by appimagetool: {machine or 'unknown'}",
            )
        )
    return mapping[machine]


def multiarch_triplet() -> str:
    try:
        result = subprocess.run(
            ["dpkg-architecture", "-qDEB_HOST_MULTIARCH"],
            check=True,
            capture_output=True,
            text=True,
        )
        value = result.stdout.strip()
        if value:
            return value
    except (OSError, subprocess.SubprocessError):
        pass
    return {
        "x86_64": "x86_64-linux-gnu",
        "aarch64": "aarch64-linux-gnu",
        "armhf": "arm-linux-gnueabihf",
        "i686": "i386-linux-gnu",
    }.get(appimage_arch(), "")


class AptRepository:
    """Read package information from the host's configured APT repositories."""

    def __init__(self) -> None:
        for command in ("apt-cache", "apt-get", "dpkg-deb"):
            if shutil.which(command) is None:
                raise MiloPKGError(
                    localized(
                        f"Falta la herramienta necesaria: {command}",
                        f"Required tool is missing: {command}",
                    )
                )
        self._control_cache: dict[str, Optional[dict[str, str]]] = {}
        self._provider_cache: dict[str, list[str]] = {}
        try:
            self._host_architecture = self._capture(
                ["dpkg", "--print-architecture"]
            ).strip()
        except MiloPKGError:
            self._host_architecture = ""

    @staticmethod
    def _capture(args: Sequence[str], cwd: Optional[Path] = None) -> str:
        env = os.environ.copy()
        env.update({"LC_ALL": "C", "LANG": "C"})
        try:
            result = subprocess.run(
                list(args),
                cwd=str(cwd) if cwd else None,
                env=env,
                check=False,
                capture_output=True,
                text=True,
            )
        except OSError as error:
            raise MiloPKGError(str(error)) from error
        if result.returncode != 0:
            message = result.stderr.strip() or result.stdout.strip()
            raise MiloPKGError(
                message
                or localized(
                    f"El comando falló: {args[0]}",
                    f"Command failed: {args[0]}",
                )
            )
        return result.stdout

    def search(self, query: str, limit: int = 100) -> list[PackageInfo]:
        query = query.strip()
        if not query:
            return []
        # apt-cache treats the term as a regular expression. Escaping makes a
        # normal application name safe and predictable. Whitespace can match a
        # Debian package separator, e.g. "GNOME Calculator" finds
        # "gnome-calculator".
        pattern = ".*".join(
            re.escape(term) for term in query.split() if term
        )
        output = self._capture(
            ["apt-cache", "search", "--names-only", pattern]
        )
        descriptions: dict[str, str] = {}
        for line in output.splitlines():
            match = re.match(r"^(\S+)\s+-\s+(.*)$", line)
            if not match:
                continue
            package = match.group(1).lower()
            if PACKAGE_RE.fullmatch(package):
                descriptions[package] = match.group(2).strip()

        query_lower = re.sub(r"\s+", "-", query.lower())
        names = sorted(
            descriptions,
            key=lambda name: (
                name != query_lower,
                not name.startswith(query_lower),
                name,
            ),
        )[:limit]
        if not names:
            return []

        metadata: dict[str, dict[str, str]] = {}
        for index in range(0, len(names), 40):
            chunk = names[index : index + 40]
            try:
                records = parse_deb822(
                    self._capture(
                        ["apt-cache", "show", "--no-all-versions", *chunk]
                    )
                )
            except MiloPKGError:
                records = []
            for record in records:
                package = record.get("Package", "").lower()
                if package and package not in metadata:
                    metadata[package] = record

        results: list[PackageInfo] = []
        for name in names:
            record = metadata.get(base_package_name(name), {})
            try:
                installed_size = int(record.get("Installed-Size", "0") or 0)
            except ValueError:
                installed_size = 0
            results.append(
                PackageInfo(
                    package=name,
                    version=record.get("Version", ""),
                    architecture=record.get("Architecture", ""),
                    description=record.get(
                        "Description-en", record.get("Description", descriptions[name])
                    ).splitlines()[0],
                    installed_size_kib=installed_size,
                    section=record.get("Section", ""),
                )
            )
        return results

    def details(self, package: str) -> PackageInfo:
        package = normalize_package(package)
        records = parse_deb822(
            self._capture(
                ["apt-cache", "show", "--no-all-versions", package]
            )
        )
        if not records:
            raise MiloPKGError(
                localized(
                    f"El paquete «{package}» no existe en los repositorios configurados.",
                    f"Package “{package}” is not available from the configured repositories.",
                )
            )
        record = records[0]
        self._control_cache[package] = record
        try:
            installed_size = int(record.get("Installed-Size", "0") or 0)
        except ValueError:
            installed_size = 0
        description = record.get(
            "Description-en", record.get("Description", "")
        ).splitlines()[0]
        return PackageInfo(
            package=record.get("Package", package),
            version=record.get("Version", ""),
            architecture=record.get("Architecture", ""),
            description=description,
            installed_size_kib=installed_size,
            section=record.get("Section", ""),
        )

    def _control_record(self, package: str) -> Optional[dict[str, str]]:
        package = normalize_package(package)
        self._prefetch_controls([package])
        return self._control_cache.get(package)

    def _prefetch_controls(self, packages: Iterable[str]) -> None:
        requested: list[str] = []
        for package in packages:
            try:
                normalized = normalize_package(package)
            except MiloPKGError:
                continue
            if normalized not in self._control_cache and normalized not in requested:
                requested.append(normalized)
        if not requested:
            return

        def fetch(chunk: list[str]) -> None:
            try:
                output = self._capture(
                    ["apt-cache", "show", "--no-all-versions", *chunk]
                )
            except MiloPKGError:
                if len(chunk) > 1:
                    middle = len(chunk) // 2
                    fetch(chunk[:middle])
                    fetch(chunk[middle:])
                else:
                    self._control_cache[chunk[0]] = None
                return
            records = parse_deb822(output)
            by_package: dict[str, dict[str, str]] = {}
            for record in records:
                name = record.get("Package", "").lower()
                if name and name not in by_package:
                    by_package[name] = record
            for package in chunk:
                self._control_cache[package] = by_package.get(
                    base_package_name(package)
                )

        for index in range(0, len(requested), 40):
            fetch(requested[index : index + 40])

    def _provider_names(self, virtual_package: str) -> list[str]:
        virtual_package = base_package_name(virtual_package)
        if virtual_package in self._provider_cache:
            return self._provider_cache[virtual_package]
        try:
            output = self._capture(["apt-cache", "showpkg", virtual_package])
        except MiloPKGError:
            output = ""
        providers: list[str] = []
        in_providers = False
        for line in output.splitlines():
            if line == "Reverse Provides: " or line == "Reverse Provides:":
                in_providers = True
                continue
            if not in_providers:
                continue
            if not line.strip():
                continue
            candidate = line.split(None, 1)[0].lower()
            if PACKAGE_RE.fullmatch(candidate) and candidate not in providers:
                providers.append(candidate)
        self._provider_cache[virtual_package] = providers
        return providers

    def _architecture_allows(self, alternative: str) -> bool:
        restrictions = re.findall(r"\[([^\]]+)\]", alternative)
        if not restrictions or not self._host_architecture:
            return True

        def matches(pattern: str) -> bool:
            pattern = pattern.lower()
            architecture = self._host_architecture.lower()
            if pattern in {"any", architecture}:
                return True
            if pattern == "linux-any":
                return sys.platform.startswith("linux")
            if pattern.startswith("any-"):
                return architecture == pattern[4:]
            return False

        for restriction in restrictions:
            tokens = restriction.split()
            positives = [item for item in tokens if not item.startswith("!")]
            negatives = [item[1:] for item in tokens if item.startswith("!")]
            if any(matches(item) for item in negatives):
                return False
            if positives and not any(matches(item) for item in positives):
                return False
        return True

    @staticmethod
    def _relation_alternatives(record: dict[str, str]) -> list[list[str]]:
        relations = []
        for field in ("Pre-Depends", "Depends"):
            value = record.get(field, "").replace("\n", " ")
            if value:
                relations.extend(value.split(","))
        return [
            [alternative.strip() for alternative in relation.split("|")]
            for relation in relations
            if relation.strip()
        ]

    @staticmethod
    def _relation_package_name(alternative: str) -> str:
        match = re.match(
            r"^\s*<?([a-z0-9][a-z0-9+.-]*(?::[a-z0-9-]+)?)>?",
            alternative.lower(),
        )
        if not match:
            return ""
        package = match.group(1)
        if package.endswith(":any") or package.endswith(":native"):
            package = package.rsplit(":", 1)[0]
        return package if PACKAGE_RE.fullmatch(package) else ""

    def dependencies(self, package: str) -> list[str]:
        package = normalize_package(package)
        selected_base = base_package_name(package)
        packages: list[str] = [package]
        seen = {package}
        queue: list[str] = [package]
        while queue:
            batch = queue[:32]
            del queue[:32]
            self._prefetch_controls(batch)
            records = [
                self._control_cache[current]
                for current in batch
                if self._control_cache.get(current)
            ]
            relation_names: list[str] = []
            for record in records:
                assert record is not None
                for alternatives in self._relation_alternatives(record):
                    relation_names.extend(
                        candidate
                        for candidate in (
                            self._relation_package_name(alternative)
                            for alternative in alternatives
                            if self._architecture_allows(alternative)
                        )
                        if candidate
                    )
            self._prefetch_controls(relation_names)

            for record in records:
                assert record is not None
                for alternatives in self._relation_alternatives(record):
                    chosen = ""
                    for alternative in alternatives:
                        if not self._architecture_allows(alternative):
                            continue
                        candidate = self._relation_package_name(alternative)
                        if not candidate:
                            continue
                        if self._control_record(candidate):
                            chosen = candidate
                            break
                        providers = self._provider_names(candidate)
                        self._prefetch_controls(providers)
                        for provider in providers:
                            if self._control_record(provider):
                                chosen = provider
                                break
                        if chosen:
                            break
                    if not chosen or chosen in seen:
                        continue
                    seen.add(chosen)
                    base = base_package_name(chosen)
                    chosen_record = self._control_record(chosen) or {}
                    if (
                        base in HOST_RUNTIME_PACKAGES
                        or chosen_record.get("Essential", "").lower() == "yes"
                    ) and base != selected_base:
                        continue
                    packages.append(chosen)
                    queue.append(chosen)
        return packages


ProgressCallback = Callable[[str, float, str], None]
LogCallback = Callable[[str], None]


class AppImageBuilder:
    """Build one AppImage in an isolated temporary directory."""

    def __init__(
        self,
        repository: Optional[AptRepository] = None,
        progress: Optional[ProgressCallback] = None,
        log: Optional[LogCallback] = None,
        cancel_event: Optional[threading.Event] = None,
    ) -> None:
        self.repository = repository or AptRepository()
        self.progress_callback = progress or (lambda _stage, _fraction, _text: None)
        self.log_callback = log or (lambda _text: None)
        self.cancel_event = cancel_event or threading.Event()
        self._active_process: Optional[subprocess.Popen[str]] = None

    def _progress(self, stage: str, fraction: float, text: str) -> None:
        self._check_cancelled()
        self.progress_callback(stage, max(0.0, min(1.0, fraction)), text)

    def _log(self, text: str) -> None:
        text = text.rstrip()
        if text:
            self.log_callback(text)

    def _check_cancelled(self) -> None:
        if self.cancel_event.is_set():
            raise BuildCancelled(
                localized("Conversión cancelada.", "Conversion cancelled.")
            )

    def _run(
        self,
        args: Sequence[str],
        *,
        cwd: Optional[Path] = None,
        env: Optional[dict[str, str]] = None,
        check: bool = True,
        log_output: bool = True,
    ) -> tuple[int, str]:
        self._check_cancelled()
        command_text = " ".join(shlex.quote(str(item)) for item in args)
        self._log(f"$ {command_text}")
        process_env = os.environ.copy()
        process_env.update({"LC_ALL": "C", "LANG": "C"})
        if env:
            process_env.update(env)
        try:
            process = subprocess.Popen(
                [str(item) for item in args],
                cwd=str(cwd) if cwd else None,
                env=process_env,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace",
                bufsize=1,
            )
        except OSError as error:
            raise MiloPKGError(str(error)) from error
        self._active_process = process
        selector = selectors.DefaultSelector()
        assert process.stdout is not None
        selector.register(process.stdout, selectors.EVENT_READ)
        collected: list[str] = []
        try:
            while True:
                if self.cancel_event.is_set():
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                    raise BuildCancelled(
                        localized("Conversión cancelada.", "Conversion cancelled.")
                    )
                for key, _mask in selector.select(timeout=0.2):
                    line = key.fileobj.readline()
                    if line:
                        collected.append(line)
                        if log_output:
                            self._log(line)
                if process.poll() is not None:
                    remainder = process.stdout.read()
                    if remainder:
                        collected.append(remainder)
                        if log_output:
                            for line in remainder.splitlines():
                                self._log(line)
                    break
        finally:
            selector.close()
            self._active_process = None
        return_code = process.returncode or 0
        output = "".join(collected)
        if check and return_code != 0:
            tail = "\n".join(output.strip().splitlines()[-8:])
            raise MiloPKGError(
                tail
                or localized(
                    f"El comando terminó con el código {return_code}.",
                    f"Command exited with status {return_code}.",
                )
            )
        return return_code, output

    def cancel(self) -> None:
        self.cancel_event.set()
        process = self._active_process
        if process and process.poll() is None:
            try:
                process.terminate()
            except OSError:
                pass

    def _download_packages(self, packages: Sequence[str], directory: Path) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        total = len(packages)
        for index in range(0, total, 24):
            chunk = list(packages[index : index + 24])
            fraction = 0.12 + 0.28 * min(1.0, index / max(1, total))
            self._progress(
                "download",
                fraction,
                localized(
                    f"Descargando paquetes ({min(index + len(chunk), total)}/{total})…",
                    f"Downloading packages ({min(index + len(chunk), total)}/{total})…",
                ),
            )
            code, _output = self._run(
                ["apt-get", "download", *chunk],
                cwd=directory,
                check=False,
            )
            if code == 0:
                continue
            # An alternative dependency can make a whole chunk report failure.
            # Retry individually so valid packages are not lost.
            for package in chunk:
                individual_code, individual_output = self._run(
                    ["apt-get", "download", package],
                    cwd=directory,
                    check=False,
                )
                if individual_code != 0 and package == packages[0]:
                    tail = "\n".join(individual_output.strip().splitlines()[-8:])
                    raise MiloPKGError(
                        tail
                        or localized(
                            f"No se pudo descargar {package}.",
                            f"Could not download {package}.",
                        )
                    )
                if individual_code != 0:
                    self._log(
                        localized(
                            f"Aviso: se omite la alternativa no descargable {package}.",
                            f"Warning: skipping unavailable alternative {package}.",
                        )
                    )
        if not any(directory.glob("*.deb")):
            raise MiloPKGError(
                localized(
                    "APT no descargó ningún paquete.",
                    "APT did not download any packages.",
                )
            )

    def _deb_package_name(self, deb: Path) -> str:
        _code, output = self._run(
            ["dpkg-deb", "-f", str(deb), "Package"], check=True
        )
        return output.strip().splitlines()[-1].lower()

    def _main_package_files(self, deb: Path) -> list[str]:
        _code, output = self._run(
            ["dpkg-deb", "-c", str(deb)], log_output=False
        )
        files: list[str] = []
        for line in output.splitlines():
            parts = line.split(maxsplit=5)
            if len(parts) != 6:
                continue
            path = parts[5].split(" -> ", 1)[0]
            if path.startswith("./"):
                path = path[2:]
            files.append(path)
        return files

    def _extract_packages(
        self, debs: Sequence[Path], appdir: Path, selected_package: str
    ) -> tuple[Path, list[str]]:
        appdir.mkdir(parents=True, exist_ok=True)
        selected_base = base_package_name(selected_package)
        main_deb: Optional[Path] = None
        package_names: dict[Path, str] = {}
        for deb in debs:
            try:
                name = self._deb_package_name(deb)
                package_names[deb] = name
                if name == selected_base:
                    main_deb = deb
            except MiloPKGError:
                continue
        if main_deb is None:
            raise MiloPKGError(
                localized(
                    f"No se encontró el archivo .deb principal de {selected_base}.",
                    f"The main .deb file for {selected_base} was not found.",
                )
            )
        main_files = self._main_package_files(main_deb)
        for index, deb in enumerate(debs):
            self._progress(
                "extract",
                0.43 + 0.25 * ((index + 1) / max(1, len(debs))),
                localized(
                    f"Extrayendo {package_names.get(deb, deb.name)}…",
                    f"Extracting {package_names.get(deb, deb.name)}…",
                ),
            )
            self._run(["dpkg-deb", "-x", str(deb), str(appdir)])
        self._make_internal_symlinks_relative(appdir)
        return main_deb, main_files

    @staticmethod
    def _make_internal_symlinks_relative(appdir: Path) -> None:
        for root, directories, files in os.walk(appdir, followlinks=False):
            for name in [*directories, *files]:
                path = Path(root) / name
                if not path.is_symlink():
                    continue
                try:
                    target = os.readlink(path)
                except OSError:
                    continue
                if not target.startswith("/"):
                    continue
                internal_target = appdir / target.lstrip("/")
                if not internal_target.exists() and not internal_target.is_symlink():
                    continue
                relative = os.path.relpath(internal_target, path.parent)
                try:
                    path.unlink()
                    path.symlink_to(relative)
                except OSError:
                    continue

    @staticmethod
    def _read_desktop(path: Path) -> Optional[dict[str, str]]:
        parser = configparser.ConfigParser(
            interpolation=None, strict=False, delimiters=("=",)
        )
        parser.optionxform = str
        try:
            parser.read(path, encoding="utf-8")
            if "Desktop Entry" not in parser:
                return None
            return dict(parser["Desktop Entry"])
        except (OSError, configparser.Error, UnicodeError):
            return None

    def _select_desktop(
        self, appdir: Path, package: str
    ) -> tuple[Optional[Path], dict[str, str]]:
        candidates: list[tuple[int, Path, dict[str, str]]] = []
        package_base = base_package_name(package)
        roots = (
            appdir / "usr/share/applications",
            appdir / "usr/local/share/applications",
            appdir / "share/applications",
        )
        for root in roots:
            if not root.is_dir():
                continue
            for path in root.glob("*.desktop"):
                entry = self._read_desktop(path)
                if not entry or entry.get("Type", "Application") != "Application":
                    continue
                if entry.get("Hidden", "").lower() == "true":
                    continue
                identifier = path.stem.lower()
                executable = self._exec_command(entry.get("Exec", ""))
                name = entry.get("Name", "").lower()
                score = 0
                if identifier == package_base:
                    score += 100
                elif package_base in identifier:
                    score += 55
                if executable == package_base:
                    score += 80
                elif package_base in executable:
                    score += 35
                if package_base.replace("-", " ") in name:
                    score += 25
                if entry.get("NoDisplay", "").lower() == "true":
                    score -= 80
                if entry.get("Terminal", "").lower() != "true":
                    score += 5
                candidates.append((score, path, entry))
        if not candidates:
            return None, {}
        _score, path, entry = max(candidates, key=lambda item: (item[0], str(item[1])))
        return path, entry

    @staticmethod
    def _exec_tokens(exec_line: str) -> list[str]:
        if not exec_line:
            return []
        try:
            return shlex.split(exec_line)
        except ValueError:
            return exec_line.split()

    @classmethod
    def _exec_command(cls, exec_line: str) -> str:
        tokens = cls._exec_tokens(exec_line)
        if not tokens:
            return ""
        index = 0
        if os.path.basename(tokens[0]) == "env":
            index = 1
            while index < len(tokens):
                token = tokens[index]
                if token == "--":
                    index += 1
                    break
                if token.startswith("-") or (
                    "=" in token and not token.startswith("/")
                ):
                    index += 1
                    continue
                break
        if index >= len(tokens):
            return ""
        return os.path.basename(tokens[index])

    @staticmethod
    def _candidate_executables(
        appdir: Path, package: str, main_files: Sequence[str]
    ) -> list[Path]:
        candidates: list[Path] = []
        package_base = base_package_name(package)
        executable_dirs = ("usr/bin/", "usr/sbin/", "usr/games/", "bin/", "sbin/")
        for relative in main_files:
            if not relative.startswith(executable_dirs):
                continue
            path = appdir / relative
            if path.exists() and not path.is_dir():
                candidates.append(path)
        return sorted(
            candidates,
            key=lambda path: (
                path.name != package_base,
                package_base not in path.name,
                "sbin" in path.parts,
                str(path),
            ),
        )

    @staticmethod
    def _find_command_path(appdir: Path, command: str) -> Optional[Path]:
        if not command:
            return None
        if command.startswith("/"):
            path = appdir / command.lstrip("/")
            return path if path.exists() and not path.is_dir() else None
        for directory in ("usr/bin", "usr/sbin", "usr/games", "bin", "sbin"):
            path = appdir / directory / command
            if path.exists() and not path.is_dir():
                return path
        return None

    def _launcher_argv(
        self,
        appdir: Path,
        desktop_entry: dict[str, str],
        package: str,
        main_files: Sequence[str],
    ) -> tuple[list[str], Path]:
        tokens = self._exec_tokens(desktop_entry.get("Exec", ""))
        command_index: Optional[int] = None
        if tokens:
            command_index = 0
            if os.path.basename(tokens[0]) == "env":
                command_index = 1
                while command_index < len(tokens):
                    token = tokens[command_index]
                    if token == "--":
                        command_index += 1
                        break
                    if token.startswith("-") or (
                        "=" in token and not token.startswith("/")
                    ):
                        command_index += 1
                        continue
                    break
            if command_index >= len(tokens):
                command_index = None

        executable: Optional[Path] = None
        if command_index is not None:
            executable = self._find_command_path(appdir, tokens[command_index])
        if executable is None:
            candidates = self._candidate_executables(appdir, package, main_files)
            if not candidates:
                raise MiloPKGError(
                    localized(
                        "El paquete no contiene una aplicación ejecutable. "
                        "Puede ser una biblioteca, un complemento o un paquete de datos.",
                        "The package does not contain an executable application. "
                        "It may be a library, plug-in or data package.",
                    )
                )
            executable = candidates[0]
            return [str(executable)], executable

        cleaned: list[str] = []
        for index, token in enumerate(tokens):
            if FIELD_CODE_RE.search(token):
                token = FIELD_CODE_RE.sub("", token)
                if not token:
                    continue
            token = token.replace("%%", "%")
            if index == command_index:
                cleaned.append(str(executable))
            else:
                cleaned.append(token)
        return cleaned, executable

    @staticmethod
    def _icon_candidates(appdir: Path, icon_value: str) -> list[Path]:
        icon_value = icon_value.strip()
        if not icon_value:
            return []
        icon_path = Path(icon_value)
        if icon_path.is_absolute():
            internal = appdir / str(icon_path).lstrip("/")
            return [internal] if internal.is_file() else []

        stem = icon_path.stem if icon_path.suffix.lower() in {".png", ".svg", ".xpm"} else icon_value
        filenames = [f"{stem}.svg", f"{stem}.png", f"{stem}.xpm"]
        candidates: list[Path] = []
        for root in (
            appdir / "usr/share/icons",
            appdir / "usr/local/share/icons",
            appdir / "usr/share/pixmaps",
            appdir / "share/icons",
        ):
            if not root.is_dir():
                continue
            if root.name == "pixmaps":
                for filename in filenames:
                    path = root / filename
                    if path.is_file():
                        candidates.append(path)
            else:
                for filename in filenames:
                    candidates.extend(path for path in root.rglob(filename) if path.is_file())
        return candidates

    @staticmethod
    def _icon_score(path: Path) -> tuple[int, int, int]:
        suffix_score = {".svg": 3, ".png": 2, ".xpm": 1}.get(path.suffix.lower(), 0)
        size = 0
        for part in path.parts:
            match = re.fullmatch(r"(\d+)x(\d+)", part)
            if match:
                size = max(size, int(match.group(1)) * int(match.group(2)))
        symbolic_penalty = -1 if "symbolic" in path.name else 0
        return suffix_score, size, symbolic_penalty

    def _install_icon(
        self, appdir: Path, desktop_entry: dict[str, str], icon_key: str
    ) -> Path:
        candidates = self._icon_candidates(appdir, desktop_entry.get("Icon", ""))
        if candidates:
            source = max(candidates, key=self._icon_score)
            suffix = source.suffix.lower()
            target = appdir / f"{icon_key}{suffix}"
            shutil.copy2(source, target)
        else:
            target = appdir / f"{icon_key}.svg"
            target.write_text(FALLBACK_ICON, encoding="utf-8")
        dir_icon = appdir / ".DirIcon"
        if dir_icon.exists() or dir_icon.is_symlink():
            dir_icon.unlink()
        dir_icon.symlink_to(target.name)
        return target

    @staticmethod
    def _desktop_field_code(exec_line: str) -> str:
        for code in ("%F", "%U", "%f", "%u"):
            if code in exec_line:
                return code
        return ""

    @staticmethod
    def _write_root_desktop(
        appdir: Path,
        original: dict[str, str],
        display_name: str,
        icon_key: str,
        package: PackageInfo,
    ) -> Path:
        allowed_exact = {
            "GenericName",
            "Comment",
            "Terminal",
            "Categories",
            "MimeType",
            "Keywords",
            "StartupNotify",
            "StartupWMClass",
        }
        lines = [
            "[Desktop Entry]",
            "Version=1.0",
            "Type=Application",
            f"Name={display_name}",
        ]
        for key, value in original.items():
            if (
                key in allowed_exact
                or key.startswith("Name[")
                or key.startswith("GenericName[")
                or key.startswith("Comment[")
                or key.startswith("Keywords[")
            ):
                if "\n" not in value and "\r" not in value:
                    lines.append(f"{key}={value}")
        code = AppImageBuilder._desktop_field_code(original.get("Exec", ""))
        lines.extend(
            [
                f"Exec=AppRun{' ' + code if code else ''}",
                f"Icon={icon_key}",
                f"Terminal={original.get('Terminal', 'false').lower()}",
                f"Categories={original.get('Categories', 'Utility;') or 'Utility;'}",
                "DBusActivatable=false",
                f"X-AppImage-Name={display_name}",
                f"X-AppImage-Version={package.version}",
                f"X-AppImage-Arch={package.architecture}",
            ]
        )
        # Keep one copy of keys that may have existed in the source entry.
        deduplicated: list[str] = []
        positions: dict[str, int] = {}
        for line in lines:
            key = line.split("=", 1)[0] if "=" in line else line
            if key in positions and key not in {"[Desktop Entry]"}:
                deduplicated[positions[key]] = line
            else:
                positions[key] = len(deduplicated)
                deduplicated.append(line)
        desktop_path = appdir / f"{icon_key}.desktop"
        desktop_path.write_text("\n".join(deduplicated) + "\n", encoding="utf-8")
        share_dir = appdir / "usr/share/applications"
        share_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(desktop_path, share_dir / desktop_path.name)
        return desktop_path

    @staticmethod
    def _apprun_argument(appdir: Path, token: str) -> str:
        try:
            internal_relative = Path(token).relative_to(appdir)
            return f'"$APPDIR/{internal_relative.as_posix()}"'
        except (ValueError, TypeError):
            pass
        if token.startswith("/"):
            internal = appdir / token.lstrip("/")
            if internal.exists() or internal.is_symlink():
                return f'"$APPDIR/{token.lstrip("/")}"'
        return shlex.quote(token)

    @staticmethod
    def _internal_runtime_library_dirs(
        appdir: Path,
        dynamic_section: str,
    ) -> list[str]:
        directories: list[str] = []
        for line in dynamic_section.splitlines():
            if "(RPATH)" not in line and "(RUNPATH)" not in line:
                continue
            match = re.search(r"\[([^\]]*)\]", line)
            if not match:
                continue
            for value in match.group(1).split(":"):
                if not value.startswith("/"):
                    continue
                internal = appdir / value.lstrip("/")
                if not internal.is_dir():
                    continue
                relative = internal.relative_to(appdir).as_posix()
                if relative not in directories:
                    directories.append(relative)
        return directories

    @classmethod
    def _runtime_library_dirs(
        cls,
        appdir: Path,
        executable: Optional[Path],
    ) -> list[str]:
        if executable is None or not executable.is_file():
            return []
        readelf = shutil.which("readelf")
        if not readelf:
            return []
        try:
            result = subprocess.run(
                [readelf, "-d", str(executable)],
                env={**os.environ, "LC_ALL": "C", "LANG": "C"},
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
                errors="replace",
                check=False,
            )
        except OSError:
            return []
        if result.returncode != 0:
            return []
        return cls._internal_runtime_library_dirs(appdir, result.stdout)

    def _write_apprun(
        self,
        appdir: Path,
        argv: Sequence[str],
        desktop_name: str = "",
        executable: Optional[Path] = None,
    ) -> None:
        triplet = multiarch_triplet()
        rendered = [self._apprun_argument(appdir, token) for token in argv]
        if not rendered:
            raise MiloPKGError(
                localized(
                    "No se pudo determinar el comando de inicio.",
                    "Could not determine the launch command.",
                )
            )
        command = " ".join(rendered)
        bundled_library_dirs = [
            f"$APPDIR/usr/lib/{triplet}",
            f"$APPDIR/lib/{triplet}",
            "$APPDIR/usr/lib",
            "$APPDIR/lib",
        ]
        bundled_library_dirs.extend(
            f"$APPDIR/{directory}"
            for directory in self._runtime_library_dirs(
                appdir,
                executable,
            )
        )
        library_path = ":".join(dict.fromkeys(bundled_library_dirs))
        script = f"""#!/bin/sh
set -eu

APPDIR="${{APPDIR:-$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)}}"
export APPDIR
export PATH="$APPDIR/usr/bin:$APPDIR/usr/sbin:$APPDIR/usr/games:$APPDIR/bin:$APPDIR/sbin:${{PATH:-/usr/bin:/bin}}"
export LD_LIBRARY_PATH="{library_path}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}"
export XDG_DATA_DIRS="$APPDIR/usr/local/share:$APPDIR/usr/share:${{XDG_DATA_DIRS:-/usr/local/share:/usr/share}}"
export GSETTINGS_SCHEMA_DIR="$APPDIR/usr/share/glib-2.0/schemas"
export GI_TYPELIB_PATH="$APPDIR/usr/lib/{triplet}/girepository-1.0:$APPDIR/usr/lib/girepository-1.0${{GI_TYPELIB_PATH:+:$GI_TYPELIB_PATH}}"
export GTK_PATH="$APPDIR/usr/lib/{triplet}/gtk-3.0:$APPDIR/usr/lib/gtk-3.0${{GTK_PATH:+:$GTK_PATH}}"
export QT_PLUGIN_PATH="$APPDIR/usr/lib/{triplet}/qt5/plugins:$APPDIR/usr/lib/{triplet}/qt6/plugins${{QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}}"
export QML2_IMPORT_PATH="$APPDIR/usr/lib/{triplet}/qt5/qml:$APPDIR/usr/lib/{triplet}/qt6/qml${{QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}}"
export PYTHONPATH="$APPDIR/usr/lib/python3/dist-packages:$APPDIR/usr/local/lib/python3/dist-packages${{PYTHONPATH:+:$PYTHONPATH}}"
case ":${{GTK_MODULES:-}}:" in
    *:appmenu-gtk-module:*) ;;
    *) export GTK_MODULES="appmenu-gtk-module${{GTK_MODULES:+:$GTK_MODULES}}" ;;
esac
export UBUNTU_MENUPROXY=1
export APPMENU_DISPLAY_BOTH=0
if [ -z "${{GIO_LAUNCHED_DESKTOP_FILE:-}}" ] && [ -n {shlex.quote(desktop_name)} ]; then
    export GIO_LAUNCHED_DESKTOP_FILE="$APPDIR/{desktop_name}"
    export GIO_LAUNCHED_DESKTOP_FILE_PID="$$"
fi
if [ -n "${{APPIMAGE:-}}" ]; then
    export MILO_APPIMAGE_PATH="$APPIMAGE"
fi

# GdkPixbuf's Debian cache contains absolute /usr paths.  Rebuild a small
# per-user cache with the paths of the mounted AppImage when the helper exists.
PIXBUF_DIR="$(find "$APPDIR/usr/lib" -type d -path '*/gdk-pixbuf-2.0/*/loaders' -print -quit 2>/dev/null || true)"
PIXBUF_QUERY="$(find "$APPDIR/usr/lib" -type f -name gdk-pixbuf-query-loaders -print -quit 2>/dev/null || true)"
if [ -n "$PIXBUF_DIR" ] && [ -x "$PIXBUF_QUERY" ]; then
    CACHE_ROOT="${{XDG_CACHE_HOME:-${{HOME:-/tmp}}/.cache}}/milopkg"
    mkdir -p "$CACHE_ROOT"
    CACHE_KEY="$(basename "${{APPIMAGE:-application}}")"
    CACHE_FILE="$CACHE_ROOT/gdk-pixbuf-$CACHE_KEY.cache"
    GDK_PIXBUF_MODULEDIR="$PIXBUF_DIR" "$PIXBUF_QUERY" > "$CACHE_FILE" 2>/dev/null || true
    if [ -s "$CACHE_FILE" ]; then
        export GDK_PIXBUF_MODULE_FILE="$CACHE_FILE"
    fi
fi

exec {command} "$@"
"""
        path = appdir / "AppRun"
        path.write_text(script, encoding="utf-8")
        path.chmod(0o755)

    def _compile_schemas(self, appdir: Path) -> None:
        schemas = appdir / "usr/share/glib-2.0/schemas"
        compiler = shutil.which("glib-compile-schemas")
        if schemas.is_dir() and compiler:
            self._run([compiler, str(schemas)], check=False)

    def _ensure_appimagetool(self) -> Path:
        installed = shutil.which("appimagetool")
        if installed:
            return Path(installed)
        arch = appimage_arch()
        cache_dir = Path(
            os.environ.get(
                "XDG_CACHE_HOME",
                str(Path.home() / ".cache"),
            )
        ) / "milopkg/tools"
        cache_dir.mkdir(parents=True, exist_ok=True)
        target = cache_dir / f"appimagetool-{arch}.AppImage"
        if target.is_file():
            try:
                with target.open("rb") as stream:
                    if stream.read(4) == b"\x7fELF":
                        target.chmod(target.stat().st_mode | stat.S_IXUSR)
                        return target
            except OSError:
                pass
            target.unlink(missing_ok=True)

        self._progress(
            "tool",
            0.76,
            localized(
                "Descargando appimagetool (solo la primera vez)…",
                "Downloading appimagetool (first use only)…",
            ),
        )
        url = APPIMAGETOOL_URL.format(arch=arch)
        temporary = target.with_suffix(".download")
        request = urllib.request.Request(
            url, headers={"User-Agent": f"miloPKG/{APP_VERSION}"}
        )
        try:
            with urllib.request.urlopen(request, timeout=45) as response, temporary.open(
                "wb"
            ) as destination:
                total = int(response.headers.get("Content-Length", "0") or 0)
                received = 0
                while True:
                    self._check_cancelled()
                    chunk = response.read(256 * 1024)
                    if not chunk:
                        break
                    destination.write(chunk)
                    received += len(chunk)
                    if total:
                        fraction = 0.76 + 0.08 * min(1.0, received / total)
                        self._progress(
                            "tool",
                            fraction,
                            localized(
                                f"Descargando appimagetool ({human_size(received)})…",
                                f"Downloading appimagetool ({human_size(received)})…",
                            ),
                        )
            with temporary.open("rb") as stream:
                if stream.read(4) != b"\x7fELF":
                    raise MiloPKGError(
                        localized(
                            "La descarga de appimagetool no es un ejecutable válido.",
                            "The appimagetool download is not a valid executable.",
                        )
                    )
            temporary.chmod(0o755)
            os.replace(temporary, target)
        except BuildCancelled:
            temporary.unlink(missing_ok=True)
            raise
        except (OSError, urllib.error.URLError) as error:
            temporary.unlink(missing_ok=True)
            raise MiloPKGError(
                localized(
                    f"No se pudo descargar appimagetool desde GitHub: {error}",
                    f"Could not download appimagetool from GitHub: {error}",
                )
            ) from error
        return target

    def build(
        self,
        package_name: str,
        output_directory: Path,
        *,
        overwrite: bool = False,
    ) -> BuildResult:
        package_name = normalize_package(package_name)
        output_directory = Path(output_directory).expanduser().resolve()
        output_directory.mkdir(parents=True, exist_ok=True)
        if not output_directory.is_dir():
            raise MiloPKGError(
                localized(
                    "La ubicación de salida no es una carpeta.",
                    "The output location is not a directory.",
                )
            )
        if not os.access(output_directory, os.W_OK):
            raise MiloPKGError(
                localized(
                    "No se puede escribir en la carpeta de salida.",
                    "The output directory is not writable.",
                )
            )

        self._progress(
            "metadata",
            0.02,
            localized(
                "Leyendo la información del paquete…",
                "Reading package information…",
            ),
        )
        info = self.repository.details(package_name)
        dependencies = self.repository.dependencies(package_name)
        self._log(
            f"{info.package} {info.version} ({info.architecture}); "
            + localized(
                f"{len(dependencies)} paquetes para incluir.",
                f"{len(dependencies)} packages to bundle.",
            )
        )
        self._progress(
            "resolve",
            0.08,
            localized(
                f"Dependencias resueltas: {len(dependencies)} paquetes.",
                f"Dependencies resolved: {len(dependencies)} packages.",
            ),
        )

        with tempfile.TemporaryDirectory(
            prefix=".milopkg-build-", dir=str(output_directory)
        ) as temporary_directory:
            work = Path(temporary_directory)
            deb_dir = work / "packages"
            appdir = work / f"{safe_icon_key(package_name)}.AppDir"
            self._download_packages(dependencies, deb_dir)
            debs = sorted(deb_dir.glob("*.deb"))
            _main_deb, main_files = self._extract_packages(
                debs, appdir, package_name
            )

            self._progress(
                "prepare",
                0.70,
                localized(
                    "Detectando lanzador e icono…",
                    "Detecting launcher and icon…",
                ),
            )
            desktop_path, desktop_entry = self._select_desktop(
                appdir, package_name
            )
            if desktop_path is None:
                desktop_entry = {
                    "Terminal": "true",
                    "Categories": "Utility;",
                    "Comment": info.description,
                }
            launcher_argv, executable = self._launcher_argv(
                appdir, desktop_entry, package_name, main_files
            )
            display_name = clean_display_name(
                desktop_entry.get("Name", ""),
                base_package_name(package_name),
                info.version,
            )
            icon_key = safe_icon_key(package_name)
            self._install_icon(appdir, desktop_entry, icon_key)
            self._write_apprun(
                appdir,
                launcher_argv,
                f"{icon_key}.desktop",
                executable,
            )
            self._write_root_desktop(
                appdir, desktop_entry, display_name, icon_key, info
            )
            self._compile_schemas(appdir)

            target = output_directory / safe_output_filename(display_name)
            if target.exists() and not overwrite:
                raise OutputExists(f"{tr('existing')}\n\n{target}")

            tool = self._ensure_appimagetool()
            staged_output = work / "result.appimage"
            self._progress(
                "package",
                0.85,
                localized("Comprimiendo el AppImage…", "Compressing AppImage…"),
            )
            tool_env = {
                "ARCH": appimage_arch(),
                "APPIMAGE_EXTRACT_AND_RUN": "1",
                "APPIMAGETOOL_APP_NAME": display_name,
                "NO_STRIP": "1",
            }
            self._run(
                [str(tool), str(appdir), str(staged_output)],
                cwd=work,
                env=tool_env,
            )
            if not staged_output.is_file() or staged_output.stat().st_size < 65536:
                raise MiloPKGError(
                    localized(
                        "appimagetool no produjo un archivo AppImage válido.",
                        "appimagetool did not produce a valid AppImage file.",
                    )
                )
            with staged_output.open("rb") as stream:
                if stream.read(4) != b"\x7fELF":
                    raise MiloPKGError(
                        localized(
                            "El resultado no contiene un runtime AppImage.",
                            "The output does not contain an AppImage runtime.",
                        )
                    )
            staged_output.chmod(0o755)
            os.replace(staged_output, target)
            target.chmod(0o755)

        self._progress(
            "done",
            1.0,
            localized(f"Creado: {target.name}", f"Created: {target.name}"),
        )
        return BuildResult(
            path=target,
            display_name=display_name,
            package=info.package,
            version=info.version,
            bundled_packages=len(dependencies),
        )


def run_gui(initial_query: str = "", output: Optional[Path] = None) -> int:
    try:
        import gi

        gi.require_version("Gtk", "3.0")
        gi.require_version("Gdk", "3.0")
        from gi.repository import Gdk, GLib, Gtk
    except (ImportError, ValueError) as error:
        raise MiloPKGError(
            localized(
                f"No se pudo iniciar GTK 3. Instala python3-gi y gir1.2-gtk-3.0: {error}",
                f"Could not start GTK 3. Install python3-gi and gir1.2-gtk-3.0: {error}",
            )
        ) from error

    class MiloPKGWindow(Gtk.Window):
        def __init__(self) -> None:
            super().__init__(title=tr("window_title"))
            self.set_default_size(860, 650)
            self.set_position(Gtk.WindowPosition.CENTER)
            self.set_border_width(22)
            self.set_icon_name("milopkg")
            self.connect("destroy", self.on_destroy)
            self.repository = AptRepository()
            self.selected: Optional[PackageInfo] = None
            self.output_directory = Path(output or Path.home())
            self.builder: Optional[AppImageBuilder] = None
            self.cancel_event: Optional[threading.Event] = None
            self.busy = False
            self._apply_css()
            self._build_ui()
            if initial_query:
                self.search_entry.set_text(initial_query)
                GLib.idle_add(self.on_search)

        def _apply_css(self) -> None:
            settings = Gtk.Settings.get_default()
            theme = settings.get_property("gtk-theme-name") if settings else ""
            dark = theme == "miloOS-Dark"
            background = "#1e1e1e" if dark else "#f1f2f6"
            card = "#2b2b2b" if dark else "#ffffff"
            border = "#3d3d3d" if dark else "#dfe2e8"
            text = "#f5f6fa" if dark else "#243042"
            muted = "#a0a0a0" if dark else "#6f7785"
            css = f"""
                window {{ background: {background}; }}
                .hero-title {{ color: {text}; font-size: 23px; font-weight: 700; }}
                .hero-subtitle {{ color: {muted}; font-size: 13px; }}
                .card {{
                    background: {card};
                    border: 1px solid {border};
                    border-radius: 10px;
                }}
                .status {{ color: {muted}; font-size: 12px; }}
                .primary {{
                    color: white;
                    background: #007aff;
                    border-color: #007aff;
                    border-radius: 8px;
                    font-weight: 600;
                    padding: 7px 18px;
                }}
                .primary:hover {{ background: #006be0; }}
                button {{ border-radius: 8px; padding: 7px 13px; }}
                entry {{ border-radius: 8px; padding: 8px; }}
                treeview {{ background: {card}; color: {text}; }}
                textview {{ background: {card}; color: {text}; }}
            """
            provider = Gtk.CssProvider()
            provider.load_from_data(css.encode("utf-8"))
            Gtk.StyleContext.add_provider_for_screen(
                Gdk.Screen.get_default(),
                provider,
                Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION,
            )

        def _build_ui(self) -> None:
            root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
            self.add(root)

            title = Gtk.Label(label=tr("title"))
            title.set_halign(Gtk.Align.START)
            title.get_style_context().add_class("hero-title")
            root.pack_start(title, False, False, 0)
            subtitle = Gtk.Label(label=tr("subtitle"))
            subtitle.set_halign(Gtk.Align.START)
            subtitle.get_style_context().add_class("hero-subtitle")
            root.pack_start(subtitle, False, False, 0)

            search_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=9)
            self.search_entry = Gtk.SearchEntry()
            self.search_entry.set_placeholder_text(tr("search_placeholder"))
            self.search_entry.connect("activate", self.on_search)
            search_box.pack_start(self.search_entry, True, True, 0)
            self.search_button = Gtk.Button(label=tr("search"))
            self.search_button.connect("clicked", self.on_search)
            search_box.pack_start(self.search_button, False, False, 0)
            root.pack_start(search_box, False, False, 0)

            self.store = Gtk.ListStore(str, str, str, str, object)
            self.tree = Gtk.TreeView(model=self.store)
            self.tree.set_headers_visible(True)
            self.tree.get_selection().connect("changed", self.on_selection_changed)
            self.tree.connect("row-activated", self.on_row_activated)
            columns = (
                (tr("package"), 0, 180),
                (tr("version"), 1, 125),
                (tr("architecture"), 2, 90),
                (tr("description"), 3, 360),
            )
            for heading, model_column, width in columns:
                renderer = Gtk.CellRendererText()
                renderer.set_property("ellipsize", 3)
                column = Gtk.TreeViewColumn(
                    heading, renderer, text=model_column
                )
                column.set_resizable(True)
                column.set_min_width(width)
                if model_column == 3:
                    column.set_expand(True)
                self.tree.append_column(column)
            scrolled = Gtk.ScrolledWindow()
            scrolled.set_policy(
                Gtk.PolicyType.AUTOMATIC, Gtk.PolicyType.AUTOMATIC
            )
            scrolled.set_vexpand(True)
            scrolled.get_style_context().add_class("card")
            scrolled.add(self.tree)
            root.pack_start(scrolled, True, True, 0)

            self.selection_label = Gtk.Label(label=tr("select_package"))
            self.selection_label.set_halign(Gtk.Align.START)
            self.selection_label.set_ellipsize(3)
            self.selection_label.get_style_context().add_class("status")
            root.pack_start(self.selection_label, False, False, 0)

            destination = Gtk.Box(
                orientation=Gtk.Orientation.HORIZONTAL, spacing=10
            )
            output_label = Gtk.Label(label=f"{tr('output')}:")
            destination.pack_start(output_label, False, False, 0)
            self.output_label = Gtk.Label(
                label=str(self.output_directory)
            )
            self.output_label.set_halign(Gtk.Align.START)
            self.output_label.set_ellipsize(3)
            destination.pack_start(self.output_label, True, True, 0)
            self.choose_button = Gtk.Button(label=tr("choose"))
            self.choose_button.connect("clicked", self.on_choose_output)
            destination.pack_start(self.choose_button, False, False, 0)
            root.pack_start(destination, False, False, 0)

            self.progress = Gtk.ProgressBar()
            self.progress.set_show_text(True)
            self.progress.set_text(tr("ready"))
            root.pack_start(self.progress, False, False, 0)

            expander = Gtk.Expander(label=tr("log"))
            log_scrolled = Gtk.ScrolledWindow()
            log_scrolled.set_size_request(-1, 105)
            self.log_view = Gtk.TextView()
            self.log_view.set_editable(False)
            self.log_view.set_cursor_visible(False)
            self.log_view.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
            log_scrolled.add(self.log_view)
            expander.add(log_scrolled)
            root.pack_start(expander, False, False, 0)

            buttons = Gtk.Box(
                orientation=Gtk.Orientation.HORIZONTAL, spacing=9
            )
            buttons.set_halign(Gtk.Align.END)
            self.cancel_button = Gtk.Button(label=tr("cancel"))
            self.cancel_button.set_no_show_all(True)
            self.cancel_button.connect("clicked", self.on_cancel)
            buttons.pack_start(self.cancel_button, False, False, 0)
            self.convert_button = Gtk.Button(label=tr("convert"))
            self.convert_button.set_sensitive(False)
            self.convert_button.get_style_context().add_class("primary")
            self.convert_button.connect("clicked", self.on_convert)
            buttons.pack_start(self.convert_button, False, False, 0)
            root.pack_start(buttons, False, False, 0)

        def set_busy(self, busy: bool, cancellable: bool = False) -> None:
            self.busy = busy
            self.search_entry.set_sensitive(not busy)
            self.search_button.set_sensitive(not busy)
            self.tree.set_sensitive(not busy)
            self.choose_button.set_sensitive(not busy)
            self.convert_button.set_sensitive(not busy and self.selected is not None)
            if busy and cancellable:
                self.cancel_button.show()
            else:
                self.cancel_button.hide()

        def on_search(self, _widget=None) -> None:
            if self.busy:
                return
            query = self.search_entry.get_text().strip()
            if not query:
                return
            self.set_busy(True)
            self.store.clear()
            self.selected = None
            self.progress.set_fraction(0.0)
            self.progress.set_text(tr("searching"))

            def worker() -> None:
                try:
                    results = self.repository.search(query)
                    GLib.idle_add(self.finish_search, results, None)
                except Exception as error:
                    GLib.idle_add(self.finish_search, [], error)

            threading.Thread(target=worker, daemon=True).start()

        def finish_search(
            self, results: list[PackageInfo], error: Optional[Exception]
        ) -> bool:
            self.set_busy(False)
            if error:
                self.progress.set_text(tr("ready"))
                self.show_error(f"{tr('error_search')}\n\n{error}")
                return False
            for info in results:
                self.store.append(
                    [
                        info.package,
                        info.version,
                        info.architecture,
                        info.description,
                        info,
                    ]
                )
            if results:
                self.progress.set_text(tr("results", count=len(results)))
                first = self.store.get_iter_first()
                if first:
                    self.tree.get_selection().select_iter(first)
            else:
                self.progress.set_text(tr("no_results"))
            return False

        def on_selection_changed(self, selection) -> None:
            model, tree_iter = selection.get_selected()
            if tree_iter is None:
                self.selected = None
                self.selection_label.set_text(tr("select_package"))
            else:
                self.selected = model.get_value(tree_iter, 4)
                assert self.selected is not None
                size = human_size(self.selected.installed_size_kib * 1024)
                self.selection_label.set_text(
                    tr(
                        "selected",
                        package=self.selected.package,
                        version=self.selected.version or "—",
                        size=size,
                    )
                )
            self.convert_button.set_sensitive(
                not self.busy and self.selected is not None
            )

        def on_row_activated(self, _tree, _path, _column) -> None:
            if self.selected and not self.busy:
                self.on_convert()

        def on_choose_output(self, _button) -> None:
            dialog = Gtk.FileChooserDialog(
                title=tr("output"),
                parent=self,
                action=Gtk.FileChooserAction.SELECT_FOLDER,
            )
            dialog.add_buttons(
                tr("cancel"),
                Gtk.ResponseType.CANCEL,
                tr("choose"),
                Gtk.ResponseType.OK,
            )
            dialog.set_current_folder(str(self.output_directory))
            if dialog.run() == Gtk.ResponseType.OK:
                selected = dialog.get_filename()
                if selected:
                    self.output_directory = Path(selected)
                    self.output_label.set_text(selected)
            dialog.destroy()

        def append_log(self, message: str) -> bool:
            buffer = self.log_view.get_buffer()
            end = buffer.get_end_iter()
            buffer.insert(end, message.rstrip() + "\n")
            return False

        def update_progress(
            self, _stage: str, fraction: float, message: str
        ) -> bool:
            self.progress.set_fraction(fraction)
            self.progress.set_text(message)
            return False

        def on_convert(self, _button=None) -> None:
            if self.busy or self.selected is None:
                return
            package = self.selected.package
            self.cancel_event = threading.Event()
            self.log_view.get_buffer().set_text("")
            self.set_busy(True, cancellable=True)
            self.progress.set_fraction(0.0)
            self.progress.set_text(tr("working"))
            self.builder = AppImageBuilder(
                repository=self.repository,
                progress=lambda stage, fraction, message: GLib.idle_add(
                    self.update_progress, stage, fraction, message
                ),
                log=lambda message: GLib.idle_add(self.append_log, message),
                cancel_event=self.cancel_event,
            )

            def worker() -> None:
                assert self.builder is not None
                try:
                    result = self.builder.build(
                        package, self.output_directory, overwrite=False
                    )
                    GLib.idle_add(self.finish_build, result, None)
                except Exception as error:
                    GLib.idle_add(self.finish_build, None, error)

            # A non-daemon build worker gets a chance to remove its temporary
            # AppDir even when the window is closed while packaging.
            threading.Thread(target=worker, daemon=False).start()

        def on_cancel(self, _button) -> None:
            if self.builder:
                self.builder.cancel()
            self.progress.set_text(tr("cancel"))

        def finish_build(
            self,
            result: Optional[BuildResult],
            error: Optional[Exception],
        ) -> bool:
            self.builder = None
            self.cancel_event = None
            self.set_busy(False)
            if error:
                self.progress.set_fraction(0.0)
                self.progress.set_text(tr("ready"))
                if not isinstance(error, BuildCancelled):
                    self.show_error(str(error))
                return False
            assert result is not None
            self.progress.set_fraction(1.0)
            self.progress.set_text(result.path.name)
            dialog = Gtk.MessageDialog(
                transient_for=self,
                modal=True,
                message_type=Gtk.MessageType.INFO,
                buttons=Gtk.ButtonsType.NONE,
                text=tr("success_title"),
            )
            dialog.format_secondary_text(
                f"{tr('success', name=result.path.name)}\n\n{result.path}"
            )
            dialog.add_button(tr("close"), Gtk.ResponseType.CLOSE)
            dialog.add_button(tr("open_folder"), 1)
            response = dialog.run()
            dialog.destroy()
            if response == 1:
                try:
                    subprocess.Popen(
                        ["milofiles", str(result.path.parent)],
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                except OSError:
                    pass
            return False

        def show_error(self, message: str) -> None:
            dialog = Gtk.MessageDialog(
                transient_for=self,
                modal=True,
                message_type=Gtk.MessageType.ERROR,
                buttons=Gtk.ButtonsType.CLOSE,
                text=tr("error_title"),
            )
            dialog.format_secondary_text(message)
            dialog.run()
            dialog.destroy()

        def on_destroy(self, _widget) -> None:
            if self.builder:
                self.builder.cancel()
            Gtk.main_quit()

    window = MiloPKGWindow()
    window.show_all()
    window.cancel_button.hide()
    Gtk.main()
    return 0


def select_cli_package(repository: AptRepository, query: str) -> PackageInfo:
    while not query.strip():
        try:
            query = input(
                localized("Aplicación o paquete: ", "Application or package: ")
            ).strip()
        except EOFError as error:
            raise MiloPKGError(
                localized(
                    "No se indicó ningún paquete.",
                    "No package was specified.",
                )
            ) from error
    results = repository.search(query, limit=40)
    if not results:
        raise MiloPKGError(
            localized(
                "No se encontró ningún paquete.",
                "No packages were found.",
            )
        )
    print()
    for index, info in enumerate(results, 1):
        version = f" [{info.version}]" if info.version else ""
        print(f"{index:2}. {info.package}{version}")
        if info.description:
            print(f"    {info.description}")
    print()
    while True:
        try:
            raw = input(
                localized(
                    f"Elige un paquete [1-{len(results)}]: ",
                    f"Choose a package [1-{len(results)}]: ",
                )
            ).strip()
        except EOFError as error:
            raise MiloPKGError(
                localized(
                    "No se eligió ningún paquete.",
                    "No package was selected.",
                )
            ) from error
        if raw.isdigit() and 1 <= int(raw) <= len(results):
            return results[int(raw) - 1]
        print(localized("Selección no válida.", "Invalid selection."))


def run_cli(
    query: str,
    direct_package: str,
    output_directory: Path,
    overwrite: bool,
) -> int:
    repository = AptRepository()
    info = (
        repository.details(direct_package)
        if direct_package
        else select_cli_package(repository, query)
    )
    print(
        localized(
            f"\nCreando AppImage de {info.package} {info.version}"
            f" en {output_directory}…\n",
            f"\nCreating AppImage for {info.package} {info.version}"
            f" in {output_directory}…\n",
        )
    )
    last_message = ""

    def progress(_stage: str, fraction: float, message: str) -> None:
        nonlocal last_message
        if message != last_message:
            print(f"[{fraction * 100:3.0f}%] {message}")
            last_message = message

    builder = AppImageBuilder(
        repository=repository,
        progress=progress,
        log=lambda message: print(f"      {message}"),
    )
    try:
        result = builder.build(
            info.package, output_directory, overwrite=overwrite
        )
    except KeyboardInterrupt:
        builder.cancel()
        raise BuildCancelled(
            localized("Conversión cancelada.", "Conversion cancelled.")
        )
    print(localized(f"\nListo: {result.path}", f"\nReady: {result.path}"))
    return 0


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="milopkg",
        description=localized(
            "Busca aplicaciones en los repositorios Debian y las convierte "
            "en archivos AppImage portátiles.",
            "Search Debian repositories and turn applications into portable "
            "AppImage files.",
        ),
    )
    parser.add_argument(
        "query",
        nargs="?",
        default="",
        help=localized(
            "texto que se buscará en el repositorio",
            "text to search for in the repository",
        ),
    )
    parser.add_argument(
        "--cli",
        action="store_true",
        help=localized(
            "usar la interfaz interactiva de terminal",
            "use the interactive terminal interface",
        ),
    )
    parser.add_argument(
        "--package",
        default="",
        metavar="PAQUETE",
        help=localized(
            "convertir directamente este nombre exacto de paquete",
            "convert this exact package name directly",
        ),
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=None,
        metavar="CARPETA",
        help=localized(
            "carpeta de destino (inicio en modo gráfico, actual en modo CLI)",
            "destination directory (home in GUI mode, current in CLI mode)",
        ),
    )
    parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help=localized(
            "reemplazar un AppImage existente con el mismo nombre",
            "replace an existing AppImage with the same name",
        ),
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"%(prog)s {APP_VERSION}",
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_argument_parser().parse_args(argv)
    graphical_session = bool(
        os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")
    )
    try:
        if args.cli or args.package or not graphical_session:
            output = (args.output or Path.cwd()).expanduser()
            return run_cli(args.query, args.package, output, args.force)
        return run_gui(args.query, args.output)
    except BuildCancelled as error:
        print(str(error), file=sys.stderr)
        return 130
    except MiloPKGError as error:
        print(f"milopkg: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
