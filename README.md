# Sidetop – Horizontal Scrolling Desktop

[日本語](README.ja.md)

A [Windhawk](https://windhawk.net/) mod that makes the Windows desktop wider
than the screen. Scroll it left and right with the mouse wheel and keep icons
on the extra pages.

- Wheel down moves right, wheel up moves left. The left edge stops at your
  original layout ("home"). Ctrl+wheel still changes the icon size.
- Smooth scrolling with a little glide, or page by page.
- Drag with the middle mouse button to move the desktop by hand.
- Double-click an empty spot, or press a shortcut key, to go back home.
- While dragging icons, hold them at the screen edge to move to the next page.
- New files (downloads, for example) appear on the page you are looking at.
- The wallpaper can stay fixed, move a little (parallax), or be a wide image
  that moves with the icons.
- A thin bar at the bottom shows where you are while the desktop moves.

Tested on Windows 10 22H2 with one monitor. On Windows 11 it was tried in a
virtual machine, where the main features worked; not every feature has been
checked there.

## Install

1. Back up your icon layout:
   `reg export "HKCU\Software\Microsoft\Windows\Shell\Bags\1\Desktop" desktop-icons-backup.reg`
2. In Windhawk, choose **Create a new mod**, replace the code with
   [`sidetop.wh.cpp`](sidetop.wh.cpp), then **Compile Mod** and enable it.
3. Restart Explorer once, so that icons placed on the added pages stay there
   after a refresh.

To restore the backup, turn the mod off, then run these one at a time
(Explorer saves the layout when it exits, so import while it is closed):

```
taskkill /f /im explorer.exe
reg import desktop-icons-backup.reg
start explorer.exe
```

## Settings

| Setting | Default |
|---|---|
| Scrolling: smooth or page by page | Smooth |
| Pixels per wheel notch (smooth only) | 200 |
| Glide (inertia), in milliseconds | 100 |
| Pages added to the right of home | 1 |
| Go back home with: double-click, shortcut key, or nothing | Double-click |
| Shortcut key for going back home | Ctrl+Alt+Home |
| Wallpaper: fixed, parallax, or wide image | Fixed |
| Wide image file | (empty) |
| Put new icons on the screen you are looking at | On |
| Move to the next page while dragging icons at the screen edge | On |
| Wait at the edge before moving, in milliseconds | 500 |
| Top speed while dragging at the edge, in pixels per second | 1500 |
| Show a position bar at the bottom while scrolling | On |

## Good to know

- **"Auto arrange icons" must be off.** While it is on, the mod does nothing.
  Turning it on moves every icon back home.
- **Turning the mod off or uninstalling it** moves every icon that is outside
  home back into a free spot on the home screen, so no file is ever lost off
  the screen.
- The page you were on is restored after Explorer restarts.
- Edge scrolling while dragging works for drags that start in Explorer (the
  desktop or a folder window), not for drags from other programs.

## Known issues

- Changing the icon size, or restarting Explorer, can now and then shift some
  icons out of place.
- With many icons on the screen, scrolling is less smooth with the fixed and
  parallax wallpaper. The wide image mode is the smoothest.
- Only one monitor is supported so far. With two or more, the mod stays
  off; when a monitor is added, every icon goes back home first. It starts
  again when you are back to one monitor.

## How it works

The desktop icons live in a list view window inside Explorer. Sidetop makes
that window and Explorer's work area two or more screens wide and slides the
window left and right. Icons are never moved for scrolling, so the layout
Explorer saves is the real one. Notes from the development are in
[`docs/`](docs/) (in Japanese).

## Credits

Written with [Claude Code](https://claude.com/claude-code) (Anthropic's
Claude), tested on the author's own PC.

## License

[MIT](LICENSE)

