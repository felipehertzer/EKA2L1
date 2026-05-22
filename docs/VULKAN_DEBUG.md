# Vulkan debugging

The Vulkan backend has runtime debug controls through environment variables. They are intentionally outside the normal settings UI so the same switches work on desktop, Android, and iOS/macOS development builds.

## Common switches

- `EKA2L1_VULKAN_DEBUG=1`
  Enables Vulkan debug setup logging.
- `EKA2L1_VULKAN_DEBUG_TRACE=1`
  Logs each recorded draw batch with target, draw counts, and clipping counts.
- `EKA2L1_VULKAN_DEBUG_CAPTURE=present`
  Dumps presented swapchain frames as binary PPM images.
- `EKA2L1_VULKAN_DEBUG_CAPTURE=offscreen`
  Dumps offscreen framebuffer targets after they are flushed.
- `EKA2L1_VULKAN_DEBUG_CAPTURE=all`
  Enables both `present` and `offscreen` captures.
- `EKA2L1_VULKAN_DEBUG_DIR=/path/to/output`
  Chooses the output directory. The default is the platform temp directory under `eka2l1-vulkan-debug`.
- `EKA2L1_VULKAN_DEBUG_FRAMES=60`
  Limits presented frame captures. Default: `60` when present capture is enabled.
- `EKA2L1_VULKAN_DEBUG_SKIP_FRAMES=120`
  Skips the first presented frames before writing captures. Use this when startup frames are not interesting.
- `EKA2L1_VULKAN_DEBUG_FRAME_INTERVAL=10`
  Captures every Nth presented frame after the skip window. Default: `1`.
- `EKA2L1_VULKAN_DEBUG_OFFSCREEN_FRAMES=120`
  Limits offscreen captures. Default: `120` when offscreen capture is enabled.
- `EKA2L1_VULKAN_DEBUG_OFFSCREEN_SKIP=20`
  Skips the first offscreen target flushes before writing captures.
- `EKA2L1_VULKAN_DEBUG_OFFSCREEN_INTERVAL=5`
  Captures every Nth offscreen target after the skip window. Default: `1`.
- `EKA2L1_WINDOW_TRACE=1`
  Logs window visibility, visible regions, redraw-store state, SGC layout changes, and status-pane IPCs.
- `EKA2L1_WINDOW_TRACE_COMMANDS=1`
  Adds per-window GDI command details to `EKA2L1_WINDOW_TRACE`. This is noisy, so use it only for short captures.

Examples:

```sh
EKA2L1_VULKAN_DEBUG=1 \
EKA2L1_VULKAN_DEBUG_TRACE=1 \
EKA2L1_VULKAN_DEBUG_CAPTURE=all \
EKA2L1_VULKAN_DEBUG_DIR=/tmp/eka2l1-vulkan \
build-macos-vulkan/bin/EKA2L1.app/Contents/MacOS/EKA2L1 --run Bounce
```

To capture later gameplay frames instead of startup clears:

```sh
EKA2L1_VULKAN_DEBUG_CAPTURE=present \
EKA2L1_VULKAN_DEBUG_SKIP_FRAMES=120 \
EKA2L1_VULKAN_DEBUG_FRAME_INTERVAL=10 \
EKA2L1_VULKAN_DEBUG_FRAMES=20 \
build-macos-vulkan/bin/EKA2L1.app/Contents/MacOS/EKA2L1 --run Bounce
```

For Android, set the variables through the shell before launching the activity, or add them to the native process environment used by the launcher.

The captures are written as `.ppm` files so they can be inspected without adding PNG dependencies to the emulator. Most image viewers and ImageMagick can open them directly.
