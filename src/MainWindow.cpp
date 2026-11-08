#include "MainWindow.h"

#include "CandidatePanel.h"
#include "DrawingCanvas.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTextEdit>
#include <QtConcurrent>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(ZinniaBackend* backend, QWidget* parent)
    : QMainWindow(parent), backend_(backend) {
  setWindowTitle(tr("marinaMoji handwriting prototype"));
  resize(820, 620);

  auto* central = new QWidget(this);
  setCentralWidget(central);
  auto* root = new QHBoxLayout(central);

  auto* leftColumn = new QWidget(central);
  auto* left = new QVBoxLayout(leftColumn);
  left->setContentsMargins(0, 0, 0, 0);
  left->setSpacing(8);

  canvas_ = new DrawingCanvas(leftColumn);
  left->addWidget(canvas_, /*stretch*/ 1);

  // Fixed-height control bar so the Expanding canvas cannot cover the buttons.
  auto* controlBar = new QWidget(leftColumn);
  controlBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  auto* controls = new QHBoxLayout(controlBar);
  controls->setContentsMargins(0, 0, 0, 0);

  modeCombo_ = new QComboBox(controlBar);
  modeCombo_->addItem(tr("Japanese"), static_cast<int>(RecognitionMode::Japanese));
  modeCombo_->addItem(tr("Traditional Chinese"),
                      static_cast<int>(RecognitionMode::TraditionalChinese));
  modeCombo_->addItem(tr("Both"), static_cast<int>(RecognitionMode::Both));
  modeCombo_->setCurrentIndex(2);

  auto* clearBtn = new QPushButton(tr("Clear"), controlBar);
  auto* undoBtn = new QPushButton(tr("Undo"), controlBar);
  copyButton_ = new QPushButton(tr("Copy"), controlBar);
  copyButton_->setEnabled(false);

  controls->addWidget(new QLabel(tr("Mode:"), controlBar));
  controls->addWidget(modeCombo_, 1);
  controls->addWidget(undoBtn);
  controls->addWidget(clearBtn);
  controls->addWidget(copyButton_);
  left->addWidget(controlBar, /*stretch*/ 0);

  diagnostics_ = new QLabel(leftColumn);
  diagnostics_->setWordWrap(true);
  diagnostics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  diagnostics_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
  left->addWidget(diagnostics_, /*stretch*/ 0);

  candidates_ = new CandidatePanel(central);

  root->addWidget(leftColumn, 3);
  root->addWidget(candidates_, 2);

  connect(canvas_, &DrawingCanvas::strokesChanged, this, &MainWindow::onStrokesChanged);
  connect(canvas_, &DrawingCanvas::holdToWriteChanged, this, &MainWindow::onHoldToWriteChanged);
  connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::onStrokesChanged);
  connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearAll);
  connect(undoBtn, &QPushButton::clicked, this, &MainWindow::undoStroke);
  connect(copyButton_, &QPushButton::clicked, this, &MainWindow::copySelected);
  connect(candidates_, &CandidatePanel::selectionChanged, this, [this](const QString& ch) {
    copyButton_->setEnabled(!ch.isEmpty());
  });

  qApp->installEventFilter(this);
  canvas_->setFocus(Qt::OtherFocusReason);

  updateDiagnostics({});
  statusBar()->showMessage(
      tr("Click-drag or hold Space to write · JA: %1 · TW: %2")
          .arg(QFileInfo(backend_->japaneseModelPath()).fileName())
          .arg(QFileInfo(backend_->traditionalModelPath()).fileName()));
}

RecognitionMode MainWindow::currentMode() const {
  return static_cast<RecognitionMode>(modeCombo_->currentData().toInt());
}

bool MainWindow::shouldStealSpaceFromFocus() const {
  QWidget* focus = QApplication::focusWidget();
  if (!focus) {
    return true;
  }
  // Let real text fields / combos keep Space.
  if (qobject_cast<QLineEdit*>(focus) || qobject_cast<QTextEdit*>(focus) ||
      qobject_cast<QComboBox*>(focus) || qobject_cast<QAbstractSpinBox*>(focus)) {
    return false;
  }
  return true;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_Space && !keyEvent->isAutoRepeat() &&
        shouldStealSpaceFromFocus()) {
      if (event->type() == QEvent::KeyPress) {
        canvas_->beginHoldToWrite();
      } else {
        canvas_->endHoldToWrite();
      }
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onHoldToWriteChanged(bool active) {
  if (active) {
    statusBar()->showMessage(tr("Hold-to-write active — move on the canvas; release Space when done"));
  } else {
    statusBar()->showMessage(tr("Click-drag or hold Space to write"));
  }
  // Refresh the Input: label without wiping recognition stats.
  const QString writeState = active ? tr("hold-to-write") : tr("click-drag");
  QString text = diagnostics_->text();
  const QString marker = QStringLiteral(" · Mode:");
  const int modeAt = text.indexOf(marker);
  if (text.startsWith(QStringLiteral("Input:")) && modeAt > 0) {
    diagnostics_->setText(tr("Input: %1").arg(writeState) + text.mid(modeAt));
  }
}

void MainWindow::clearAll() {
  canvas_->clearStrokes();
  candidates_->clear();
  updateDiagnostics({});
}

void MainWindow::undoStroke() {
  canvas_->undoLastStroke();
}

void MainWindow::copySelected() {
  const QString ch = candidates_->selectedCharacter();
  if (!ch.isEmpty()) {
    QApplication::clipboard()->setText(ch);
    statusBar()->showMessage(tr("Copied %1").arg(ch), 2000);
  }
}

void MainWindow::updateDiagnostics(const RecognitionResult& result) {
  const QString modeName = modeCombo_->currentText();
  const QString writeState =
      canvas_->holdToWriteActive() ? tr("hold-to-write") : tr("click-drag");
  // Keep this short so diagnostics do not steal vertical space from the canvas.
  diagnostics_->setText(
      tr("Input: %1 · Mode: %2 · Strokes: %3 · Latency: %4 ms%5")
          .arg(writeState)
          .arg(modeName)
          .arg(result.strokeCount)
          .arg(result.latencyMs)
          .arg(result.error.isEmpty()
                   ? QString()
                   : QStringLiteral(" · Error: %1").arg(result.error)));
}

void MainWindow::onStrokesChanged() {
  runRecognition();
}

void MainWindow::runRecognition() {
  const QVector<Stroke> strokes = canvas_->strokes();
  if (strokes.isEmpty()) {
    candidates_->clear();
    updateDiagnostics({});
    return;
  }

  const int generation = ++recognitionGeneration_;
  const RecognitionMode mode = currentMode();
  const int w = canvas_->logicalWidth();
  const int h = canvas_->logicalHeight();
  ZinniaBackend* backend = backend_;

  auto* watcher = new QFutureWatcher<RecognitionResult>(this);
  connect(watcher, &QFutureWatcher<RecognitionResult>::finished, this, [this, watcher, generation]() {
    const RecognitionResult result = watcher->result();
    watcher->deleteLater();
    if (generation != recognitionGeneration_) {
      return;
    }
    candidates_->setResult(result);
    updateDiagnostics(result);
    copyButton_->setEnabled(!candidates_->selectedCharacter().isEmpty());
  });

  QFuture<RecognitionResult> future = QtConcurrent::run([backend, strokes, mode, w, h]() {
    return backend->recognize(strokes, mode, w, h, 10);
  });
  watcher->setFuture(future);
}

void MainWindow::onRecognitionFinished() {
  // Unused; kept for slot naming symmetry / future hooks.
}
