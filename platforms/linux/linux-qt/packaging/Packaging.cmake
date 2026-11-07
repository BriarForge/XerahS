# Linux distribution packaging for the linux-qt edition (CORE-028, CORE-033).
#
# Package formats come from product-contract/capabilities/CORE-PLATFORM-001/
# linux-editions.json for linux-qt: .tar.gz, .deb, .rpm and AUR. The AUR recipe
# lives in packaging/aur/PKGBUILD and builds from source with these install
# rules. Signing, build provenance and release approval stay with the release
# owner (CORE-028); nothing here signs or publishes a package.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
#   cmake --build build
#   cpack --config build/CPackConfig.cmake -B build/packages
#
# Only the application and its desktop integration files are installed. Tests,
# fixtures and the conformance adapter are build-tree only.

include(GNUInstallDirs)

set(XERAHS_PACKAGE_VERSION "0.0.0" CACHE STRING
  "Version stamped on Linux packages; release builds set the approved release version")
set(XERAHS_PACKAGE_RELEASE "1" CACHE STRING "Package release (deb revision / rpm release)")

install(TARGETS xerahs RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/com.xerahs.app.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/com.xerahs.app.metainfo.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES ${XERAHS_REPO_ROOT}/LICENSE
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/xerahs)

set(CPACK_PACKAGE_NAME "xerahs")
set(CPACK_PACKAGE_VENDOR "ShareX Team")
set(CPACK_PACKAGE_CONTACT "ShareX Team <https://github.com/BriarForge/XerahS>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/BriarForge/XerahS")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Capture, annotate, and share screenshots")
set(CPACK_PACKAGE_DESCRIPTION
  "XerahS captures regions of the screen through the xdg-desktop-portal, annotates them in a native editor, and saves, copies or uploads the result. This is the Qt 6 reference edition for Linux, Wayland first.")
set(CPACK_PACKAGE_VERSION "${XERAHS_PACKAGE_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${XERAHS_REPO_ROOT}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "xerahs-${XERAHS_PACKAGE_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_STRIP_FILES ON)
set(CPACK_GENERATOR "TGZ")

find_program(XERAHS_DPKG_SHLIBDEPS dpkg-shlibdeps)
if(XERAHS_DPKG_SHLIBDEPS)
  list(APPEND CPACK_GENERATOR "DEB")
endif()
find_program(XERAHS_RPMBUILD rpmbuild)
if(XERAHS_RPMBUILD)
  list(APPEND CPACK_GENERATOR "RPM")
endif()

# .deb: shared-library dependencies come from dpkg-shlibdeps; the portal and the
# Secret Service are runtime services, so they are recommendations.
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_RELEASE "${XERAHS_PACKAGE_RELEASE}")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "xdg-desktop-portal, qt6-qpa-plugins, qt6-wayland, qt6-image-formats-plugins, gnome-keyring | kwalletmanager")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "tesseract-ocr-eng, gstreamer1.0-plugins-good")

# .rpm: library requirements come from rpm's automatic dependency generator.
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_RELEASE "${XERAHS_PACKAGE_RELEASE}")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Multimedia")
set(CPACK_RPM_PACKAGE_URL "${CPACK_PACKAGE_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
  /usr/share/applications /usr/share/metainfo /usr/share/doc)

include(CPack)
