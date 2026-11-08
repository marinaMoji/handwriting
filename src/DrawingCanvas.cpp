#include "DrawingCanvas.h"

#include "macos/TrackpadStrokeCapture.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

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
    // Mouse-fallback stroke break only (absolute mode ends on finger lift).
    if (drawSource_ == DrawSource::HoldToWrite && !absoluteTrackpadDriving()) {
      finishStroke();
    }
  });

  trackpadCapture_ = new TrackpadStrokeCapture(this, this);
  connect(trackpadCapture_, &TrackpadStrokeCapture::strokeBegan, this,
          &DrawingCanvas::onTrackpadStrokeBegan);
  connect(trackpadCapture_, &TrackpadStrokeCapture::strokeMoved, this,
          &DrawingCanvas::onTrackpadStrokeMoved);
  connect(trackpadCapture_, &TrackpadStrokeCapture::strokeEnded, this,
          &DrawingCanvas::onTrackpadStrokeEnded);
}

void DrawingCanvas::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
}

bool DrawingCanvas::absoluteTrackpadDriving() const {
  return trackpadCapture_ && trackpadCapture_->isActive() &&
         trackpadCapture_->hasReceivedContact();
}

void DrawingCanvas::clearStrokes() {
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
  updateTracking();
  setFocus(Qt::ShortcutFocusReason);
  if (trackpadCapture_ && trackpadCapture_->isAvailable()) {
    trackpadCapture_->setActive(true);
  }
  update();
  emit holdToWriteChanged(true);
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
  updateTracking();
  update();
  emit holdToWriteChanged(false);
}

void DrawingCanvas::updateTracking() {
  // Keep mouse-move hold fallback until MultitouchSupport proves it is driving.
  setMouseTracking(holdToWrite_ || drawSource_ == DrawSource::ClickDrag);
}

StrokePoint DrawingCanvas::toLogical(const QPointF& widgetPos) const {
  const qreal sx = logicalWidth() / qMax<qreal>(1.0, width());
  const qreal sy = logicalHeight() / qMax<qreal>(1.0, height());
  StrokePoint p;
  p.x = qBound(0, static_cast<int>(widgetPos.x() * sx), logicalWidth() - 1);
  p.y = qBound(0, static_cast<int>(widgetPos.y() * sy), logicalHeight() - 1);
  return p;
}

StrokePoint DrawingCanvas::fromNormalizedTrackpad(QPointF normalized) const {
  // Full trackpad surface ↔ full canvas. Corners match; cannot go offscreen.
  const qreal x = qBound<qreal>(0.0, normalized.x(), 1.0);
  const qreal y = qBound<qreal>(0.0, normalized.y(), 1.0);
  StrokePoint p;
  p.x = static_cast<int>(x * (logicalWidth() - 1) + 0.5);
  p.y = static_cast<int>(y * (logicalHeight() - 1) + 0.5);
  return p;
}

int DrawingCanvas::distanceSquared(const StrokePoint& a, const StrokePoint& b) {
  const int dx = a.x - b.x;
  const int dy = a.y - b.y;
  return dx * dx + dy * dy;
}

void DrawingCanvas::startStroke(const StrokePoint& point, DrawSource source) {
  holdStrokeIdleTimer_->stop();
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
    bumpHoldStrokeTimer();
    return;
  }

  if (!currentStroke_.isEmpty()) {
    const int jumpLimit = kHoldJumpThreshold * kHoldJumpThreshold;
    if (distanceSquared(currentStroke_.last(), point) >= jumpLimit) {
      finishStroke();
      startStroke(point, DrawSource::HoldToWrite);
      bumpHoldStrokeTimer();
      return;
    }
  }

  appendPoint(point);
  bumpHoldStrokeTimer();
}

void DrawingCanvas::finishStroke() {
  holdStrokeIdleTimer_->stop();
  if (drawSource_ == DrawSource::None) {
    return;
  }
  if (!currentStroke_.isEmpty()) {
    strokes_.push_back(currentStroke_);
  }
  currentStroke_.clear();
  drawSource_ = DrawSource::None;
  updateTracking();
  update();
  emit strokesChanged();
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
    finishStroke();
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
  if (drawSource_ != DrawSource::HoldToWrite) {
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
  const QColor bg = holdToWrite_ ? QColor(235, 245, 255) : QColor(250, 248, 242);
  painter.fillRect(rect(), bg);

  painter.setPen(QPen(QColor(210, 205, 195), 1, Qt::DotLine));
  const int step = width() / 3;
  if (step > 0) {
    painter.drawLine(step, 0, step, height());
    painter.drawLine(step * 2, 0, step * 2, height());
    painter.drawLine(0, height() / 3, width(), height() / 3);
    painter.drawLine(0, (height() * 2) / 3, width(), (height() * 2) / 3);
  }

  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(QPen(QColor(20, 20, 20), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

  const qreal sx = width() / static_cast<qreal>(logicalWidth());
  const qreal sy = height() / static_cast<qreal>(logicalHeight());

  auto drawStroke = [&](const Stroke& stroke) {
    if (stroke.size() < 2) {
      if (stroke.size() == 1) {
        painter.drawPoint(QPointF(stroke[0].x * sx, stroke[0].y * sy));
      }
      return;
    }
    for (int i = 1; i < stroke.size(); ++i) {
      painter.drawLine(QPointF(stroke[i - 1].x * sx, stroke[i - 1].y * sy),
                       QPointF(stroke[i].x * sx, stroke[i].y * sy));
    }
  };

  for (const Stroke& stroke : strokes_) {
    drawStroke(stroke);
  }
  if (drawSource_ != DrawSource::None) {
    drawStroke(currentStroke_);
  }

  if (holdToWrite_ && trackpadFingerVisible_) {
    const StrokePoint tip = fromNormalizedTrackpad(trackpadFingerNormalized_);
    const QPointF tipPx(tip.x * sx, tip.y * sy);
    painter.setBrush(QColor(40, 110, 200, 160));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(tipPx, 6.0, 6.0);
  }

  const QColor border = holdToWrite_ ? QColor(40, 110, 200) : QColor(140, 140, 140);
  painter.setPen(QPen(border, holdToWrite_ ? 2 : 1));
  painter.drawRect(rect().adjusted(0, 0, -1, -1));

  painter.setPen(QColor(100, 100, 100));
  QFont hint = font();
  hint.setPointSize(11);
  painter.setFont(hint);
  QString hintText;
  if (holdToWrite_) {
    if (absoluteTrackpadDriving()) {
      hintText = tr("Absolute trackpad: corners match canvas · lift between strokes · release Space");
    } else if (trackpadCapture_ && trackpadCapture_->isAvailable()) {
      hintText = tr("Hold Space: touch the trackpad (absolute) · or move cursor as fallback");
    } else {
      hintText = tr("Hold-to-write (cursor fallback): pause between strokes · release Space");
    }
  } else {
    hintText = tr("Click-drag to draw · or hold Space and write on the trackpad");
  }
  painter.drawText(rect().adjusted(8, 8, -8, -8), Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
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

  // Cursor fallback while Space is held, until absolute Multitouch takes over.
  if (holdToWrite_ && !absoluteTrackpadDriving()) {
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
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    beginHoldToWrite();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void DrawingCanvas::keyReleaseEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
    endHoldToWrite();
    event->accept();
    return;
  }
  QWidget::keyReleaseEvent(event);
}

void DrawingCanvas::focusOutEvent(QFocusEvent* event) {
  // Don't cancel hold-to-write on focus changes; Space release ends it.
  QWidget::focusOutEvent(event);
}

void DrawingCanvas::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  update();
}
