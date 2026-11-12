# Application icon

Original mark for the linux-qt edition (`com.xerahs.app`, display name XerahS). It is an amber X inside a paper capture frame on an ink field. The corners of the canvas stay transparent. The paper plate is the shape a dark panel sees; the ink field is the shape a light panel sees.

The application and tray use this mark through `applicationIcon()`. The install rules provide the matching scalable icon for the desktop entry and autostart entry (`Icon=com.xerahs.app`).

This does not finish CORE-028. Signed packages, build provenance, replacement migration, update, rollback, and target qualification remain open.

## License

The mark is original artwork made for this repository. It is not derived from the ShareX logo or the Avalonia XerahS logo, and it is not a trademark of those projects.

It is released under the same license as the program: GNU GPL v3, expressed in packaging as `GPL-3.0-or-later` (`packaging/com.xerahs.app.metainfo.xml`, repository `LICENSE`). The SVG carries an SPDX notice so the installed file stays identifiable without the rest of the tree. No extra font, image, or icon-theme package is required to draw it.

## Runtime API

```cpp
QIcon xerahs::app::applicationIcon();
```

Declared in `app/ApplicationIcon.h`, drawn in `app/ApplicationIcon.cpp`.

- Call it only after `QApplication` / `QGuiApplication` has been constructed. `QPixmap` is not valid before that. The existing call site in `main.cpp` is already after construction.
- Each call builds a new `QIcon` that owns pixmaps at 16, 24, 32, 48, 64, 128, and 256. Copies keep those pixmaps after the original icon is cleared or destroyed.
- Drawing uses `QPainter` into a `QImage`. It does not open a file, a resource, or the network, and it does not link Qt Svg.
- The scalable file is for the desktop icon theme. The running application does not load it.

## Shared geometry

Both the painter and `packaging/icons/com.xerahs.app.svg` use a 256 viewBox. Change them together.

| Part | Geometry | Paint |
|---|---|---|
| Paper plate | rounded rect x=16 y=16 w=224 h=224 rx=52 | `#F3EEE4` |
| Ink field | rounded rect x=32 y=32 w=192 h=192 rx=40 | `#243044` |
| Capture frame | stroke width 28, square cap, miter join, miter limit 2. Paths `M100 48 H48 V100`, `M156 48 H208 V100`, `M100 208 H48 V156`, `M156 208 H208 V156` | `#F3EEE4` |
| X | stroke width 30, round cap and join. Paths `M92 92 L164 164`, `M164 92 L92 164` | `#E39B12` |

Outside the paper plate is transparent. There is no outline stroke on either rounded rectangle.

## Integration

`CMakeLists.txt` includes the painter in `xerahs_app` and registers `application-icon-test`. `main.cpp` sets the window and tray icons after constructing `QApplication`; the headless path does not create pixmaps.

`packaging/Packaging.cmake` installs the SVG to `${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps/com.xerahs.app.svg`. The runtime painter needs no SVG loader or Qt Svg dependency.

## Tests

`tests/ApplicationIconTest.cpp` checks three behaviours offscreen:

- the icon is non-null and has a real pixmap at each of 16, 24, 32, 48, 64, 128, and 256, and two calls match
- the canvas corners are transparent, the plate covers a real part of the square, a light composite still shows ink, a dark composite still shows paper, the centre has a warm X, and each corner zone has a light capture bracket
- a copy and a pixmap still paint after the original icon is cleared, a moved-to icon paints, and two dozen fresh calls stay non-null and stable

The test runs offscreen through CTest.

Integration verification: the icon test passes on Qt 6.12 and the Qt 6.4.2 baseline. A staged install and generated `.tar.gz` contain the scalable launcher asset. RPM packaging remains unverified locally because `rpmbuild` is unavailable. These checks do not grant release approval.
