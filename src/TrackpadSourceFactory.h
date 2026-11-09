#pragma once

#include <QString>

class QObject;
class QWidget;
class TrackpadSource;

// Prefer a native absolute-trackpad backend for the current OS; fall back to
// QtTouchCapture. Override with HANDWRITING_CAPTURE=qt|nstouch|evdev|rawinput.
// If a preferred native backend could not be used, *fallbackNote explains why.
TrackpadSource* createTrackpadSource(QWidget* host, QObject* parent = nullptr,
                                     QString* fallbackNote = nullptr);
