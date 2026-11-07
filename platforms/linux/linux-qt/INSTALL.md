# Installing XerahS on Linux (`linux-qt`)

The Qt 6 reference edition ships as `.tar.gz`, `.deb`, `.rpm` and an AUR recipe,
the formats that `CORE-PLATFORM-001/linux-editions.json` declares for `linux-qt`.
Packages built here are **unsigned development builds**. Signed release packages,
build provenance and distribution to users are release-owner steps under
CORE-028 and are not produced by this repository yet.

## Runtime requirements

| Need | Why | Ubuntu / Debian | Fedora | Arch |
|---|---|---|---|---|
| Qt 6.4+ (Core, Gui, Widgets, DBus, Network, PrintSupport) | Application toolkit | `qt6-base-dev` (pulls the runtime) | `qt6-qtbase` | `qt6-base` |
| xdg-desktop-portal plus your desktop's backend | Screenshot and global shortcut consent | `xdg-desktop-portal-gnome` or `-kde` | `xdg-desktop-portal-gnome` or `-kde` | `xdg-desktop-portal-gnome`, `-kde` or `-hyprland` |
| Secret Service (optional) | Uploader credentials | `gnome-keyring` or KWallet | `gnome-keyring` or KWallet | `gnome-keyring` or KWallet |
| Tesseract 5.5.3+ with English data (optional) | Text recognition | `tesseract-ocr-eng` | `tesseract-langpack-eng` | `tesseract-data-eng` |
| zxing-cpp 3.1.1+ (optional, build time) | QR scanning | build from source | `zxing-cpp-devel` | `zxing-cpp` |
| GStreamer good plugins (optional) | Screen recording | `gstreamer1.0-plugins-good` | `gstreamer1-plugins-good` | `gst-plugins-good` |

When OCR or QR dependencies are missing, the Tools dialogs say so and name what
is missing instead of hiding the feature.

## Install a package

```bash
# Ubuntu 24.04 / Debian
sudo apt install ./xerahs_<version>_amd64.deb

# Fedora
sudo dnf install ./xerahs-<version>.x86_64.rpm

# Arch (AUR recipe in this repository)
cd platforms/linux/linux-qt/packaging/aur && makepkg -si

# Any distribution: unpack the .tar.gz into a prefix
tar -xzf xerahs-<version>-linux-x86_64.tar.gz
```

Every push and pull request uploads the `.tar.gz`, `.deb` and `.rpm` from the
`linux-qt-packages` CI job as the `xerahs-linux-qt-packages` artifact.

Packages install:

- `/usr/bin/xerahs`
- `/usr/share/applications/com.xerahs.app.desktop` (the `com.xerahs.app`
  identity that portals use to attribute consent)
- `/usr/share/metainfo/com.xerahs.app.metainfo.xml` (software centres)
- `/usr/share/doc/xerahs/LICENSE`

Uninstall with `sudo apt remove xerahs`, `sudo dnf remove xerahs` or
`sudo pacman -R xerahs-git`. Uninstalling leaves your settings, history and
captures in your home directory.

## Build packages from source

```bash
cd platforms/linux/linux-qt
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
cpack --config build/CPackConfig.cmake -B build/packages
```

CPack always produces `.tar.gz`. It adds `.deb` when `dpkg-shlibdeps` is present
(`dpkg-dev`) and `.rpm` when `rpmbuild` is present (`rpm`). Library dependencies
are computed by `dpkg-shlibdeps` and by rpm's automatic requirement generator.
Set `-DXERAHS_PACKAGE_VERSION=` and `-DXERAHS_PACKAGE_RELEASE=` to stamp a version.

To install straight from the build tree instead of a package:

```bash
sudo cmake --install build
```

## Not yet covered

- An application icon: the desktop entry names `com.xerahs.app`, but no icon
  artwork is in the repository yet, so menus show a generic icon.
- Signing, provenance, replacement migration, update and rollback qualification
  (CORE-028), and qualification on every declared distribution and desktop.
- Flatpak and AppImage are not formats of the `linux-qt` edition; Flatpak is
  declared for the candidate GNOME and KDE editions.
