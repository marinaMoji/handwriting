#pragma once

#include <QKeyEvent>
#include <QtGlobal>

// The hold-to-write key is the physical key at the top-left of the main block
// (left of "1", below Esc): ` on US/UK, ² on French AZERTY, § on many Nordic/
// Swiss/German Mac layouts, etc. Prefer layout-independent hardware codes.
namespace hold_key {

inline constexpr quint32 kMacGraveVirtualKey = 0x32;
// ISO keyboards (AZERTY, QWERTZ, UK...) report the top-left key as kVK_ISO_Section
// on many setups, and kVK_ANSI_Grave for the key beside left Shift. Accept both.
inline constexpr quint32 kMacIsoSectionVirtualKey = 0x0A;

// Windows PS/2 scan code for the grave/tilde key (US position).
inline constexpr quint32 kWinGraveScanCode = 0x29;

// X11 / Linux keycode for that same physical key (US position).
inline constexpr quint32 kX11GraveKeycode = 49;

inline bool isHoldToWriteKey(int qtKey, quint32 nativeVirtualKey,
                             quint32 nativeScanCode = 0) {
#ifdef Q_OS_MACOS
  Q_UNUSED(nativeScanCode);
  if (nativeVirtualKey != 0) {
    return nativeVirtualKey == kMacGraveVirtualKey ||
           nativeVirtualKey == kMacIsoSectionVirtualKey;
  }
#elif defined(Q_OS_WIN)
  Q_UNUSED(nativeVirtualKey);
  if (nativeScanCode != 0) {
    // Qt sets bit 0x100 for extended keys; the grave key is never extended, so compare exactly.
    return nativeScanCode == kWinGraveScanCode;
  }
#elif defined(Q_OS_LINUX)
  Q_UNUSED(nativeVirtualKey);
  if (nativeScanCode != 0) {
    return nativeScanCode == kX11GraveKeycode;
  }
#else
  Q_UNUSED(nativeVirtualKey);
  Q_UNUSED(nativeScanCode);
#endif
  // Character fallback when the platform did not supply a scan/virtual code.
  switch (qtKey) {
    case Qt::Key_QuoteLeft:
    case Qt::Key_Dead_Grave:
    case Qt::Key_AsciiTilde:
    case Qt::Key_twosuperior:  // AZERTY ²
    case Qt::Key_section:      // § (Nordic/Swiss/German Mac)
    case Qt::Key_Dead_Circumflex:  // ^ (German QWERTZ, PC)
      return true;
    default:
      return false;
  }
}

inline bool isHoldToWriteKey(const QKeyEvent* e) {
  return isHoldToWriteKey(e->key(), e->nativeVirtualKey(), e->nativeScanCode());
}

}  // namespace hold_key
