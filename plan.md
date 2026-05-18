# Steam Controller Remapper Plan

## Next Priority

1. Design and implement an in-Xbox-mode remap workflow so button remaps can be changed without leaving the console-like experience.
2. Evaluate a Game Bar widget as the primary approach for in-game remap/profile control.
3. If Game Bar is not a good fit, evaluate a lightweight always-on-top overlay or controller-driven mini UI as the fallback approach.

## Game Bar Widget Plan

1. Confirm packaging/runtime requirements for an Xbox Game Bar widget integration for this project.
2. Decide whether the widget hosts the existing remap logic through a local bridge/service layer or whether it talks directly to a shared config/control backend.
3. Extract profile/mapping operations into a UI-agnostic backend surface so desktop UI and Game Bar UI use the same codepath.
4. Build a v1 widget with:
   - current detected game
   - active profile
   - profile selection
   - per-paddle remap controls
   - immediate save/apply
5. Add an `Open full desktop editor` action so the widget can hand off advanced edits cleanly.
6. Define a controller-friendly navigation model for the widget so it works without mouse/keyboard dependence.
7. Decide how the widget invokes advanced flows that may still belong to the desktop UI.
8. Add debug logging around widget open, profile apply, remap save, and detected-game refresh.
9. Test widget behavior across:
   - Xbox app / Game Pass titles
   - Steam titles
   - no-game / desktop state
   - Steam-running-disabled state
10. After v1 works, decide which current desktop-only actions should also be exposed in the widget.

## Controller-Friendly Desktop UI

1. Define a controller-first navigation model for the remap window:
   - focus order
   - selected control styling
   - confirm/back semantics
   - shoulder/trigger shortcuts where useful
2. Make the installed-game list, source list, and remap editors navigable without mouse hover dependence.
3. Replace hover-only affordances with focusable or explicit actions so bindings and statuses are visible from controller navigation alone.
4. Add large, predictable focus targets for:
   - profile selection
   - paddle selection
   - action type
   - gamepad target
   - save/reset/open-editor actions
5. Add controller-driven open/close behavior that works cleanly while Game Bar or Xbox mode is active.
6. Test the remap window end to end without touching mouse or keyboard.

## Identity / Rename

1. Rename the project away from the fork identity so the name reflects the current product direction.
2. Update app name, executable name, tray text, config directory naming, registry keys, docs, and repo branding consistently.
3. Plan a backward-compatibility pass for existing config paths and registry values so current users are not broken by the rename.

## UX / Visibility

4. Add an `Active profile` indicator in the tray/menu UI.
5. Add a `Current detected game` status line in the remap window.
6. Add lightweight status feedback for `Profile saved` / `Profile switched`.
7. Add an optional debug/status view showing:
   - active profile
   - detected foreground exe
   - last matched game source

## Library / Profiles

8. Add search/filter for the installed game list.
9. Add favorites or recent games near the top of the installed game list.
10. Filter Xbox/Steam DLC and bonus-content entries more aggressively.
11. Add `Duplicate profile from current`.
12. Add `Reset this profile to default`.
13. Add import/export for `profiles.ini`.
