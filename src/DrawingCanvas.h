#pragma once

#include "ZinniaBackend.h"

#include <QWidget>

class QTimer;
class TrackpadStrokeCapture;

class DrawingCanvas : public QWidget {
  Q_OBJECT

 public:
  explicit DrawingCanvas(QWidget* parent = nullptr);

  QVector<Stroke> strokes() const { return strokes_; }
  int logicalWidth() const { return 300; }
  int logicalHeight() const { return 300; }
  QSize sizeHint() const override { return QSize(300, 300); }
  bool holdToWriteActive() const { return holdToWrite_; }

 public slots:
  void clearStrokes();
  void undoLastStroke();
  void beginHoldToWrite();
  void endHoldToWrite();

 signals:
  void strokesChanged();
  void holdToWriteChanged(bool active);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void showEvent(QShowEvent* event) override;

 private slots:
  void onTrackpadStrokeBegan(QPointF normalized);
  void onTrackpadStrokeMoved(QPointF normalized);
  void onTrackpadStrokeEnded();

 private:
  enum class DrawSource {
    None,
    ClickDrag,
    HoldToWrite,
  };

  StrokePoint toLogical(const QPointF& widgetPos) const;
  StrokePoint fromNormalizedTrackpad(QPointF normalized) const;
  static int distanceSquared(const StrokePoint& a, const StrokePoint& b);
  void startStroke(const StrokePoint& point, DrawSource source);
  void appendPoint(const StrokePoint& point);
  void appendHoldToWritePoint(const StrokePoint& point);
  void finishStroke();
  void bumpHoldStrokeTimer();
  void updateTracking();
  bool absoluteTrackpadDriving() const;

  QVector<Stroke> strokes_;
  Stroke currentStroke_;
  DrawSource drawSource_ = DrawSource::None;
  bool holdToWrite_ = false;
  QTimer* holdStrokeIdleTimer_ = nullptr;
  TrackpadStrokeCapture* trackpadCapture_ = nullptr;
  QPointF trackpadFingerNormalized_{-1.0, -1.0};  // for on-canvas finger marker
  bool trackpadFingerVisible_ = false;
};
