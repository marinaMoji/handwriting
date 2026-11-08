#pragma once

#include "ZinniaBackend.h"

#include <QMainWindow>

class DrawingCanvas;
class CandidatePanel;
class QComboBox;
class QLabel;
class QPushButton;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(ZinniaBackend* backend, QWidget* parent = nullptr);

 private slots:
  void onStrokesChanged();
  void onRecognitionFinished();
  void copySelected();
  void clearAll();
  void undoStroke();

 private:
  RecognitionMode currentMode() const;
  void updateDiagnostics(const RecognitionResult& result);
  void runRecognition();

  ZinniaBackend* backend_ = nullptr;
  DrawingCanvas* canvas_ = nullptr;
  CandidatePanel* candidates_ = nullptr;
  QComboBox* modeCombo_ = nullptr;
  QLabel* diagnostics_ = nullptr;
  QPushButton* copyButton_ = nullptr;
  int recognitionGeneration_ = 0;
};
