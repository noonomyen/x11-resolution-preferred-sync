# X11 Resolution Preferred Sync

A tool installed as a service that monitors the X11 preferred resolution profile.
If the profile changes, the tool immediately resets the display to the preferred resolution.
Designed for use with **Spice** on **virt-manager**.

## Requirements

- **libx11-dev**
- **libxrandr-dev**

Tested on **Kali Linux** with **virt-manager + spice + virtio**.

## Installation

Note: `sudo` is required only for installing the polkit policy.

```sh
make build
make install
```
