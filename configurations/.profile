# ~/.profile: executed by the command interpreter for login shells.
# This file is not read by bash(1), if ~/.bash_profile or ~/.bash_login
# exists.
# see /usr/share/doc/bash/examples/startup-files for examples.
# the files are located in the bash-doc package.

# the default umask is set in /etc/profile; for setting the umask
# for ssh logins, install and configure the libpam-umask package.
#umask 022

# if running bash
if [ -n "$BASH_VERSION" ]; then
    # include .bashrc if it exists
    if [ -f "$HOME/.bashrc" ]; then
	. "$HOME/.bashrc"
    fi
fi

# set PATH so it includes user's private bin if it exists
if [ -d "$HOME/bin" ] ; then
    PATH="$HOME/bin:$PATH"
fi

# set PATH so it includes user's private bin if it exists
if [ -d "$HOME/.local/bin" ] ; then
    PATH="$HOME/.local/bin:$PATH"
fi

# PipeWire JACK library path for audio applications (miloOS)
MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo "x86_64-linux-gnu")
export LD_LIBRARY_PATH="/usr/lib/${MULTIARCH}/pipewire-0.3/jack${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Global menu environment for miloPanel.
milo_normalize_gtk_modules() {
    _milo_modules="appmenu-gtk-module"
    _old_ifs="$IFS"
    IFS=:
    for _module in $GTK_MODULES; do
        [ -z "$_module" ] && continue
        case ":$_milo_modules:" in
            *":$_module:"*) ;;
            *) _milo_modules="$_milo_modules:$_module" ;;
        esac
    done
    IFS="$_old_ifs"
    export GTK_MODULES="$_milo_modules"
    export UBUNTU_MENUPROXY=1
    if command -v dbus-update-activation-environment >/dev/null 2>&1; then
        dbus-update-activation-environment --systemd GTK_MODULES UBUNTU_MENUPROXY >/dev/null 2>&1 || true
    fi
    unset _milo_modules _old_ifs _module
}
milo_normalize_gtk_modules
unset -f milo_normalize_gtk_modules
