#include "TrackpadSourceFactory.h"

#include "QtTouchCapture.h"
#include "TrackpadSource.h"

#include <QStringList>
#include <QWidget>

#ifdef Q_OS_MACOS
#include "macos/NSTouchCapture.h"
#endif
#ifdef Q_OS_LINUX
#include "linux/EvdevTrackpadSource.h"
#endif
#ifdef Q_OS_WIN
#include "windows/RawInputTrackpadSource.h"
#endif

TrackpadSource* createTrackpadSource(QWidget* host, QObject* parent, QString* fallbackNote) {
  const QString pref = qEnvironmentVariable("HANDWRITING_CAPTURE").toLower();

  if (pref == QStringLiteral("qt")) {
    return new QtTouchCapture(parent);
  }

  const bool wantAuto = pref.isEmpty();
  bool matchedKnown = wantAuto || pref == QStringLiteral("qt");

#ifdef Q_OS_MACOS
  if (wantAuto || pref == QStringLiteral("nstouch")) {
    matchedKnown = true;
    return new NSTouchCapture(host, parent);
  }
#endif

#ifdef Q_OS_LINUX
  if (wantAuto || pref == QStringLiteral("evdev")) {
    matchedKnown = true;
    auto* evdev = new EvdevTrackpadSource(parent);
    if (evdev->isAvailable()) {
      return evdev;
    }
    delete evdev;
    qWarning("evdev touchpad not available; falling back to qt-touch "
             "(is the user in the `input` group?)");
    if (fallbackNote != nullptr) {
      *fallbackNote = QObject::tr("No readable touchpad found (is your user in the `input` "
                                  "group?); hold-to-write uses the cursor instead.");
    }
  }
#endif

#ifdef Q_OS_WIN
  if (wantAuto || pref == QStringLiteral("rawinput")) {
    matchedKnown = true;
    auto* raw = new RawInputTrackpadSource(host, parent);
    if (raw->isAvailable()) {
      return raw;
    }
    delete raw;
    qWarning("rawinput digitizer not available; falling back to qt-touch");
    if (fallbackNote != nullptr) {
      *fallbackNote = QObject::tr("No precision touchpad found; hold-to-write uses the cursor instead.");
    }
  }
#endif

  if (!matchedKnown) {
    qWarning("HANDWRITING_CAPTURE=%s is not recognized on this platform; using qt-touch",
             qPrintable(pref));
  }

  const QStringList known{QStringLiteral("qt"), QStringLiteral("nstouch"), QStringLiteral("evdev"),
                          QStringLiteral("rawinput")};
  if (!pref.isEmpty() && !known.contains(pref)) {
    qWarning("unknown HANDWRITING_CAPTURE=%s (expected qt|nstouch|evdev|rawinput)",
             qPrintable(pref));
  }
  return new QtTouchCapture(parent);
}
