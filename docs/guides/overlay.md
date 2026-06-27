# Overlay and GUI Behavior

There are two separate display concepts:

- The GUI/debug preview window, controlled mostly by `show_window`.
- The game overlay, controlled by `game_overlay_enabled` and related overlay keys.

The GUI preview can require CPU copies because it has to show captured pixels. The game overlay can draw boxes, future positions, frame outlines, Circle FOV, and optional icons.

Useful overlay keys:

```ini
overlay_exclude_from_capture = true
game_overlay_enabled = false
game_overlay_compensate_latency = true
game_overlay_draw_circle_fov = true
```

If you are measuring performance, test with the preview closed and then open so you can see how CPU-copy needs change.

## Latency Compensation

The app compensates for the delay between frame capture, inference, and display.
This keeps aim and overlay drawing closer to what is currently on screen instead
of where the target was when the frame was captured.

Use this key only if the game overlay boxes appear over-corrected or under-corrected:

```ini
game_overlay_compensate_latency = false
```

This setting affects the visual overlay correction. It does not switch input
methods and does not create a fallback control path.

Related docs:

- [Capture diagnostics](capture-diagnostics.md)
- [Circle FOV](circle-fov.md)
