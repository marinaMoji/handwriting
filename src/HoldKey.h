#pragma once

#include <QKeyEvent>
#include <QtGlobal>

// The hold-to-write key is the physical key at the top-left of the main block
// (left of "1", below Esc): ` on US/UK, ² on French AZERTY, § on many Nordic/
// Swiss/German Mac layouts, etc. On macOS we match the hardware key code
// (kVK_ANSI_Grave = 0x32), which is layout-independent. Elsewhere we fall back
// to the common characters that key produces.
namespace hold_key {

inline constexpr quint32 kMacGraveVirtualKey = 0x32;
// ISO keyboards (AZERTY, QWERTZ, UK...) report the top-left key as kVK_ISO_Section
// on many setups, and kVK_ANSI_Grave for the key beside left Shift. Accept both.
inline constexpr quint32 kMacIsoSectionVirtualKey = 0x0A;

inline bool isHoldToWriteKey(int qtKey, quint32 nativeVirtualKey) {
#ifdef Q_OS_MACOS
  if (nativeVirtualKey != 0) {
    return nativeVirtualKey == kMacGraveVirtualKey ||
           nativeVirtualKey == kMacIsoSectionVirtualKey;
  }
#else
  Q_UNUSED(nativeVirtualKey);
#endif
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
  return isHoldToWriteKey(e->key(), e->nativeVirtualKey());
}

}  // namespace hold_key
