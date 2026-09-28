# Linux

Native Linux application. Not started.

Pilot default (D-LIN-001): Qt 6 (dynamically linked), C++17, Wayland first, X11 fallback. GNOME 46+ and KDE Plasma 6 first-class.

Editions (D-LIN-002, `CORE-033` to `CORE-037`): `linux-qt` is the supported
reference edition. Distro-native candidate editions, `linux-gnome` (Ubuntu,
Fedora Workstation), `linux-kde` (Fedora KDE, Kubuntu), and `linux-hyprland`
(Omarchy, Arch with Hyprland), are profiled in
[`linux-editions.json`](../../product-contract/capabilities/CORE-PLATFORM-001/linux-editions.json)
and live under `platforms/linux/<edition>/` when started.
