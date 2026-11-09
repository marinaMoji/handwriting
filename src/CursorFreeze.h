#pragma once

// Detach the pointer from the trackpad while writing, so finger movement
// cannot drag it onto other windows. Best effort per platform:
//   macOS   - CGAssociateMouseAndMouseCursorPosition
//   Windows - ClipCursor to the current point (untested)
//   others  - no-op (needs an evdev grab; see docs)
namespace cursor_freeze {
void set(bool frozen);
}
