#pragma once

#include "ZinniaBackend.h"

#include <QWidget>

class DrawingCanvas : public QWidget {
  Q_OBJECT

 public:
  explicit DrawingCanvas(QWidget* parent = nullptr);

  QVector<Stroke> strokes() const { return strokes_; }
  int logicalWidth() const { return 300; }
  int logicalHeight() const { return 300; }
  QSize sizeHint() const override { return QSize(300, 300); }

 public slots:
  void clearStrokes();
  void undoLastStroke();

 signals:
  void strokesChanged();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  StrokePoint toLogical(const QPointF& widgetPos) const;

  QVector<Stroke> strokes_;
  Stroke currentStroke_;
  bool drawing_ = false;
};
