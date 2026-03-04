# X11 Resolution Preferred Sync

A tool installed as a service that monitors the X11 preferred resolution profile. If the profile changes, the tool immediately resets the display to the preferred resolution. Designed for use with Spice on virt-manager.

## Requirements

- `xev` (Xorg)
- `xrandr` (Xorg)
