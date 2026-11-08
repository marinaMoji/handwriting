#include "DrawingCanvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

DrawingCanvas::DrawingCanvas(QWidget* parent) : QWidget(parent) {
  // Keep the minimum modest so layout can shrink; logical Zinnia space stays 300×300.
  setMinimumSize(120, 120);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMouseTracking(false);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setFocusPolicy(Qt::StrongFocus);
}

void DrawingCanvas::clearStrokes() {
  strokes_.clear();
  currentStroke_.clear();
  drawing_ = false;
  update();
  emit strokesChanged();
}

void DrawingCanvas::undoLastStroke() {
  if (strokes_.isEmpty()) {
    return;
  }
  strokes_.removeLast();
  update();
  emit strokesChanged();
}

StrokePoint DrawingCanvas::toLogical(const QPointF& widgetPos) const {
  const qreal sx = logicalWidth() / qMax<qreal>(1.0, width());
  const qreal sy = logicalHeight() / qMax<qreal>(1.0, height());
  StrokePoint p;
  p.x = qBound(0, static_cast<int>(widgetPos.x() * sx), logicalWidth() - 1);
  p.y = qBound(0, static_cast<int>(widgetPos.y() * sy), logicalHeight() - 1);
  return p;
}

void DrawingCanvas::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(250, 248, 242));

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
  if (drawing_) {
    drawStroke(currentStroke_);
  }

  painter.setPen(QColor(140, 140, 140));
  painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void DrawingCanvas::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  drawing_ = true;
  currentStroke_.clear();
  currentStroke_.push_back(toLogical(event->position()));
  update();
}

void DrawingCanvas::mouseMoveEvent(QMouseEvent* event) {
  if (!drawing_) {
    return;
  }
  currentStroke_.push_back(toLogical(event->position()));
  update();
}

void DrawingCanvas::mouseReleaseEvent(QMouseEvent* event) {
  if (!drawing_ || event->button() != Qt::LeftButton) {
    return;
  }
  currentStroke_.push_back(toLogical(event->position()));
  if (!currentStroke_.isEmpty()) {
    strokes_.push_back(currentStroke_);
  }
  currentStroke_.clear();
  drawing_ = false;
  update();
  emit strokesChanged();
}

void DrawingCanvas::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  update();
}
