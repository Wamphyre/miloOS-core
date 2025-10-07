#!/bin/bash
# Script para reparar el audio de PipeWire después de configuración incorrecta

echo "Reparando configuración de audio..."

# Eliminar configuración problemática de WirePlumber
rm -f ~/.config/wireplumber/main.lua.d/99-device-config.lua

# Reiniciar servicios de audio
systemctl --user restart pipewire
systemctl --user restart pipewire-pulse
systemctl --user restart wireplumber

echo "Audio reparado. Espera unos segundos para que los servicios se reinicien."
