#pragma once

#include "ZinniaBackend.h"

#include <QElapsedTimer>
#include <QWidget>

class QTimer;
class TrackpadSource;
class QtTouchCapture;

class DrawingCanvas : public QWidget {
  Q_OBJECT

 public:
  explicit DrawingCanvas(QWidget* parent = nullptr);
  ~DrawingCanvas() override;

  QVector<Stroke> strokes() const { return strokes_; }
  // Recognition canvas size (what Zinnia sees after cropping).
  int logicalWidth() const { return 300; }
  int logicalHeight() const { return 300; }
  // Drawing surface in stroke coordinates: pad-shaped so the whole trackpad is usable.
  int surfaceWidth() const;
  int surfaceHeight() const { return 300; }
  QSize sizeHint() const override { return QSize(480, 300); }
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
  bool event(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void changeEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private slots:
  void onTrackpadFirstContact();
  void onTrackpadStrokeBegan(QPointF normalized);
  void onTrackpadStrokeMoved(QPointF normalized);
  void onTrackpadStrokeEnded();

 private:
  enum class DrawSource {
    None,
    ClickDrag,
    HoldToWrite,
  };

  QRectF surfaceRect() const;  // letterboxed drawing area inside the widget
  StrokePoint toLogical(const QPointF& widgetPos) const;
  StrokePoint fromNormalizedTrackpad(QPointF normalized) const;
  void startStroke(const StrokePoint& point, DrawSource source);
  void appendPoint(const StrokePoint& point);
  void appendHoldToWritePoint(const StrokePoint& point);
  void finishStroke();
  void cancelStroke();  // drop the in-progress stroke without committing it
  void bumpHoldStrokeTimer();
  void updateTracking();
  void setCursorFrozen(bool frozen);
  bool absoluteTrackpadDriving() const;

  QVector<Stroke> strokes_;
  Stroke currentStroke_;
  DrawSource drawSource_ = DrawSource::None;
  bool holdToWrite_ = false;
  QTimer* holdStrokeIdleTimer_ = nullptr;
  TrackpadSource* trackpadCapture_ = nullptr;
  QtTouchCapture* qtTouch_ = nullptr;  // same object as trackpadCapture_ when the Qt backend is used
  bool cursorFrozen_ = false;
  QPointF trackpadFingerNormalized_{-1.0, -1.0};
  bool trackpadFingerVisible_ = false;
  bool cursorStroke_ = false;  // in-progress hold stroke came from cursor fallback
  QElapsedTimer holdBeganAt_;
};
