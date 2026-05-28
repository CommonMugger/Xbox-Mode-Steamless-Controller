# Steam Controller Remapper 1.4.2

Steam Controller Remapper is now centered on a full desktop remapper plus an Xbox Game Bar widget, with VIIPER virtual output replacing the old ViGEmBus path.

## Highlights

- Migrated virtual controller output to `VIIPER/libVIIPER`
- Installer now downloads and installs the latest upstream `usbip-win2` package automatically when required
- `Check for Updates...` now downloads the latest installer bundle from GitHub Releases and launches it automatically
- Added an Xbox Game Bar widget for quick profile switching and fast paddle remaps
- Added Steam Controller trackpad mouse support with opposite-pad click handling and click haptics
- Added automated update checks for SDL, VIIPER, and `usbip-win2`

## Behavior changes

- Virtual output now depends on `usbip-win2` instead of ViGEmBus
- Trackpad cursor movement uses the app's host-side path, with short-press opposite-pad left click and hold-for-right-click
- The app backs off cleanly when Steam is running so Steam Input can take control

## Installer and trust notes

- Install runs with administrator rights because `usbip-win2` and the desktop app install into system locations
- The widget install path imports a bundled local certificate for sideloading
- This release does not use a paid public code-signing certificate, so Windows may show SmartScreen or other trust prompts
- If you are upgrading from the old ViGEmBus-based release, uninstall the old version first and then install this release fresh

## License

- The project is now distributed under `GPL-3.0-or-later` because it links against `libVIIPER`
- Original SteamlessController attribution and the original MIT notice remain preserved in `LICENSES/original-steamlesscontroller-mit.txt`

## Known limitations

- Trackpad feel is close to Steam desktop behavior, but not an exact Steam Input clone
- Game Bar quick remaps currently edit only gamepad-button mappings
