#include "CursorFreeze.h"

#include <QtGlobal>

#if defined(Q_OS_WIN)
#include <windows.h>

namespace cursor_freeze {
void set(bool frozen) {
  if (frozen) {
    POINT pt;
    if (GetCursorPos(&pt)) {
      RECT r{pt.x, pt.y, pt.x + 1, pt.y + 1};
      ClipCursor(&r);
    }
  } else {
    ClipCursor(nullptr);
  }
}
}  // namespace cursor_freeze

#elif !defined(Q_OS_MACOS)

namespace cursor_freeze {
void set(bool) {}
}  // namespace cursor_freeze

#endif
