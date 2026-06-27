# Circle FOV

## Recommended Setup

```ini
circle_fov_enabled = true
circle_fov_radius_percent = 100
circle_fov_show_preview = true
game_overlay_draw_circle_fov = true
```

Use the GUI or overlay to visualize the circle. Lower `circle_fov_radius_percent` to make the active area smaller.

## Does Drawing the Circle Add Much Overhead?

Drawing the circle in the GUI preview or game overlay is small compared with capture and inference. The overlay depiction only matters when the GUI or overlay is actually open and configured to show it.

Related docs:

- [Overlay and GUI behavior](overlay.md)
- [Configuration guide](../config.md)
