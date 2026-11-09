#include "DrawingCanvas.h"

#include "CursorFreeze.h"
#include "HoldKey.h"
#include "QtTouchCapture.h"
#include "StrokeMapping.h"
#include "TrackpadSource.h"
#include "TrackpadSourceFactory.h"

#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QTouchEvent>
#include <QWindow>

namespace {
constexpr int kHoldStrokeIdleMs = 120;
constexpr int kHoldJumpThreshold = 40;
}

DrawingCanvas::DrawingCanvas(QWidget* parent) : QWidget(parent) {
  setMinimumSize(120, 120);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMouseTracking(false);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);

  holdStrokeIdleTimer_ = new QTimer(this);
  holdStrokeIdleTimer_->setSingleShot(true);
  holdStrokeIdleTimer_->setInterval(kHoldStrokeIdleMs);
  connect(holdStrokeIdleTimer_, &QTimer::timeout, this, [this]() {
    if (drawSource_ == DrawSource::HoldToWrite && !absoluteTrackpadDriving()) {
      finishStroke();
    }
  });

  setAttribute(Qt::WA_AcceptTouchEvents, true);

  // Platform native absolute backends plug into TrackpadSource; Qt touch is the portable fallback.
  // Override with HANDWRITING_CAPTURE=qt|nstouch|evdev|rawinput.
  trackpadCapture_ = createTrackpadSource(this, this, &backendNote_);
  qtTouch_ = qobject_cast<QtTouchCapture*>(trackpadCapture_);
  qInfo("trackpad capture backend: %s", qPrintable(trackpadCapture_->name()));
  bool aspectOk = false;
  const qreal forced = qEnvironmentVariable("HANDWRITING_PAD_ASPECT").toDouble(&aspectOk);
  if (aspectOk && forced > 0.0) {
    padAspect_ = forced;
    padAspectFixed_ = true;
  }
  connect(trackpadCapture_, &TrackpadSource::problem, this, &DrawingCanvas::trackpadProblem);
  connect(trackpadCapture_, &TrackpadSource::padAspectChanged, this,
          &DrawingCanvas::onPadAspectChanged);
  connect(trackpadCapture_, &TrackpadSource::firstContact, this,
          &DrawingCanvas::onTrackpadFirstContact);
  connect(trackpadCapture_, &TrackpadSource::strokeBegan, this,
          &DrawingCanvas::onTrackpadStrokeBegan);
  connect(trackpadCapture_, &TrackpadSource::strokeMoved, this,
          &DrawingCanvas::onTrackpadStrokeMoved);
  connect(trackpadCapture_, &TrackpadSource::strokeEnded, this,
          &DrawingCanvas::onTrackpadStrokeEnded);
}

bool DrawingCanvas::absoluteTrackpadDriving() const {
  return trackpadCapture_ && trackpadCapture_->isActive() &&
         trackpadCapture_->hasReceivedContact();
}

void DrawingCanvas::clearStrokes() {
  usedHold_ = false;
  usedClick_ = false;
  holdStrokeIdleTimer_->stop();
  strokes_.clear();
  currentStroke_.clear();
  drawSource_ = DrawSource::None;
  update();
  emit strokesChanged();
}

void DrawingCanvas::undoLastStroke() {
  if (drawSource_ != DrawSource::None) {
    holdStrokeIdleTimer_->stop();
    currentStroke_.clear();
    drawSource_ = DrawSource::None;
    update();
    return;
  }
  if (strokes_.isEmpty()) {
    return;
  }
  strokes_.removeLast();
  update();
  emit strokesChanged();
}

void DrawingCanvas::beginHoldToWrite() {
  if (holdToWrite_) {
    return;
  }
  holdToWrite_ = true;
  trackpadFingerVisible_ = false;
  holdBeganAt_.start();
  // Keep the pointer over the canvas so a stray press cannot land on another window.
  if (!rect().contains(mapFromGlobal(QCursor::pos()))) {
    QCursor::setPos(mapToGlobal(rect().center()));
  }
  updateTracking();
  setFocus(Qt::ShortcutFocusReason);
  if (trackpadCapture_ && trackpadCapture_->isAvailable()) {
    trackpadCapture_->setActive(true);
  }
  update();
  emit holdToWriteChanged(true);
}

DrawingCanvas::~DrawingCanvas() {
  setCursorFrozen(false);
}

QString DrawingCanvas::inputSourceLabel() const {
  if (usedHold_ && usedClick_) {
    return QStringLiteral("mixed");
  }
  if (usedHold_) {
    return QStringLiteral("hold-to-write");
  }
  return usedClick_ ? QStringLiteral("click-drag") : QString();
}

void DrawingCanvas::setCursorFrozen(bool frozen) {
  if (frozen == cursorFrozen_) {
    return;
  }
  cursorFrozen_ = frozen;
  cursor_freeze::set(frozen);
}

void DrawingCanvas::onPadAspectChanged(qreal aspect) {
  if (padAspectFixed_ || qAbs(aspect - padAspect_) < 0.005) {
    return;
  }
  // Keep existing ink where it is relative to the (resized) surface.
  const int oldW = surfaceWidth();
  padAspect_ = aspect;
  const qreal ratio = static_cast<qreal>(surfaceWidth()) / oldW;
  auto rescale = [&](Stroke& st) {
    for (StrokePoint& p : st) {
      p.x = qBound(0, static_cast<int>(p.x * ratio + 0.5), surfaceWidth() - 1);
    }
  };
  for (Stroke& st : strokes_) {
    rescale(st);
  }
  rescale(currentStroke_);
  update();
}

void DrawingCanvas::onTrackpadFirstContact() {
  // Absolute touches are flowing: stop finger movement from dragging the pointer around.
  if (holdToWrite_) {
    setCursorFrozen(true);
  }
}

bool DrawingCanvas::event(QEvent* e) {
  switch (e->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
      if (qtTouch_ != nullptr && qtTouch_->handleTouchEvent(static_cast<QTouchEvent*>(e))) {
        e->accept();
        return true;
      }
      break;
    default:
      break;
  }
  return QWidget::event(e);
}

void DrawingCanvas::endHoldToWrite() {
  if (!holdToWrite_) {
    return;
  }
  if (drawSource_ == DrawSource::HoldToWrite) {
    finishStroke();
  }
  holdToWrite_ = false;
  trackpadFingerVisible_ = false;
  holdStrokeIdleTimer_->stop();
  if (trackpadCapture_) {
    trackpadCapture_->setActive(false);
  }
  setCursorFrozen(false);
  updateTracking();
  update();
  emit holdToWriteChanged(false);
}

void DrawingCanvas::updateTracking() {
  setMouseTracking(holdToWrite_ || drawSource_ == DrawSource::ClickDrag);
}

int DrawingCanvas::surfaceWidth() const {
  return stroke_mapping::surfaceWidthFor(surfaceHeight(), padAspect_);
}

QRectF DrawingCanvas::surfaceRect() const {
  const qreal aspect = static_cast<qreal>(surfaceWidth()) / surfaceHeight();
  qreal w = width();
  qreal h = w / aspect;
  if (h > height()) {
    h = height();
    w = h * aspect;
  }
  return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
}

StrokePoint DrawingCanvas::toLogical(const QPointF& widgetPos) const {
  const QRectF r = surfaceRect();
  const qreal s = surfaceWidth() / qMax<qreal>(1.0, r.width());
  StrokePoint p;
  p.x = qBound(0, static_cast<int>((widgetPos.x() - r.left()) * s), surfaceWidth() - 1);
  p.y = qBound(0, static_cast<int>((widgetPos.y() - r.top()) * s), surfaceHeight() - 1);
  return p;
}

StrokePoint DrawingCanvas::fromNormalizedTrackpad(QPointF normalized) const {
  return stroke_mapping::fromNormalizedTrackpad(normalized, surfaceWidth(), surfaceHeight());
}

void DrawingCanvas::startStroke(const StrokePoint& point, DrawSource source) {
  (source == DrawSource::ClickDrag ? usedClick_ : usedHold_) = true;
  holdStrokeIdleTimer_->stop();
  cursorStroke_ = false;
  drawSource_ = source;
  currentStroke_.clear();
  currentStroke_.push_back(point);
  updateTracking();
  update();
}

void DrawingCanvas::appendPoint(const StrokePoint& point) {
  if (currentStroke_.isEmpty()) {
    currentStroke_.push_back(point);
    update();
    return;
  }
  const StrokePoint& last = currentStroke_.last();
  if (last.x == point.x && last.y == point.y) {
    return;
  }
  currentStroke_.push_back(point);
  update();
}

void DrawingCanvas::appendHoldToWritePoint(const StrokePoint& point) {
  if (drawSource_ != DrawSource::HoldToWrite) {
    startStroke(point, DrawSource::HoldToWrite);
    cursorStroke_ = true;
    bumpHoldStrokeTimer();
    return;
  }

  if (!currentStroke_.isEmpty() &&
      stroke_mapping::isHoldJump(currentStroke_.last(), point, kHoldJumpThreshold)) {
    finishStroke();
    startStroke(point, DrawSource::HoldToWrite);
    cursorStroke_ = true;
    bumpHoldStrokeTimer();
    return;
  }

  appendPoint(point);
  bumpHoldStrokeTimer();
}

void DrawingCanvas::finishStroke() {
  holdStrokeIdleTimer_->stop();
  if (drawSource_ == DrawSource::None) {
    return;
  }
  // A lone point from cursor fallback is noise (e.g. the pointer being moved), not a stroke.
  const bool noise = cursorStroke_ && currentStroke_.size() < 2;
  if (!currentStroke_.isEmpty() && !noise) {
    strokes_.push_back(currentStroke_);
  }
  currentStroke_.clear();
  cursorStroke_ = false;
  drawSource_ = DrawSource::None;
  updateTracking();
  update();
  if (!noise) {
    emit strokesChanged();
  }
}

void DrawingCanvas::cancelStroke() {
  holdStrokeIdleTimer_->stop();
  currentStroke_.clear();
  cursorStroke_ = false;
  drawSource_ = DrawSource::None;
  updateTracking();
  update();
}

void DrawingCanvas::bumpHoldStrokeTimer() {
  if (drawSource_ == DrawSource::HoldToWrite && !absoluteTrackpadDriving()) {
    holdStrokeIdleTimer_->start();
  }
}

void DrawingCanvas::onTrackpadStrokeBegan(QPointF normalized) {
  if (!holdToWrite_) {
    return;
  }
  trackpadFingerNormalized_ = normalized;
  trackpadFingerVisible_ = true;
  if (drawSource_ == DrawSource::HoldToWrite) {
    // Absolute touch takes over: drop any cursor-fallback stroke instead of committing it.
    cursorStroke_ ? cancelStroke() : finishStroke();
  }
  startStroke(fromNormalizedTrackpad(normalized), DrawSource::HoldToWrite);
  updateTracking();
}

void DrawingCanvas::onTrackpadStrokeMoved(QPointF normalized) {
  if (!holdToWrite_) {
    return;
  }
  trackpadFingerNormalized_ = normalized;
  trackpadFingerVisible_ = true;
  const StrokePoint point = fromNormalizedTrackpad(normalized);
  if (drawSource_ == DrawSource::HoldToWrite && cursorStroke_) {
    cancelStroke();
  }
  if (drawSource_ != DrawSource::HoldToWrite) {
    startStroke(point, DrawSource::HoldToWrite);
  } else if (!currentStroke_.isEmpty() &&
             stroke_mapping::isHoldJump(currentStroke_.last(), point, kHoldJumpThreshold)) {
    // Belt-and-suspenders with TrackpadSource jump splitting: if a lift was
    // missed and we only see a teleporting move, still break the line.
    finishStroke();
    startStroke(point, DrawSource::HoldToWrite);
  } else {
    appendPoint(point);
  }
  update();
}

void DrawingCanvas::onTrackpadStrokeEnded() {
  if (!holdToWrite_) {
    return;
  }
  trackpadFingerVisible_ = false;
  if (drawSource_ == DrawSource::HoldToWrite) {
    finishStroke();
  } else {
    update();
  }
}

void DrawingCanvas::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), palette().window());

  const QRectF area = surfaceRect();
  const QColor bg = holdToWrite_ ? QColor(235, 245, 255) : QColor(250, 248, 242);
  painter.fillRect(area, bg);

  painter.setPen(QPen(QColor(210, 205, 195), 1, Qt::DotLine));
  for (int i = 1; i < 3; ++i) {
    const qreal x = area.left() + area.width() * i / 3.0;
    const qreal y = area.top() + area.height() * i / 3.0;
    painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
    painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
  }

  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(QColor(20, 20, 20), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  const qreal sc = area.width() / static_cast<qreal>(surfaceWidth());
  auto toPx = [&](const StrokePoint& p) {
    return QPointF(area.left() + p.x * sc, area.top() + p.y * sc);
  };

  auto drawStroke = [&](const Stroke& stroke) {
    if (stroke.size() == 1) {
      painter.drawPoint(toPx(stroke[0]));
      return;
    }
    for (int i = 1; i < stroke.size(); ++i) {
      painter.drawLine(toPx(stroke[i - 1]), toPx(stroke[i]));
    }
  };

  for (const Stroke& stroke : strokes_) {
    drawStroke(stroke);
  }
  if (drawSource_ != DrawSource::None) {
    drawStroke(currentStroke_);
  }

  if (holdToWrite_ && trackpadFingerVisible_) {
    painter.setBrush(QColor(40, 110, 200, 160));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(toPx(fromNormalizedTrackpad(trackpadFingerNormalized_)), 6.0, 6.0);
  }

  const QColor border = holdToWrite_ ? QColor(40, 110, 200) : QColor(140, 140, 140);
  painter.setPen(QPen(border, holdToWrite_ ? 2 : 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(area.adjusted(0.5, 0.5, -0.5, -0.5));

  painter.setPen(QColor(100, 100, 100));
  QFont hint = font();
  hint.setPointSize(11);
  painter.setFont(hint);
  QString hintText;
  if (holdToWrite_) {
    if (absoluteTrackpadDriving()) {
      hintText = tr("Whole trackpad = this area · write anywhere, it is cropped automatically · lift between strokes · release the ` key");
    } else if (trackpadCapture_ && trackpadCapture_->isAvailable()) {
      hintText = tr("Hold ` (key left of 1): touch the trackpad (absolute) · or move cursor as fallback");
    } else {
      hintText = tr("Hold-to-write (cursor fallback): pause between strokes · release the ` key");
    }
  } else {
    hintText = tr("Click-drag to draw · or hold ` (key left of 1) and write on the trackpad");
  }
  painter.drawText(area.adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                   hintText);
}

void DrawingCanvas::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  setFocus(Qt::MouseFocusReason);
  if (drawSource_ == DrawSource::HoldToWrite) {
    finishStroke();
  }
  startStroke(toLogical(event->position()), DrawSource::ClickDrag);
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent* event) {
  const bool leftDown = event->buttons() & Qt::LeftButton;
  const StrokePoint point = toLogical(event->position());

  if (leftDown) {
    if (drawSource_ != DrawSource::ClickDrag) {
      startStroke(point, DrawSource::ClickDrag);
    } else {
      appendPoint(point);
    }
    return;
  }

  // Ignore the move generated by our own cursor warp right after the key goes down.
  constexpr qint64 kWarpSettleMs = 250;
  if (holdToWrite_ && !absoluteTrackpadDriving() && holdBeganAt_.isValid() &&
      holdBeganAt_.elapsed() > kWarpSettleMs) {
    appendHoldToWritePoint(point);
  }
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  if (drawSource_ == DrawSource::ClickDrag) {
    appendPoint(toLogical(event->position()));
    finishStroke();
  }
}

void DrawingCanvas::keyPressEvent(QKeyEvent* event) {
  if (hold_key::isHoldToWriteKey(event) && !event->isAutoRepeat()) {
    beginHoldToWrite();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void DrawingCanvas::keyReleaseEvent(QKeyEvent* event) {
  if (hold_key::isHoldToWriteKey(event) && !event->isAutoRepeat()) {
    endHoldToWrite();
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

void DrawingCanvas::changeEvent(QEvent* event) {
  // Window deactivation (Cmd-Tab, dialog) — key release may never arrive.
  if (event->type() == QEvent::WindowDeactivate && holdToWrite_) {
    endHoldToWrite();
  }
  QWidget::changeEvent(event);
}

void DrawingCanvas::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  update();
}
