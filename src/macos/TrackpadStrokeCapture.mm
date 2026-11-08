#import "TrackpadStrokeCapture.h"

#import <AppKit/AppKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import <dlfcn.h>

#include <QMetaObject>
#include <QMetaType>
#include <QVector>
#include <QWidget>

Q_DECLARE_METATYPE(QVector<QPointF>)

namespace {

struct MTPoint {
  float x;
  float y;
};

// Layout used by MultitouchSupport contact-frame callbacks (undocumented).
struct MTTouch {
  int frame;
  double timestamp;
  int identifier;
  int state;
  int foo3;
  int foo4;
  MTPoint normalized;
  float size;
  int zero1;
  float angle;
  float majorAxis;
  float minorAxis;
  MTPoint unknown2;
  int zero2[2];
  float density;
};

enum {
  MTTouchStateNotTracking = 0,
  MTTouchStateStartInRange = 1,
  MTTouchStateHoverInRange = 2,
  MTTouchStateMakeTouch = 3,
  MTTouchStateTouching = 4,
  MTTouchStateBreakTouch = 5,
  MTTouchStateLingerInRange = 6,
  MTTouchStateOutOfRange = 7
};

using MTDeviceRef = void*;
using MTContactCallback =
    int (*)(int device, MTTouch* touches, int numTouches, double timestamp, int frame);

using FnMTDeviceCreateDefault = MTDeviceRef (*)();
using FnMTDeviceCreateList = CFMutableArrayRef (*)();
using FnMTRegisterContactFrameCallback = void (*)(MTDeviceRef, MTContactCallback);
using FnMTUnregisterContactFrameCallback = void (*)(MTDeviceRef, MTContactCallback);
using FnMTDeviceStart = void (*)(MTDeviceRef, int);
using FnMTDeviceStop = void (*)(MTDeviceRef);

FnMTDeviceCreateDefault gCreateDefault = nullptr;
FnMTDeviceCreateList gCreateList = nullptr;
FnMTRegisterContactFrameCallback gRegister = nullptr;
FnMTUnregisterContactFrameCallback gUnregister = nullptr;
FnMTDeviceStart gStart = nullptr;
FnMTDeviceStop gStop = nullptr;

TrackpadStrokeCapture* gActiveCapture = nullptr;

bool loadMultitouchSymbols() {
  static bool attempted = false;
  static bool ok = false;
  if (attempted) {
    return ok;
  }
  attempted = true;

  NSBundle* bundle = [NSBundle
      bundleWithPath:@"/System/Library/PrivateFrameworks/MultitouchSupport.framework"];
  if (bundle == nil || ![bundle load]) {
    // Fallback: dyld shared cache resolution.
    dlopen("/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport",
           RTLD_LAZY);
  }

  gCreateDefault = (FnMTDeviceCreateDefault)dlsym(RTLD_DEFAULT, "MTDeviceCreateDefault");
  gCreateList = (FnMTDeviceCreateList)dlsym(RTLD_DEFAULT, "MTDeviceCreateList");
  gRegister =
      (FnMTRegisterContactFrameCallback)dlsym(RTLD_DEFAULT, "MTRegisterContactFrameCallback");
  gUnregister = (FnMTUnregisterContactFrameCallback)dlsym(RTLD_DEFAULT,
                                                          "MTUnregisterContactFrameCallback");
  gStart = (FnMTDeviceStart)dlsym(RTLD_DEFAULT, "MTDeviceStart");
  gStop = (FnMTDeviceStop)dlsym(RTLD_DEFAULT, "MTDeviceStop");

  ok = gRegister && gStart && (gCreateDefault || gCreateList);
  return ok;
}

int contactCallback(int device, MTTouch* touches, int numTouches, double timestamp, int frame) {
  (void)device;
  (void)timestamp;
  (void)frame;
  TrackpadStrokeCapture* capture = gActiveCapture;
  if (capture == nullptr || touches == nullptr || numTouches <= 0) {
    // Still need to signal lift when numTouches is 0.
    if (capture != nullptr && numTouches == 0) {
      QMetaObject::invokeMethod(capture, "onContacts", Qt::QueuedConnection,
                                Q_ARG(QVector<QPointF>, {}));
    }
    return 0;
  }

  QVector<QPointF> points;
  points.reserve(numTouches);
  for (int i = 0; i < numTouches; ++i) {
    const MTTouch& t = touches[i];
    // Only solid contact, not hover.
    if (t.state != MTTouchStateTouching && t.state != MTTouchStateMakeTouch) {
      continue;
    }
    // normalized: bottom-left origin → Qt top-left.
    points.push_back(QPointF(t.normalized.x, 1.0 - t.normalized.y));
  }

  QMetaObject::invokeMethod(capture, "onContacts", Qt::QueuedConnection,
                            Q_ARG(QVector<QPointF>, points));
  return 0;
}

}  // namespace

TrackpadStrokeCapture::TrackpadStrokeCapture(QWidget* host, QObject* parent)
    : QObject(parent), host_(host) {
  available_ = loadMultitouchSymbols();
  qRegisterMetaType<QVector<QPointF>>("QVector<QPointF>");
}

TrackpadStrokeCapture::~TrackpadStrokeCapture() {
  stopDevice();
}

void TrackpadStrokeCapture::startDevice() {
  if (!devices_.isEmpty() || !available_) {
    return;
  }

  // Prefer the full device list (built-in trackpad + Magic Trackpad, etc.).
  if (gCreateList) {
    CFMutableArrayRef list = gCreateList();
    if (list != nullptr) {
      deviceList_ = (void*)CFRetain(list);
      const CFIndex count = CFArrayGetCount(list);
      for (CFIndex i = 0; i < count; ++i) {
        MTDeviceRef device = (MTDeviceRef)CFArrayGetValueAtIndex(list, i);
        if (device == nullptr) {
          continue;
        }
        gRegister(device, contactCallback);
        gStart(device, 0);
        devices_.push_back(device);
      }
      CFRelease(list);
    }
  }

  if (devices_.isEmpty() && gCreateDefault) {
    MTDeviceRef device = gCreateDefault();
    if (device != nullptr) {
      gRegister(device, contactCallback);
      gStart(device, 0);
      devices_.push_back(device);
    }
  }

  if (devices_.isEmpty()) {
    available_ = false;
  }
}

void TrackpadStrokeCapture::stopDevice() {
  for (void* device : devices_) {
    if (gUnregister) {
      gUnregister((MTDeviceRef)device, contactCallback);
    }
    if (gStop) {
      gStop((MTDeviceRef)device);
    }
  }
  devices_.clear();
  if (deviceList_ != nullptr) {
    CFRelease((CFArrayRef)deviceList_);
    deviceList_ = nullptr;
  }
}

void TrackpadStrokeCapture::setActive(bool active) {
  active_ = active;
  receivedContact_ = false;
  touching_ = false;
  activeFingerId_ = -1;

  if (active) {
    gActiveCapture = this;
    startDevice();
  } else {
    if (gActiveCapture == this) {
      gActiveCapture = nullptr;
    }
    if (touching_) {
      emit strokeEnded();
      touching_ = false;
    }
    stopDevice();
  }
}

void TrackpadStrokeCapture::onContacts(const QVector<QPointF>& touchingPoints) {
  if (!active_) {
    return;
  }

  if (touchingPoints.isEmpty()) {
    if (touching_) {
      touching_ = false;
      emit strokeEnded();
    }
    return;
  }

  receivedContact_ = true;
  // Use the first touching finger; map its absolute pad position to the canvas.
  const QPointF p = touchingPoints.first();
  if (!touching_) {
    touching_ = true;
    emit strokeBegan(p);
  } else {
    emit strokeMoved(p);
  }
}
