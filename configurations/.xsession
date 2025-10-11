#!/bin/sh
# miloOS .xsession for SLiM

# PipeWire JACK library path for audio applications
export LD_LIBRARY_PATH="/usr/lib/x86_64-linux-gnu/pipewire-0.3/jack:${LD_LIBRARY_PATH}"

# Start XFCE session
exec startxfce4
