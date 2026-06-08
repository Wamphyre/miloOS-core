#!/bin/sh
# miloOS .xsession for SLiM

# PipeWire JACK library path for audio applications
MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo "x86_64-linux-gnu")
export LD_LIBRARY_PATH="/usr/lib/${MULTIARCH}/pipewire-0.3/jack${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Start XFCE session
exec startxfce4
