# Design Document

## Overview

Este documento describe el diseño técnico del sistema de creación de ISO instalable para miloOS. El sistema utilizará herramientas estándar de Linux (debootstrap, squashfs-tools, xorriso, GRUB) para crear una imagen ISO booteable que incluya un sistema LiveCD con usuario predefinido y el instalador Calamares configurado para preservar todas las configuraciones de miloOS.

El diseño se basa en el enfoque utilizado por distribuciones como Ubuntu, Debian Live y Linux Mint, adaptado específicamente para las necesidades de miloOS como distribución de producción de audio profesional.

## Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                  make-miloOS-release.sh                     │
│                    (Main Orchestrator)                      │
└────────────────────────┬────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────┐
         │               │               │
         ▼               ▼               ▼
┌────────────────┐ ┌──────────────┐ ┌──────────────┐
│  System Copy   │ │  Live System │ │  Calamares   │
│    Module      │ │    Module    │ │    Module    │
└────────┬───────┘ └──────┬───────┘ └──────┬───────┘
         │                │                │
         ▼                ▼                ▼
┌────────────────────────────────────────────────────┐
│              ISO Build Module                      │
│  (squashfs + GRUB + xorriso)                      │
└────────────────────────────────────────────────────┘
```

### Component Breakdown

1. **System Copy Module**: Copia el sistema actual a un directorio temporal
2. **Live System Module**: Configura el usuario live y scripts de arranque
3. **Calamares Module**: Configura el instalador con branding y hooks
4. **ISO Build Module**: Crea el filesystem comprimido y la imagen ISO booteable


## Components and Interfaces

### 1. Main Script (make-miloOS-release.sh)

**Responsabilidad**: Orquestar todo el proceso de creación de ISO

**Funciones principales**:
- `check_dependencies()`: Verificar herramientas necesarias
- `check_root()`: Verificar ejecución como root
- `setup_workspace()`: Crear directorios de trabajo temporales
- `copy_system()`: Copiar sistema actual
- `configure_live_system()`: Configurar usuario y arranque live
- `install_calamares()`: Instalar y configurar Calamares
- `build_iso()`: Crear imagen ISO
- `cleanup()`: Limpiar archivos temporales
- `validate_iso()`: Validar ISO creada

**Variables de entorno**:
```bash
WORK_DIR="/tmp/miloOS-build-$$"
CHROOT_DIR="$WORK_DIR/chroot"
ISO_DIR="$WORK_DIR/iso"
SQUASHFS_DIR="$ISO_DIR/live"
VERSION="1.0"
ISO_NAME="miloOS-${VERSION}-amd64.iso"
```

### 2. System Copy Module

**Propósito**: Crear una copia limpia del sistema actual

**Pre-requisitos**: Antes de copiar, el sistema debe tener todas las configuraciones en /etc/skel

**Preparación de /etc/skel**:
```bash
# Copiar configuraciones de usuario actual a /etc/skel
prepare_skel() {
    local SOURCE_USER="$SUDO_USER"
    local SOURCE_HOME="/home/$SOURCE_USER"
    
    # Crear estructura de /etc/skel
    mkdir -p /etc/skel/.config/{xfce4,plank,gtk-3.0,fontconfig,menus,autostart,environment.d,systemd/user.conf.d}
    mkdir -p /etc/skel/.local/share/applications
    
    # Copiar configuraciones XFCE4
    cp -R "$SOURCE_HOME/.config/xfce4"/* /etc/skel/.config/xfce4/
    
    # Copiar configuraciones Plank
    cp -R "$SOURCE_HOME/.config/plank"/* /etc/skel/.config/plank/
    
    # Copiar configuraciones GTK
    cp -R "$SOURCE_HOME/.config/gtk-3.0"/* /etc/skel/.config/gtk-3.0/
    cp "$SOURCE_HOME/.gtkrc-2.0" /etc/skel/ 2>/dev/null || true
    
    # Copiar configuraciones de fuentes
    cp -R "$SOURCE_HOME/.config/fontconfig"/* /etc/skel/.config/fontconfig/ 2>/dev/null || true
    
    # Copiar autostart
    cp -R "$SOURCE_HOME/.config/autostart"/* /etc/skel/.config/autostart/ 2>/dev/null || true
    
    # Copiar menús personalizados
    cp -R "$SOURCE_HOME/.config/menus"/* /etc/skel/.config/menus/ 2>/dev/null || true
    
    # Copiar aplicaciones ocultas
    cp -R "$SOURCE_HOME/.local/share/applications"/* /etc/skel/.local/share/applications/ 2>/dev/null || true
    
    # Copiar shell configs
    cp "$SOURCE_HOME/.profile" /etc/skel/
    cp "$SOURCE_HOME/.bashrc" /etc/skel/
    cp "$SOURCE_HOME/.xsession" /etc/skel/ 2>/dev/null || true
    cp "$SOURCE_HOME/.xsessionrc" /etc/skel/ 2>/dev/null || true
    
    # Copiar environment.d
    cp -R "$SOURCE_HOME/.config/environment.d"/* /etc/skel/.config/environment.d/ 2>/dev/null || true
    
    # Copiar systemd user configs
    cp -R "$SOURCE_HOME/.config/systemd/user.conf.d"/* /etc/skel/.config/systemd/user.conf.d/ 2>/dev/null || true
    
    # Copiar xinitrc.d scripts
    mkdir -p /etc/skel/.config/xfce4/xinitrc.d
    cp -R "$SOURCE_HOME/.config/xfce4/xinitrc.d"/* /etc/skel/.config/xfce4/xinitrc.d/ 2>/dev/null || true
    
    # Limpiar datos personales de /etc/skel
    find /etc/skel -name "*history" -delete
    find /etc/skel -name "*.log" -delete
    find /etc/skel -name "*.cache" -type f -delete
    
    log_info "/etc/skel prepared with user configurations"
}
```

**Método**: Usar `rsync` para copiar el sistema excluyendo directorios temporales

**Exclusiones**:
- `/proc`, `/sys`, `/dev`, `/run`
- `/tmp`, `/var/tmp`
- `/home/*` (excepto configuraciones de /etc/skel)
- `/root`
- Archivos de swap
- Directorios de build temporales

**Comando base**:
```bash
rsync -aAXv --exclude={'/dev/*','/proc/*','/sys/*','/tmp/*','/run/*','/mnt/*','/media/*','/lost+found','/home/*','/root/*'} / "$CHROOT_DIR/"
```

**Verificación de aplicaciones miloOS**:
```bash
verify_miloOS_apps() {
    local errors=0
    
    # Verificar AudioConfig
    if [ ! -f "$CHROOT_DIR/usr/local/bin/audio-config" ]; then
        log_error "AudioConfig binary not found"
        errors=$((errors + 1))
    fi
    
    if [ ! -f "$CHROOT_DIR/usr/share/applications/audio-config.desktop" ]; then
        log_error "AudioConfig desktop file not found"
        errors=$((errors + 1))
    fi
    
    # Verificar menús miloOS
    if [ ! -f "$CHROOT_DIR/etc/xdg/menus/milo.menu" ]; then
        log_error "miloOS menu not found"
        errors=$((errors + 1))
    fi
    
    if [ ! -f "$CHROOT_DIR/usr/bin/milo-session" ]; then
        log_error "milo-session script not found"
        errors=$((errors + 1))
    fi
    
    # Verificar items de menú
    local menu_items=("milo-logout" "milo-shutdown" "milo-restart" "milo-sleep" "milo-settings" "milo-about")
    for item in "${menu_items[@]}"; do
        if [ ! -f "$CHROOT_DIR/usr/share/applications/${item}.desktop" ]; then
            log_warn "Menu item ${item}.desktop not found"
        fi
    done
    
    # Verificar temas
    if [ ! -d "$CHROOT_DIR/usr/share/themes/miloOS" ]; then
        log_error "miloOS theme not found"
        errors=$((errors + 1))
    fi
    
    if [ ! -d "$CHROOT_DIR/usr/share/plank/themes/milo" ]; then
        log_error "Plank milo theme not found"
        errors=$((errors + 1))
    fi
    
    # Verificar scripts de optimización
    if [ ! -f "$CHROOT_DIR/usr/local/bin/miloOS-audio-optimize.sh" ]; then
        log_error "Audio optimization script not found"
        errors=$((errors + 1))
    fi
    
    if [ ! -f "$CHROOT_DIR/etc/systemd/system/miloOS-audio-optimization.service" ]; then
        log_error "Audio optimization service not found"
        errors=$((errors + 1))
    fi
    
    return $errors
}
```

**Post-procesamiento**:
- Crear directorios necesarios (proc, sys, dev, etc.)
- Verificar que todas las aplicaciones miloOS estén presentes
- Limpiar logs del sistema
- Limpiar cache de paquetes
- Resetear machine-id

### 3. Live System Module

**Propósito**: Configurar el sistema para arranque Live

**Componentes**:

#### 3.1 Usuario Live
- Crear usuario `milo` con contraseña `1234`
- Agregar a grupos: audio, video, sudo, plugdev, netdev
- Configurar autologin en SLiM
- Copiar configuraciones de /etc/skel

#### 3.2 Scripts de Arranque Live
- `/usr/local/bin/live-config`: Script de inicialización live
- `/etc/systemd/system/live-config.service`: Servicio systemd

#### 3.3 Configuración de Autologin
Modificar `/etc/slim.conf`:
```
default_user milo
auto_login yes
```

#### 3.4 Icono del Instalador
- Crear `/home/milo/Desktop/install-miloOS.desktop`
- Hacer ejecutable y visible en escritorio


### 4. Calamares Module

**Propósito**: Instalar y configurar el instalador gráfico Calamares

#### 4.1 Instalación de Calamares
```bash
chroot "$CHROOT_DIR" apt-get install -y calamares calamares-settings-debian
```

#### 4.2 Estructura de Configuración

```
/etc/calamares/
├── settings.conf           # Configuración principal
├── modules/
│   ├── welcome.conf       # Pantalla de bienvenida
│   ├── locale.conf        # Configuración de idioma
│   ├── keyboard.conf      # Configuración de teclado
│   ├── partition.conf     # Particionado
│   ├── users.conf         # Creación de usuario
│   ├── finished.conf      # Pantalla final
│   └── packages.conf      # Paquetes a instalar
└── branding/
    └── miloOS/
        ├── branding.desc  # Descripción del branding
        ├── logo.png       # Logo de miloOS
        ├── stylesheet.qss # Estilos personalizados
        └── show.qml       # Slideshow durante instalación
```

#### 4.3 Configuración Principal (settings.conf)

**Secuencia de módulos**:
1. welcome - Bienvenida y verificación de requisitos
2. locale - Selección de idioma y región
3. keyboard - Configuración de teclado
4. partition - Particionado del disco
5. users - Creación de usuario
6. summary - Resumen de configuración
7. packages - Instalación de paquetes
8. shellprocess - Ejecución de scripts post-instalación
9. finished - Finalización

**Configuración de branding**:
```yaml
branding: miloOS
```

#### 4.4 Módulo de Usuarios (users.conf)

**Configuración**:
```yaml
defaultGroups:
    - audio
    - video
    - sudo
    - plugdev
    - netdev
    - cdrom
    - floppy
    - scanner
    - bluetooth

autologinGroup: autologin
sudoersGroup: sudo
setRootPassword: true
doAutologin: false
```

#### 4.5 Módulo de Paquetes (packages.conf)

**Backend**: apt

**Paquetes esenciales que deben estar en la ISO**:

```yaml
# Sistema base
- gtk2-engines-murrine
- gtk2-engines-pixbuf
- plank
- catfish
- appmenu-gtk3-module
- dconf-cli
- vala-panel-appmenu
- xfce4-appmenu-plugin
- xfce4-notifyd
- cifs-utils
- smbclient
- slim
- zenity
- git
- unzip
- policykit-1

# PipeWire stack completo
- pipewire
- pipewire-audio-client-libraries
- pipewire-pulse
- pipewire-alsa
- pipewire-jack
- pipewire-v4l2
- pipewire-bin
- wireplumber
- libspa-0.2-bluetooth
- libspa-0.2-jack
- libspa-0.2-modules
- gstreamer1.0-pipewire
- rtkit

# Plugins de audio profesional
- lsp-plugins-lv2
- lsp-plugins-vst
- calf-plugins
- x42-plugins
- zam-plugins

# Sintetizadores
- zynaddsubfx
- zynaddsubfx-lv2
- yoshimi

# Procesadores de guitarra
- guitarix

# Máquinas de ritmo
- hydrogen
- drumgizmo

# Efectos
- dragonfly-reverb
- eq10q

# Utilidades de audio
- ardour
- qpwgraph

# Aplicaciones multimedia
- audacious
- audacious-plugins
- vlc
- gimp
- gimp-data-extras
- shotcut
- digikam

# Internet
- transmission
- filezilla

# Herramientas de compresión
- p7zip-full
- unzip
- zip
- xz-utils
- bzip2
- lzip
- lzop
- arj

# Herramientas ISO
- fuseiso
- genisoimage

# Gestión de fuentes
- font-manager

# Soporte de sistemas de archivos
- fuse3
- ntfs-3g
- exfat-fuse
- exfatprogs

# Herramientas del sistema
- gparted
- bleachbit
- thunar-archive-plugin

# Plymouth (boot splash)
- plymouth
- plymouth-themes

# Dependencias Python para AudioConfig
- python3-gi
- python3-gi-cairo
- gir1.2-gtk-3.0
```

**Operaciones**:
- Todos los paquetes deben estar preinstalados en la ISO
- No se requiere conexión a internet para instalación básica
- Actualizar cache de paquetes post-instalación
- Limpiar cache después de instalación

#### 4.6 Módulo ShellProcess

**Propósito**: Ejecutar scripts de post-instalación para preservar configuraciones

**Scripts a ejecutar**:
1. `preserve-configurations.sh` - Copiar configuraciones de miloOS
2. `setup-audio-groups.sh` - Configurar grupos y permisos de audio
3. `configure-grub.sh` - Configurar GRUB con parámetros optimizados
4. `setup-pipewire.sh` - Configurar PipeWire y JACK
5. `install-miloApps.sh` - Asegurar que AudioConfig y otras apps estén instaladas
6. `finalize-system.sh` - Tareas finales de configuración

#### 4.7 Verificación de miloApps en Chroot

**Propósito**: Asegurar que todas las aplicaciones miloOS estén disponibles en el sistema Live e instalado

```bash
ensure_miloApps_in_chroot() {
    local CHROOT="$1"
    
    # Verificar AudioConfig
    if [ ! -f "$CHROOT/usr/local/bin/audio-config" ]; then
        log_warn "AudioConfig not found, copying..."
        cp AudioConfig/audio-config.py "$CHROOT/usr/local/bin/audio-config"
        chmod +x "$CHROOT/usr/local/bin/audio-config"
    fi
    
    if [ ! -f "$CHROOT/usr/share/applications/audio-config.desktop" ]; then
        cp AudioConfig/audio-config.desktop "$CHROOT/usr/share/applications/"
        chmod 644 "$CHROOT/usr/share/applications/audio-config.desktop"
    fi
    
    # Verificar dependencias de AudioConfig
    chroot "$CHROOT" apt-get install -y python3-gi python3-gi-cairo gir1.2-gtk-3.0 2>/dev/null || true
    
    # Verificar menús miloOS
    if [ ! -f "$CHROOT/etc/xdg/menus/milo.menu" ]; then
        log_warn "miloOS menu not found, copying..."
        cp resources/menus/xdg/milo.menu "$CHROOT/etc/xdg/menus/"
    fi
    
    if [ ! -f "$CHROOT/usr/bin/milo-session" ]; then
        log_warn "milo-session not found, copying..."
        cp resources/menus/bin/milo-session "$CHROOT/usr/bin/"
        chmod +x "$CHROOT/usr/bin/milo-session"
    fi
    
    # Copiar items de menú
    for item in resources/menus/items/*.desktop; do
        if [ -f "$item" ]; then
            cp "$item" "$CHROOT/usr/share/applications/"
        fi
    done
    
    log_info "miloApps verified and installed in chroot"
}
```


### 5. ISO Build Module

**Propósito**: Crear la imagen ISO booteable

#### 5.1 Creación del Filesystem Squashfs

**Comando**:
```bash
mksquashfs "$CHROOT_DIR" "$SQUASHFS_DIR/filesystem.squashfs" \
    -comp xz \
    -b 1M \
    -Xdict-size 100% \
    -e boot
```

**Parámetros**:
- `-comp xz`: Compresión XZ (mejor ratio)
- `-b 1M`: Tamaño de bloque 1MB
- `-Xdict-size 100%`: Diccionario completo para mejor compresión
- `-e boot`: Excluir directorio boot (se copia separado)

#### 5.2 Configuración de GRUB

**Estructura**:
```
$ISO_DIR/
├── boot/
│   └── grub/
│       ├── grub.cfg
│       ├── i386-pc/
│       └── x86_64-efi/
├── live/
│   └── filesystem.squashfs
└── EFI/
    └── BOOT/
        └── bootx64.efi
```

**grub.cfg**:
```
set timeout=10
set default=0

menuentry "miloOS Live" {
    linux /live/vmlinuz boot=live components quiet splash
    initrd /live/initrd.img
}

menuentry "miloOS Live (Safe Mode)" {
    linux /live/vmlinuz boot=live components nomodeset
    initrd /live/initrd.img
}
```

#### 5.3 Creación de la Imagen ISO

**Comando xorriso**:
```bash
xorriso -as mkisofs \
    -iso-level 3 \
    -full-iso9660-filenames \
    -volid "miloOS" \
    -appid "miloOS 1.0" \
    -publisher "Wamphyre" \
    -preparer "miloOS Build System" \
    -eltorito-boot boot/grub/i386-pc/eltorito.img \
    -no-emul-boot \
    -boot-load-size 4 \
    -boot-info-table \
    --eltorito-catalog boot/grub/boot.cat \
    --grub2-boot-info \
    --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img \
    -eltorito-alt-boot \
    -e EFI/BOOT/bootx64.efi \
    -no-emul-boot \
    -append_partition 2 0xef "$ISO_DIR/EFI/BOOT/efiboot.img" \
    -output "$ISO_NAME" \
    -graft-points \
    "$ISO_DIR"
```

**Características**:
- Soporte BIOS legacy (i386-pc)
- Soporte UEFI (x86_64-efi)
- Hybrid MBR para USB booteable
- ISO Level 3 para archivos grandes


## Data Models

### Configuration Preservation Map

**Estructura de datos para preservar configuraciones**:

```json
{
  "system_configs": {
    "grub": {
      "source": "/etc/default/grub",
      "target": "/etc/default/grub",
      "preserve": ["GRUB_CMDLINE_LINUX_DEFAULT", "GRUB_DISTRIBUTOR"]
    },
    "slim": {
      "source": "/etc/slim.conf",
      "target": "/etc/slim.conf",
      "preserve": ["current_theme"]
    },
    "os_release": {
      "source": "/etc/os-release",
      "target": "/etc/os-release",
      "preserve": "all"
    },
    "audio_limits": {
      "source": "/etc/security/limits.d/99-audio-production.conf",
      "target": "/etc/security/limits.d/99-audio-production.conf",
      "preserve": "all"
    },
    "sysctl": {
      "source": "/etc/sysctl.d/99-audio-production.conf",
      "target": "/etc/sysctl.d/99-audio-production.conf",
      "preserve": "all"
    }
  },
  "user_configs": {
    "xfce4": {
      "source": "/etc/skel/.config/xfce4",
      "target": "~/.config/xfce4",
      "recursive": true
    },
    "plank": {
      "source": "/etc/skel/.config/plank",
      "target": "~/.config/plank",
      "recursive": true
    },
    "pipewire": {
      "source": "/etc/pipewire",
      "target": "/etc/pipewire",
      "recursive": true
    },
    "wireplumber": {
      "source": "/etc/wireplumber",
      "target": "/etc/wireplumber",
      "recursive": true
    },
    "profile": {
      "source": "/etc/skel/.profile",
      "target": "~/.profile",
      "preserve": ["LD_LIBRARY_PATH"]
    },
    "bashrc": {
      "source": "/etc/skel/.bashrc",
      "target": "~/.bashrc",
      "preserve": ["LD_LIBRARY_PATH"]
    },
    "xsession": {
      "source": "/etc/skel/.xsession",
      "target": "~/.xsession",
      "preserve": "all"
    },
    "xsessionrc": {
      "source": "/etc/skel/.xsessionrc",
      "target": "~/.xsessionrc",
      "preserve": "all"
    },
    "autostart": {
      "source": "/etc/skel/.config/autostart",
      "target": "~/.config/autostart",
      "recursive": true
    },
    "gtk-3.0": {
      "source": "/etc/skel/.config/gtk-3.0",
      "target": "~/.config/gtk-3.0",
      "recursive": true
    },
    "fontconfig": {
      "source": "/etc/skel/.config/fontconfig",
      "target": "~/.config/fontconfig",
      "recursive": true
    },
    "menus": {
      "source": "/etc/skel/.config/menus",
      "target": "~/.config/menus",
      "recursive": true
    },
    "local_applications": {
      "source": "/etc/skel/.local/share/applications",
      "target": "~/.local/share/applications",
      "recursive": true
    }
  },
  "themes": {
    "gtk": "/usr/share/themes/miloOS",
    "icons": "/usr/local/share/icons/WhiteSur-light",
    "plank": "/usr/share/plank/themes/milo",
    "slim": "/usr/share/slim/themes/milk"
  },
  "applications": {
    "audio_config": {
      "binary": "/usr/local/bin/audio-config",
      "desktop": "/usr/share/applications/audio-config.desktop",
      "icon": "/usr/share/pixmaps/audio-config.png"
    },
    "milo_menus": {
      "menu_file": "/etc/xdg/menus/milo.menu",
      "desktop_files": "/usr/share/applications/milo-*.desktop",
      "menu_script": "/usr/bin/milo-session"
    }
  },
  "system_scripts": {
    "audio_optimize": "/usr/local/bin/miloOS-audio-optimize.sh",
    "profile_d": "/etc/profile.d/pipewire-jack.sh"
  },
  "systemd_services": {
    "audio_optimization": "/etc/systemd/system/miloOS-audio-optimization.service",
    "user_environment": "/etc/systemd/user.conf.d/pipewire-jack.conf"
  }
}
```

### Live System Configuration

```json
{
  "live_user": {
    "username": "milo",
    "password": "1234",
    "groups": ["audio", "video", "sudo", "plugdev", "netdev"],
    "home": "/home/milo",
    "shell": "/bin/bash",
    "autologin": true
  },
  "live_services": {
    "enabled": [
      "NetworkManager",
      "slim",
      "pipewire",
      "wireplumber"
    ],
    "disabled": [
      "apt-daily.timer",
      "apt-daily-upgrade.timer"
    ]
  },
  "live_scripts": {
    "init": "/usr/local/bin/live-config",
    "service": "/etc/systemd/system/live-config.service"
  }
}
```


## Error Handling

### Error Categories

#### 1. Pre-flight Errors
- **Missing dependencies**: Instalar automáticamente o abortar con mensaje claro
- **Insufficient permissions**: Verificar root y abortar si no
- **Insufficient disk space**: Calcular espacio necesario y verificar antes de comenzar
- **System incompatibility**: Verificar que es Debian/miloOS

**Estrategia**: Fail-fast con mensajes claros y sugerencias de solución

#### 2. Build Errors
- **Copy failures**: Reintentar con rsync, log de errores
- **Chroot failures**: Verificar montajes, limpiar y reintentar
- **Package installation failures**: Continuar con advertencia, log de paquetes fallidos
- **Configuration errors**: Revertir a configuración por defecto

**Estrategia**: Continuar cuando sea posible, log detallado, cleanup automático

#### 3. ISO Creation Errors
- **Squashfs creation failure**: Verificar espacio, permisos, reintentar
- **GRUB installation failure**: Verificar paquetes GRUB, reintentar
- **xorriso failure**: Verificar sintaxis, archivos necesarios

**Estrategia**: Abortar con mensaje claro, mantener archivos para debugging

### Error Recovery

**Cleanup automático**:
```bash
cleanup() {
    log_info "Cleaning up..."
    
    # Unmount chroot filesystems
    umount -l "$CHROOT_DIR/proc" 2>/dev/null || true
    umount -l "$CHROOT_DIR/sys" 2>/dev/null || true
    umount -l "$CHROOT_DIR/dev/pts" 2>/dev/null || true
    umount -l "$CHROOT_DIR/dev" 2>/dev/null || true
    
    # Remove work directory
    if [ -d "$WORK_DIR" ]; then
        rm -rf "$WORK_DIR"
    fi
    
    log_info "Cleanup completed"
}

# Trap errors and cleanup
trap cleanup EXIT ERR INT TERM
```

### Logging Strategy

**Log levels**:
- INFO: Progreso normal
- WARN: Advertencias no críticas
- ERROR: Errores que requieren atención

**Log file**: `/tmp/miloOS-build-YYYYMMDD-HHMMSS.log`

**Formato**:
```
[2025-08-10 14:30:45] [INFO] Starting ISO build process
[2025-08-10 14:30:46] [INFO] Checking dependencies...
[2025-08-10 14:30:47] [WARN] Package xyz not found, installing...
[2025-08-10 14:35:12] [ERROR] Failed to create squashfs: No space left on device
```


## Testing Strategy

### Unit Testing

**Scripts individuales**:
- Cada función principal debe ser testeable independientemente
- Usar variables de entorno para simular condiciones
- Mock de comandos externos cuando sea necesario

**Ejemplo**:
```bash
# Test function
test_check_dependencies() {
    # Mock apt-cache
    apt-cache() { echo "debootstrap"; }
    export -f apt-cache
    
    # Run function
    if check_dependencies; then
        echo "PASS: Dependencies check"
    else
        echo "FAIL: Dependencies check"
    fi
}
```

### Integration Testing

**Escenarios de prueba**:

1. **Build completo en sistema limpio**
   - VM con Debian Trixie fresco
   - Ejecutar script completo
   - Verificar ISO creada

2. **Build con configuraciones personalizadas**
   - Sistema con miloOS configurado
   - Verificar preservación de configuraciones
   - Verificar temas y aplicaciones

3. **Boot testing**
   - Probar ISO en QEMU (BIOS)
   - Probar ISO en QEMU (UEFI)
   - Verificar arranque Live
   - Verificar autologin

4. **Installation testing**
   - Instalar desde Live en VM
   - Verificar configuraciones preservadas
   - Verificar grupos y permisos
   - Verificar aplicaciones instaladas

### Validation Testing

**Checklist de validación**:

```bash
validate_iso() {
    local iso_file="$1"
    local errors=0
    
    # 1. Verificar que el archivo existe
    if [ ! -f "$iso_file" ]; then
        log_error "ISO file not found"
        return 1
    fi
    
    # 2. Verificar tamaño mínimo (debe ser > 1GB)
    local size=$(stat -f%z "$iso_file" 2>/dev/null || stat -c%s "$iso_file")
    if [ "$size" -lt 1073741824 ]; then
        log_warn "ISO size is less than 1GB, might be incomplete"
        errors=$((errors + 1))
    fi
    
    # 3. Verificar que es booteable
    if ! file "$iso_file" | grep -q "ISO 9660"; then
        log_error "File is not a valid ISO 9660 image"
        errors=$((errors + 1))
    fi
    
    # 4. Verificar estructura interna
    if command -v isoinfo &> /dev/null; then
        if ! isoinfo -d -i "$iso_file" | grep -q "Volume id: miloOS"; then
            log_warn "ISO volume ID is not 'miloOS'"
            errors=$((errors + 1))
        fi
    fi
    
    # 5. Calcular checksum
    log_info "Calculating SHA256 checksum..."
    sha256sum "$iso_file" > "${iso_file}.sha256"
    log_info "Checksum saved to ${iso_file}.sha256"
    
    # 6. Test boot con QEMU (si está disponible)
    if command -v qemu-system-x86_64 &> /dev/null; then
        log_info "Testing boot with QEMU (this will take a moment)..."
        timeout 30 qemu-system-x86_64 \
            -cdrom "$iso_file" \
            -m 2048 \
            -boot d \
            -nographic \
            -serial mon:stdio &> /dev/null || true
        log_info "QEMU boot test completed"
    fi
    
    return $errors
}
```

### Performance Testing

**Métricas a medir**:
- Tiempo total de build
- Tamaño de ISO resultante
- Tiempo de boot en VM
- Uso de memoria durante build
- Uso de disco durante build

**Objetivos**:
- Build completo: < 30 minutos
- Tamaño ISO: 2-4 GB
- Tiempo de boot: < 60 segundos
- Memoria durante build: < 4 GB
- Espacio temporal: < 20 GB


## Implementation Details

### Calamares Post-Installation Scripts

#### 1. preserve-configurations.sh

**Propósito**: Copiar todas las configuraciones de miloOS al nuevo usuario

```bash
#!/bin/bash
# Preserve miloOS configurations for new user

NEW_USER="$1"
NEW_HOME="/home/$NEW_USER"

# Copy XFCE4 configurations
cp -R /etc/skel/.config/xfce4 "$NEW_HOME/.config/"

# Copy Plank configurations
cp -R /etc/skel/.config/plank "$NEW_HOME/.config/"

# Copy shell configurations
cp /etc/skel/.profile "$NEW_HOME/"
cp /etc/skel/.bashrc "$NEW_HOME/"
cp /etc/skel/.xsession "$NEW_HOME/" 2>/dev/null || true
cp /etc/skel/.xsessionrc "$NEW_HOME/" 2>/dev/null || true

# Copy font configurations
cp -R /etc/skel/.config/fontconfig "$NEW_HOME/.config/" 2>/dev/null || true

# Copy GTK configurations
cp -R /etc/skel/.config/gtk-3.0 "$NEW_HOME/.config/" 2>/dev/null || true
cp /etc/skel/.gtkrc-2.0 "$NEW_HOME/" 2>/dev/null || true

# Copy autostart configurations
cp -R /etc/skel/.config/autostart "$NEW_HOME/.config/" 2>/dev/null || true

# Copy menu configurations
cp -R /etc/skel/.config/menus "$NEW_HOME/.config/" 2>/dev/null || true

# Copy local applications (hidden items)
mkdir -p "$NEW_HOME/.local/share/applications"
cp -R /etc/skel/.local/share/applications/* "$NEW_HOME/.local/share/applications/" 2>/dev/null || true

# Copy XFCE4 xinitrc.d scripts
mkdir -p "$NEW_HOME/.config/xfce4/xinitrc.d"
cp -R /etc/skel/.config/xfce4/xinitrc.d/* "$NEW_HOME/.config/xfce4/xinitrc.d/" 2>/dev/null || true

# Set ownership
chown -R "$NEW_USER:$NEW_USER" "$NEW_HOME"
```

#### 2. setup-audio-groups.sh

**Propósito**: Configurar grupos y permisos de audio para el nuevo usuario

```bash
#!/bin/bash
# Setup audio groups and permissions

NEW_USER="$1"

# Add user to audio groups
usermod -aG audio,video,plugdev,netdev "$NEW_USER"

# Verify limits are in place
if [ ! -f /etc/security/limits.d/99-audio-production.conf ]; then
    cat > /etc/security/limits.d/99-audio-production.conf << 'EOF'
@audio   -  rtprio     99
@audio   -  memlock    unlimited
@audio   -  nice      -20
@audio   -  nofile     524288
EOF
fi

# Verify sysctl settings
if [ ! -f /etc/sysctl.d/99-audio-production.conf ]; then
    cat > /etc/sysctl.d/99-audio-production.conf << 'EOF'
vm.swappiness = 10
fs.inotify.max_user_watches = 524288
kernel.shmmax = 2147483648
fs.file-max = 524288
EOF
    sysctl -p /etc/sysctl.d/99-audio-production.conf
fi
```

#### 3. configure-grub.sh

**Propósito**: Configurar GRUB con parámetros optimizados para audio

```bash
#!/bin/bash
# Configure GRUB for real-time audio

# Ensure GRUB has miloOS branding
sed -i 's/GRUB_DISTRIBUTOR=.*/GRUB_DISTRIBUTOR="miloOS"/' /etc/default/grub

# Ensure kernel parameters are present
if ! grep -q "preempt=full" /etc/default/grub; then
    sed -i 's/GRUB_CMDLINE_LINUX_DEFAULT="\(.*\)"/GRUB_CMDLINE_LINUX_DEFAULT="\1 preempt=full nohz_full=all threadirqs mitigations=off"/' /etc/default/grub
fi

# Update GRUB
update-grub
```

#### 4. setup-pipewire.sh

**Propósito**: Configurar PipeWire y JACK para el nuevo usuario

```bash
#!/bin/bash
# Setup PipeWire and JACK

NEW_USER="$1"
NEW_HOME="/home/$NEW_USER"

# Ensure PipeWire configurations are in place
mkdir -p "$NEW_HOME/.config/pipewire/pipewire.conf.d"
mkdir -p "$NEW_HOME/.config/pipewire/jack.conf.d"

# Copy system-wide PipeWire configs if they exist
if [ -d /etc/pipewire ]; then
    cp -R /etc/pipewire/* "$NEW_HOME/.config/pipewire/" 2>/dev/null || true
fi

# Ensure environment.d for JACK library path
mkdir -p "$NEW_HOME/.config/environment.d"
cat > "$NEW_HOME/.config/environment.d/pipewire-jack.conf" << 'EOF'
LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack:${LD_LIBRARY_PATH}
EOF

# Set ownership
chown -R "$NEW_USER:$NEW_USER" "$NEW_HOME/.config"

# Enable PipeWire services for user
sudo -u "$NEW_USER" systemctl --user enable pipewire.service
sudo -u "$NEW_USER" systemctl --user enable pipewire-pulse.service
sudo -u "$NEW_USER" systemctl --user enable wireplumber.service
```

#### 5. install-miloApps.sh

**Propósito**: Asegurar que AudioConfig y otras aplicaciones miloOS estén instaladas

```bash
#!/bin/bash
# Install miloOS applications

# Ensure AudioConfig is executable
if [ -f /usr/local/bin/audio-config ]; then
    chmod +x /usr/local/bin/audio-config
    log_info "AudioConfig installed"
else
    log_error "AudioConfig not found!"
fi

# Ensure menu scripts are executable
if [ -f /usr/bin/milo-session ]; then
    chmod +x /usr/bin/milo-session
    log_info "milo-session installed"
fi

# Ensure Python dependencies for AudioConfig
apt-get install -y python3-gi python3-gi-cairo gir1.2-gtk-3.0 2>/dev/null || true

# Verify desktop files
for desktop in /usr/share/applications/milo-*.desktop /usr/share/applications/audio-config.desktop; do
    if [ -f "$desktop" ]; then
        chmod 644 "$desktop"
    fi
done

log_info "miloApps installation verified"
```

#### 6. finalize-system.sh

**Propósito**: Tareas finales de configuración

```bash
#!/bin/bash
# Finalize system configuration

# Remove live-config service
systemctl disable live-config.service 2>/dev/null || true
rm -f /etc/systemd/system/live-config.service
rm -f /usr/local/bin/live-config

# Remove installer desktop icon
rm -f /home/*/Desktop/install-miloOS.desktop

# Disable autologin in SLiM
sed -i 's/auto_login yes/auto_login no/' /etc/slim.conf
sed -i 's/default_user milo/default_user/' /etc/slim.conf

# Remove live user if it exists
if id "milo" &>/dev/null; then
    userdel -r milo 2>/dev/null || true
fi

# Update initramfs
update-initramfs -u -k all

# Clean package cache
apt-get clean
apt-get autoremove -y
```

#### 7. calamares-post-install.sh (Script Maestro)

**Propósito**: Orquestar todos los scripts de post-instalación

```bash
#!/bin/bash
# Calamares post-installation master script
# This script is called by Calamares after installation

set -e

LOG_FILE="/var/log/miloOS-post-install.log"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" | tee -a "$LOG_FILE"
}

log "Starting miloOS post-installation configuration..."

# Get the new user from Calamares
NEW_USER=$(awk -F: '$3 >= 1000 && $3 < 65534 {print $1}' /etc/passwd | head -n 1)

if [ -z "$NEW_USER" ]; then
    log "ERROR: Could not determine new user"
    exit 1
fi

log "Configuring system for user: $NEW_USER"

# Run all post-installation scripts
SCRIPT_DIR="/usr/local/share/calamares/scripts"

if [ -f "$SCRIPT_DIR/preserve-configurations.sh" ]; then
    log "Preserving configurations..."
    bash "$SCRIPT_DIR/preserve-configurations.sh" "$NEW_USER" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/setup-audio-groups.sh" ]; then
    log "Setting up audio groups..."
    bash "$SCRIPT_DIR/setup-audio-groups.sh" "$NEW_USER" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/configure-grub.sh" ]; then
    log "Configuring GRUB..."
    bash "$SCRIPT_DIR/configure-grub.sh" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/setup-pipewire.sh" ]; then
    log "Setting up PipeWire..."
    bash "$SCRIPT_DIR/setup-pipewire.sh" "$NEW_USER" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/install-miloApps.sh" ]; then
    log "Installing miloApps..."
    bash "$SCRIPT_DIR/install-miloApps.sh" 2>&1 | tee -a "$LOG_FILE"
fi

if [ -f "$SCRIPT_DIR/finalize-system.sh" ]; then
    log "Finalizing system..."
    bash "$SCRIPT_DIR/finalize-system.sh" 2>&1 | tee -a "$LOG_FILE"
fi

log "miloOS post-installation completed successfully!"
exit 0
```

### Calamares Branding Configuration

#### branding.desc

```yaml
---
componentName: miloOS

strings:
    productName:         "miloOS"
    shortProductName:    "miloOS"
    version:             "1.0"
    shortVersion:        "1.0"
    versionedName:       "miloOS 1.0"
    shortVersionedName:  "miloOS 1.0"
    bootloaderEntryName: "miloOS"
    productUrl:          "https://github.com/Wamphyre/miloOS-core"
    supportUrl:          "https://github.com/Wamphyre/miloOS-core/issues"
    knownIssuesUrl:      "https://github.com/Wamphyre/miloOS-core/issues"
    releaseNotesUrl:     "https://github.com/Wamphyre/miloOS-core/releases"

images:
    productLogo:         "logo.png"
    productIcon:         "logo.png"
    productWelcome:      "welcome.png"

slideshow:              "show.qml"

style:
   sidebarBackground:    "#007AFF"
   sidebarText:          "#FFFFFF"
   sidebarTextSelect:    "#FFFFFF"
   sidebarTextHighlight: "#0051D5"
```

#### Configuración de módulos de Calamares

**welcome.conf**:
```yaml
---
showSupportUrl:         true
showKnownIssuesUrl:     true
showReleaseNotesUrl:    true

requirements:
    requiredStorage:    20.0
    requiredRam:        2.0
    internetCheckUrl:   http://google.com
    check:
        - storage
        - ram
        - power
        - internet
        - root
    required:
        - storage
        - ram
        - root
```

**users.conf**:
```yaml
---
defaultGroups:
    - audio
    - video
    - sudo
    - plugdev
    - netdev
    - cdrom
    - floppy
    - scanner
    - bluetooth
    - lpadmin

autologinGroup:  autologin
sudoersGroup:    sudo
setRootPassword: true
doAutologin:     false

userShell: /bin/bash

avatarFilePath: /usr/share/pixmaps/faces/
```

**partition.conf**:
```yaml
---
efiSystemPartition:     "/boot/efi"
userSwapChoices:
    - none
    - small
    - suspend
    - file

drawNestedPartitions:   false
alwaysShowPartitionLabels: true
allowManualPartitioning: true

defaultFileSystemType:  "ext4"
availableFileSystemTypes:
    - "ext4"
    - "btrfs"
    - "xfs"
    - "f2fs"
```

**shellprocess.conf**:
```yaml
---
dontChroot: false
timeout: 999

script:
    - command: "/usr/local/bin/calamares-post-install.sh"
      timeout: 300
```

**finished.conf**:
```yaml
---
restartNowEnabled: true
restartNowChecked: true
restartNowCommand: "systemctl reboot"

notifyOnFinished: false
```

### Live System Init Script

```bash
#!/bin/bash
# /usr/local/bin/live-config
# Live system initialization script

# Configure network
systemctl start NetworkManager

# Ensure desktop icons are visible
xdg-user-dirs-update

# Show installer icon on desktop
if [ -f /usr/share/applications/calamares.desktop ]; then
    cp /usr/share/applications/calamares.desktop /home/milo/Desktop/install-miloOS.desktop
    chmod +x /home/milo/Desktop/install-miloOS.desktop
    chown milo:milo /home/milo/Desktop/install-miloOS.desktop
fi

# Start PipeWire for live user
sudo -u milo systemctl --user start pipewire.service
sudo -u milo systemctl --user start pipewire-pulse.service
sudo -u milo systemctl --user start wireplumber.service
```


## Security Considerations

### Live System Security

1. **Usuario Live con contraseña conocida**
   - Riesgo: Contraseña predecible (1234)
   - Mitigación: Solo para uso Live, se elimina durante instalación
   - Advertencia: No usar para datos sensibles en modo Live

2. **Sudo sin contraseña**
   - Riesgo: Usuario live tiene acceso root
   - Mitigación: Solo en modo Live, se configura correctamente en instalación
   - Justificación: Necesario para instalador y configuración

3. **Servicios expuestos**
   - Riesgo: Servicios de red activos en Live
   - Mitigación: Firewall básico activo, solo servicios esenciales

### Installation Security

1. **Preservación de configuraciones**
   - Verificar que no se copien credenciales o datos sensibles
   - Limpiar logs y historiales antes de crear ISO
   - No incluir claves SSH o tokens

2. **Permisos de archivos**
   - Verificar permisos correctos en archivos copiados
   - Asegurar que archivos de configuración no sean world-writable
   - Validar ownership de archivos de usuario

3. **Paquetes y actualizaciones**
   - Incluir solo paquetes de repositorios oficiales
   - Verificar checksums de paquetes críticos
   - Habilitar actualizaciones automáticas de seguridad post-instalación

### Build System Security

1. **Ejecución como root**
   - Necesario para operaciones de sistema
   - Validar que solo se ejecute en entorno controlado
   - No ejecutar en sistema de producción

2. **Archivos temporales**
   - Usar directorios temporales seguros
   - Limpiar automáticamente después de build
   - No dejar datos sensibles en temporales

3. **Integridad de ISO**
   - Generar checksums SHA256
   - Firmar ISO con GPG (futuro)
   - Publicar checksums en canal seguro

## Performance Optimizations

### Build Performance

1. **Paralelización**
   - Usar múltiples cores para compresión squashfs
   - Paralelizar operaciones de copia cuando sea posible
   - Usar rsync con opciones optimizadas

2. **Compresión**
   - XZ para mejor ratio (más lento pero menor tamaño)
   - Opción para usar LZ4 (más rápido, mayor tamaño)
   - Ajustar nivel de compresión según necesidad

3. **Caching**
   - Cachear paquetes descargados
   - Reutilizar squashfs si no hay cambios
   - Mantener chroot base para builds incrementales

### Runtime Performance

1. **Boot Speed**
   - Minimizar servicios en arranque Live
   - Usar systemd-analyze para optimizar
   - Precargar módulos críticos

2. **Memory Usage**
   - Comprimir filesystem para reducir uso de RAM
   - Usar zram para swap en Live
   - Optimizar servicios para bajo consumo

3. **Disk I/O**
   - Usar readahead para archivos comunes
   - Optimizar orden de archivos en ISO
   - Minimizar escrituras en modo Live

## Maintenance and Updates

### Version Management

**Esquema de versiones**: MAJOR.MINOR.PATCH

- MAJOR: Cambios significativos de arquitectura
- MINOR: Nuevas características
- PATCH: Correcciones de bugs

**Ejemplo**: miloOS-1.0.0-amd64.iso

### Update Strategy

1. **Sistema base**
   - Seguir actualizaciones de Debian Trixie
   - Probar actualizaciones en VM antes de release
   - Mantener compatibilidad con configuraciones existentes

2. **Aplicaciones miloOS**
   - Actualizar independientemente del sistema base
   - Proveer repositorio APT para actualizaciones
   - Mantener changelog detallado

3. **Temas y recursos**
   - Actualizar sin romper configuraciones existentes
   - Proveer migración automática cuando sea necesario
   - Mantener versiones anteriores disponibles

### Build Automation

**CI/CD Pipeline** (futuro):

```yaml
# .github/workflows/build-iso.yml
name: Build miloOS ISO

on:
  push:
    tags:
      - 'v*'

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Build ISO
        run: sudo ./make-miloOS-release.sh
      - name: Upload ISO
        uses: actions/upload-artifact@v2
        with:
          name: miloOS-ISO
          path: miloOS-*.iso
      - name: Create Release
        uses: softprops/action-gh-release@v1
        with:
          files: |
            miloOS-*.iso
            miloOS-*.iso.sha256
```

## Documentation Requirements

### User Documentation

1. **Installation Guide**
   - Requisitos del sistema
   - Crear USB booteable
   - Proceso de instalación paso a paso
   - Troubleshooting común

2. **Live System Guide**
   - Cómo usar el sistema Live
   - Limitaciones del modo Live
   - Probar hardware y software

3. **Post-Installation Guide**
   - Primeros pasos después de instalar
   - Configuración de audio
   - Instalación de software adicional

### Developer Documentation

1. **Build System Documentation**
   - Cómo construir la ISO
   - Personalizar la build
   - Agregar paquetes o configuraciones

2. **Architecture Documentation**
   - Estructura del sistema
   - Componentes y dependencias
   - Flujo de datos

3. **Contribution Guide**
   - Cómo contribuir
   - Estándares de código
   - Proceso de testing

