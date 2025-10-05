# miloOS Audio Configuration Tool

Simple graphical tool to configure PipeWire parameters.

## Features

- Sample rate selection (44.1kHz - 192kHz)
- Buffer size configuration (32 - 1024 samples)
- Default input/output device selection
- Simple and clear graphical interface
- Automatic PipeWire restart after applying changes

## Requirements

- Python 3
- GTK 3
- PipeWire
- PulseAudio compatibility layer (pactl)

```bash
sudo apt install python3-gi gir1.2-gtk-3.0 pipewire pipewire-pulse
```

## Installation

```bash
sudo ./install.sh
```

## Usage

Run from the applications menu or from terminal:

```bash
audio-config
```

## Configuration

The tool creates/modifies the file:
- `~/.config/pipewire/pipewire.conf.d/99-custom.conf`

Changes are applied automatically by restarting PipeWire when you click Apply.

## Uninstall

```bash
sudo rm /usr/local/bin/audio-config
sudo rm /usr/share/applications/audio-config.desktop
sudo update-desktop-database /usr/share/applications/
```

---

# Herramienta de Configuración de Audio miloOS

Herramienta gráfica simple para configurar parámetros de PipeWire.

## Características

- Selección de frecuencia de muestreo (44.1kHz - 192kHz)
- Configuración de tamaño de buffer (32 - 1024 samples)
- Selección de dispositivos de entrada/salida por defecto
- Interfaz gráfica simple y clara
- Reinicio automático de PipeWire al aplicar cambios

## Requisitos

- Python 3
- GTK 3
- PipeWire
- Capa de compatibilidad PulseAudio (pactl)

```bash
sudo apt install python3-gi gir1.2-gtk-3.0 pipewire pipewire-pulse
```

## Instalación

```bash
sudo ./install.sh
```

## Uso

Ejecutar desde el menú de aplicaciones o desde terminal:

```bash
audio-config
```

## Configuración

La herramienta crea/modifica el archivo:
- `~/.config/pipewire/pipewire.conf.d/99-custom.conf`

Los cambios se aplican automáticamente reiniciando PipeWire al hacer clic en Aplicar.

## Desinstalación

```bash
sudo rm /usr/local/bin/audio-config
sudo rm /usr/share/applications/audio-config.desktop
sudo update-desktop-database /usr/share/applications/
```
