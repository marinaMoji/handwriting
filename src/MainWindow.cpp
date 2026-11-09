#include "MainWindow.h"

#include "CandidatePanel.h"
#include "DrawingCanvas.h"
#include "HoldKey.h"
#include "Recognizer.h"
#include "SampleIO.h"
#include "StrokePreprocess.h"
#include "StrokeMapping.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
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

#ifndef HANDWRITING_DEFAULT_SAMPLES_DIR
#define HANDWRITING_DEFAULT_SAMPLES_DIR "samples"
#endif

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

  expectedEdit_ = new QLineEdit(controlBar);
  expectedEdit_->setPlaceholderText(tr("Intended character"));
  expectedEdit_->setMaxLength(2);  // one character, possibly a surrogate pair
  expectedEdit_->setFixedWidth(130);
  saveSampleButton_ = new QPushButton(tr("Save sample"), controlBar);
  saveSampleButton_->setToolTip(tr("Save the raw strokes with the intended character for offline evaluation"));
  saveSampleButton_->setEnabled(false);

  auto* clearBtn = new QPushButton(tr("Clear"), controlBar);
  auto* undoBtn = new QPushButton(tr("Undo"), controlBar);
  copyButton_ = new QPushButton(tr("Copy"), controlBar);
  copyButton_->setEnabled(false);

  controls->addWidget(new QLabel(tr("Mode:"), controlBar));
  controls->addWidget(modeCombo_, 1);
  controls->addWidget(undoBtn);
  controls->addWidget(clearBtn);
  controls->addWidget(copyButton_);
  controls->addWidget(expectedEdit_);
  controls->addWidget(saveSampleButton_);
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
  connect(canvas_, &DrawingCanvas::trackpadProblem, this,
          [this](const QString& message) { statusBar()->showMessage(message, 10000); });
  connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::onStrokesChanged);
  connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearAll);
  connect(saveSampleButton_, &QPushButton::clicked, this, &MainWindow::saveSample);
  connect(expectedEdit_, &QLineEdit::returnPressed, this, &MainWindow::saveSample);
  connect(expectedEdit_, &QLineEdit::textChanged, this, &MainWindow::updateSaveEnabled);
  connect(undoBtn, &QPushButton::clicked, this, &MainWindow::undoStroke);
  connect(copyButton_, &QPushButton::clicked, this, &MainWindow::copySelected);
  connect(candidates_, &CandidatePanel::selectionChanged, this, [this](const QString& ch) {
    copyButton_->setEnabled(!ch.isEmpty());
  });
  connect(qApp, &QGuiApplication::applicationStateChanged, this,
          &MainWindow::onApplicationStateChanged);

  // App-wide filter for the hold key (see HoldKey.h); ignored while typing in text widgets.
  qApp->installEventFilter(this);
  canvas_->setFocus(Qt::OtherFocusReason);

  updateDiagnostics({});
  statusBar()->showMessage(
      tr("Click-drag or hold ` to write · JA: %1 · TW: %2")
          .arg(QFileInfo(backend_->japaneseModelPath()).fileName())
          .arg(QFileInfo(backend_->traditionalModelPath()).fileName()));
  if (!canvas_->backendNote().isEmpty()) {
    statusBar()->showMessage(canvas_->backendNote(), 15000);
  }
}

namespace {
// Default measured with handwriting_eval on synthetic jittery ink (docs/findings.md).
// Override with HANDWRITING_PREPROCESS (e.g. "none", "smooth:2", "resample:4+smooth:3").
const Recognizer::Options& recognizerOptions() {
  static const Recognizer::Options opts = []() {
    QString spec = qEnvironmentVariable("HANDWRITING_PREPROCESS",
                                        QString::fromLatin1(Recognizer::kDefaultPreprocessSpec));
    Recognizer::Options o;
    QString error;
    if (!Recognizer::optionsFromSpec(spec, &o, &error)) {
      qWarning("HANDWRITING_PREPROCESS: %s; using the default", qPrintable(error));
      o = Recognizer::defaultOptions();
      spec = QString::fromLatin1(Recognizer::kDefaultPreprocessSpec);
    }
    qInfo("stroke preprocessing: %s", qPrintable(spec));
    return o;
  }();
  return opts;
}
}  // namespace

RecognitionMode MainWindow::currentMode() const {
  return static_cast<RecognitionMode>(modeCombo_->currentData().toInt());
}

bool MainWindow::shouldHandleHoldKeyFromFocus() const {
  // Always handle the key while already holding, so release is not lost.
  if (canvas_->holdToWriteActive()) {
    return true;
  }
  QWidget* focus = QApplication::focusWidget();
  if (focus == nullptr) {
    return true;
  }
  // Only text-entry widgets keep the key (it types ` / ² there).
  return !(qobject_cast<QLineEdit*>(focus) || qobject_cast<QTextEdit*>(focus) ||
           qobject_cast<QAbstractSpinBox*>(focus) ||
           (qobject_cast<QComboBox*>(focus) &&
            qobject_cast<QComboBox*>(focus)->isEditable()));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  Q_UNUSED(watched);
  // While hold-to-write is active, pad presses/taps must not click anything.
  if (canvas_->holdToWriteActive()) {
    switch (event->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
      case QEvent::ContextMenu:
        return true;
      default:
        break;
    }
  }
  if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
    auto* keyEvent = static_cast<QKeyEvent*>(event);
    static const bool debugKeys = qEnvironmentVariableIsSet("HANDWRITING_DEBUG_KEYS");
    if (debugKeys) {
      qInfo("key %s qtKey=0x%x nativeVK=0x%x scan=0x%x text='%s' hold=%d",
            event->type() == QEvent::KeyPress ? "press" : "release", keyEvent->key(),
            keyEvent->nativeVirtualKey(), keyEvent->nativeScanCode(),
            qPrintable(keyEvent->text()), hold_key::isHoldToWriteKey(keyEvent));
    }
    if (hold_key::isHoldToWriteKey(keyEvent) && !keyEvent->isAutoRepeat() &&
        shouldHandleHoldKeyFromFocus()) {
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

void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::WindowDeactivate) {
    canvas_->endHoldToWrite();
  }
  QMainWindow::changeEvent(event);
}

void MainWindow::onApplicationStateChanged(Qt::ApplicationState state) {
  if (state != Qt::ApplicationActive) {
    canvas_->endHoldToWrite();
  }
}

void MainWindow::onHoldToWriteChanged(bool active) {
  if (active) {
    statusBar()->showMessage(
        tr("Hold-to-write active — write on the trackpad; release ` when done"));
  } else {
    statusBar()->showMessage(tr("Click-drag or hold ` to write"));
  }
  updateDiagnostics(lastResult_);
}

void MainWindow::clearAll() {
  canvas_->clearStrokes();
  candidates_->clear();
  lastResult_ = {};
  updateDiagnostics(lastResult_);
}

void MainWindow::updateSaveEnabled() {
  saveSampleButton_->setEnabled(!canvas_->strokes().isEmpty() &&
                                !expectedEdit_->text().trimmed().isEmpty());
}

void MainWindow::saveSample() {
  const QString expected = expectedEdit_->text().trimmed();
  if (expected.isEmpty() || canvas_->strokes().isEmpty()) {
    statusBar()->showMessage(tr("Type the intended character and draw something first."), 4000);
    return;
  }
  Sample sample;
  sample.character = expected;
  sample.strokes = canvas_->strokes();
  sample.surfaceWidth = canvas_->surfaceWidth();
  sample.surfaceHeight = canvas_->surfaceHeight();
  sample.source = canvas_->inputSourceLabel();

  QString dir = qEnvironmentVariable("HANDWRITING_SAMPLES_DIR");
  if (dir.isEmpty()) {
    dir = QString::fromUtf8(HANDWRITING_DEFAULT_SAMPLES_DIR);
  }
  QString path;
  QString error;
  if (!sample_io::saveSample(dir, sample, &path, &error)) {
    statusBar()->showMessage(tr("Could not save sample: %1").arg(error), 8000);
    return;
  }
  statusBar()->showMessage(tr("Saved %1").arg(path), 5000);
  clearAll();
  expectedEdit_->selectAll();
  canvas_->setFocus(Qt::OtherFocusReason);
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
  lastResult_ = result;
  const QString modeName = modeCombo_->currentText();
  const QString writeState =
      canvas_->holdToWriteActive() ? tr("hold-to-write") : tr("click-drag");
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
  updateSaveEnabled();
  runRecognition();
}

void MainWindow::runRecognition() {
  const QVector<Stroke> rawStrokes = canvas_->strokes();
  if (rawStrokes.isEmpty()) {
    candidates_->clear();
    lastResult_ = {};
    updateDiagnostics(lastResult_);
    return;
  }

  const int generation = ++recognitionGeneration_;
  const RecognitionMode mode = currentMode();
  const int w = canvas_->logicalWidth();
  const int h = canvas_->logicalHeight();
  // Crop/fit/preprocess happen inside the shared Recognizer pipeline.
  const Recognizer recognizer(backend_);
  const Recognizer::Options options = recognizerOptions();

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

  QFuture<RecognitionResult> future = QtConcurrent::run([recognizer, rawStrokes, mode, w, h, options]() {
    return recognizer.recognize(rawStrokes, w, h, mode, options);
  });
  watcher->setFuture(future);
}
