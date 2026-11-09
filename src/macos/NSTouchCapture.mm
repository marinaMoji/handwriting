#include "NSTouchCapture.h"

#import <AppKit/AppKit.h>

#include <QWidget>

NSTouchCapture::NSTouchCapture(QWidget* host, QObject* parent)
    : TrackpadSource(parent), host_(host) {}

NSTouchCapture::~NSTouchCapture() {
  setActive(false);
}

bool NSTouchCapture::startBackend() {
  if (host_ == nullptr || host_->window() == nullptr) {
    return false;
  }
  NSView* view = (__bridge NSView*)reinterpret_cast<void*>(host_->window()->winId());
  if (view == nil) {
    return false;
  }

  savedAllowedTypes_ = static_cast<quint64>(view.allowedTouchTypes);
  savedWantsResting_ = view.wantsRestingTouches;
  savedValid_ = true;
  view.allowedTouchTypes = NSTouchTypeMaskIndirect;
  view.wantsRestingTouches = YES;

  ids_.clear();
  nextId_ = 1;
  id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskGesture
                                                     handler:^NSEvent*(NSEvent* event) {
                                                       this->handleGestureEvent(
                                                           (__bridge void*)event);
                                                       return event;
                                                     }];
  monitor_ = (__bridge_retained void*)monitor;
  return monitor_ != nullptr;
}

void NSTouchCapture::stopBackend() {
  if (monitor_ != nullptr) {
    id monitor = (__bridge_transfer id)monitor_;
    [NSEvent removeMonitor:monitor];
    monitor_ = nullptr;
  }
  if (savedValid_ && host_ != nullptr && host_->window() != nullptr) {
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(host_->window()->winId());
    if (view != nil) {
      view.allowedTouchTypes = static_cast<NSTouchTypeMask>(savedAllowedTypes_);
      view.wantsRestingTouches = savedWantsResting_;
    }
  }
  savedValid_ = false;
}

void NSTouchCapture::handleGestureEvent(void* nsEvent) {
  if (!isActive()) {
    return;
  }
  NSEvent* event = (__bridge NSEvent*)nsEvent;
  static const bool debug = qEnvironmentVariableIsSet("HANDWRITING_DEBUG_TOUCH");

  NSSet<NSTouch*>* touches = [event touchesMatchingPhase:NSTouchPhaseTouching inView:nil];
  QVector<TrackpadContact> contacts;
  for (NSTouch* touch in touches) {
    if (touch.type != NSTouchTypeIndirect) {
      continue;
    }
    const quint64 key = static_cast<quint64>([touch.identity hash]);
    auto it = ids_.find(key);
    if (it == ids_.end()) {
      it = ids_.insert(key, nextId_++);
    }
    if (touch.deviceSize.height > 0.0) {
      reportPadAspect(touch.deviceSize.width / touch.deviceSize.height);
    }
    TrackpadContact c;
    c.identifier = it.value();
    // normalizedPosition origin is bottom-left; Qt/our contract is top-left.
    c.normalized = QPointF(touch.normalizedPosition.x, 1.0 - touch.normalizedPosition.y);
    if (debug) {
      qInfo("nstouch id=%d norm=(%.3f, %.3f) deviceSize=(%.1f, %.1f) resting=%d", c.identifier,
            c.normalized.x(), c.normalized.y(), touch.deviceSize.width, touch.deviceSize.height,
            touch.resting ? 1 : 0);
    }
    contacts.push_back(c);
  }
  if (contacts.isEmpty()) {
    ids_.clear();
  }
  processContacts(contacts);
}
