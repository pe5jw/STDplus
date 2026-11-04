// STDplus — MainWindowPlus implementation
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainWindowPlus.h"
#include "CwModem.h"
#include "N1mmCat.h"
#include "VoiceKeyer.h"
#include "../voicekey/DvkModeWidget.h"
#include "TciClient.h"

#include <QAction>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

// ---------------------------------------------------------------------------
// STDplus brand title widget
// ---------------------------------------------------------------------------
class RaisedBrandTitlePlus : public QWidget {
public:
  explicit RaisedBrandTitlePlus(QWidget* parent = nullptr) : QWidget(parent) {
    setObjectName("brandNamePlus");
    setAccessibleName("STD+");
    QFont face("Inter");
    face.setPixelSize(32);
    face.setWeight(QFont::Black);
    face.setLetterSpacing(QFont::AbsoluteSpacing, 1.6);
    setFont(face);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
  }

  QSize sizeHint() const override {
    const QFontMetrics m(font());
    return {m.horizontalAdvance(QStringLiteral("STD+")) + 28, m.height() + 24};
  }
  QSize minimumSizeHint() const override { return sizeHint(); }

protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const QFontMetrics m(font());
    const qreal padX = 12.0, padY = 12.0;
    QPainterPath letters;
    letters.addText(padX, padY + m.ascent(), font(), QStringLiteral("STD+"));

    p.setBrush(Qt::NoBrush);
    const int gw[] = {18, 12, 7};
    const int ga[] = {18, 32, 54};
    for (int i = 0; i < 3; ++i) {
      p.setPen(QPen(QColor(255, 146, 38, ga[i]), gw[i],
                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p.drawPath(letters);
    }

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(22, 4, 16, 220));
    p.drawPath(letters.translated(2.6, 3.4));
    p.setBrush(QColor(96, 28, 78, 235));
    p.drawPath(letters.translated(1.3, 1.8));

    const QRectF b = letters.boundingRect();
    QLinearGradient face(b.topLeft(), b.bottomLeft());
    face.setColorAt(0.0,  QColor(232, 206, 246));
    face.setColorAt(0.38, QColor(154,  72, 196));
    face.setColorAt(0.72, QColor(108,  36, 158));
    face.setColorAt(1.0,  QColor( 62,  16,  96));
    p.setBrush(face);
    p.drawPath(letters);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(244, 226, 255, 190), 1.2,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(letters.translated(-0.7, -0.85));
    p.setPen(QPen(QColor(255, 176, 62, 220), 1.15,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(letters.translated(0.65, 0.8));
  }
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
MainWindowPlus::MainWindowPlus(QWidget* parent)
  : MainWindow(parent)
{
  voiceKeyer_ = new VoiceKeyer(this);
  n1mmCat_    = new N1mmCat(this);
  setWindowTitle(QStringLiteral("STDplus"));
}

// ---------------------------------------------------------------------------
// showEvent — runs once after the window is fully constructed by MainWindow
// ---------------------------------------------------------------------------
void MainWindowPlus::showEvent(QShowEvent* event)
{
  MainWindow::showEvent(event);

  if (featuresWired_)
    return;
  featuresWired_ = true;

  patchBrandBar();

  for (QAction* act : menuBar()->actions()) {
    if (act->text() == QLatin1String("Help")) {
      for (QAction* sub : act->menu()->actions()) {
        if (sub->text().startsWith(QLatin1String("About"))) {
          disconnect(sub, nullptr, nullptr, nullptr);
          connect(sub, &QAction::triggered,
                  this, &MainWindowPlus::showAboutPlus);
          sub->setText(QStringLiteral("About STDplus..."));
          break;
        }
      }
      break;
    }
  }

  tci_ = findChild<TciClient*>();

  if (tci_) {
    voiceKeyer_->setClient(tci_);

    connect(tci_, &TciClient::dialChanged,  n1mmCat_, &N1mmCat::setFreqHz);
    connect(tci_, &TciClient::modeChanged,  n1mmCat_, &N1mmCat::setMode);
    connect(tci_, &TciClient::pttChanged,   n1mmCat_, &N1mmCat::setPtt);

    connect(n1mmCat_, &N1mmCat::freqChangeRequested,
            tci_, &TciClient::setDialHz);
    connect(n1mmCat_, &N1mmCat::pttChangeRequested,
            tci_, [this](bool on) { tci_->setTransmit(on); });
  }

  // -----------------------------------------------------------------------
  // CW mode
  // -----------------------------------------------------------------------
  cwModem_ = new CwModem(this);
  if (tci_)
    cwModem_->setClient(tci_);

  connect(cwModem_, &CwModem::textDecoded,
          n1mmCat_, &N1mmCat::sendCallToN1mm);

  auto* modeMenu = findChild<QMenu*>(QStringLiteral("modeMenu"));
  if (modeMenu) {
    modeMenu->addSeparator();
    auto* cwAction = new QAction(QStringLiteral("CW"), this);
    cwAction->setObjectName(QStringLiteral("modeCw"));
    cwAction->setCheckable(true);
    cwAction->setToolTip(QStringLiteral(
        "CW decoder.\nDecodes CW from TCI audio stream."));
    modeMenu->addAction(cwAction);

    QTextEdit* wEcho_ = findChild<QTextEdit*>(QStringLiteral("rxEcho"));

    connect(cwAction, &QAction::toggled, this,
            [this, cwAction, wEcho_](bool on) {
      if (on) {
        // Show the keyboard-receive widget directly — do NOT trigger the RTTY
        // action because activateDigitalMode("RTTY") sets mshvFamily=false,
        // which breaks the FT8 armed condition and stops decoding.
        showKeyboardReceive(QStringLiteral("RTTY"));
        // Re-check cwAction (showKeyboardReceive does not uncheck it, but
        // keep the blocker for safety).
        { const QSignalBlocker sb(cwAction); cwAction->setChecked(true); }
        if (wEcho_)
          wEcho_->setPlaceholderText(
              QStringLiteral("CW decoded text appears here."));
        if (tci_)
          connect(tci_, &TciClient::rxAudio, cwModem_,
                  [this](int /*rate*/, const QByteArray& mono) {
                    const int n = mono.size() / int(sizeof(float));
                    const float* p =
                        reinterpret_cast<const float*>(mono.constData());
                    cwModem_->processAudio(QVector<float>(p, p + n));
                  }, Qt::UniqueConnection);
      } else {
        if (tci_)
          disconnect(tci_, &TciClient::rxAudio, cwModem_, nullptr);
      }
    });
  }

  // -----------------------------------------------------------------------
  // DVK mode
  // -----------------------------------------------------------------------
  dvkWidget_ = new DvkModeWidget(voiceKeyer_, this);
  dvkWidget_->setObjectName(QStringLiteral("dvkModeWidget"));

  dvkDock_ = new QDockWidget(tr("DVK -- Voice Keyer"), this);
  dvkDock_->setObjectName(QStringLiteral("dvkDock"));
  dvkDock_->setWidget(dvkWidget_);
  dvkDock_->setAllowedAreas(Qt::AllDockWidgetAreas);
  dvkDock_->setVisible(false);
  addDockWidget(Qt::BottomDockWidgetArea, dvkDock_);

  if (modeMenu) {
    QAction* dvkAction = new QAction(QStringLiteral("DVK"), this);
    dvkAction->setObjectName(QStringLiteral("modeDvk"));
    dvkAction->setCheckable(true);
    dvkAction->setToolTip(QStringLiteral(
        "Voice Keyer (DVK) -- 9 WAV slots (F1-F9).\n"
        "Triggered via N1MM Kenwood TS-2000 PB0-PB8 CAT commands."));
    modeMenu->addAction(dvkAction);

    QDockWidget* txDock_ = findChild<QDockWidget*>(QStringLiteral("transmitDock"));

    connect(dvkAction, &QAction::toggled, this,
            [this, dvkAction, txDock_](bool on) {
      if (on) {
        // Show the keyboard-receive widget directly — same reason as CW:
        // triggering the RTTY action sets mshvFamily=false and breaks FT8.
        showKeyboardReceive(QStringLiteral("RTTY"));
        // Re-check DVK action.
        { const QSignalBlocker sb(dvkAction); dvkAction->setChecked(true); }
        if (txDock_) txDock_->setVisible(false);
        if (dvkDock_) dvkDock_->setVisible(true);
      } else {
        if (dvkDock_) dvkDock_->setVisible(false);
        if (txDock_)  txDock_->setVisible(true);
      }
    });
  }

  connect(n1mmCat_, &N1mmCat::macroRequested, dvkWidget_,
          &DvkModeWidget::triggerSlot);
  connect(voiceKeyer_, &VoiceKeyer::playbackStarted,
          dvkWidget_, &DvkModeWidget::onPlaybackStarted);
  connect(voiceKeyer_, &VoiceKeyer::playbackFinished,
          dvkWidget_, &DvkModeWidget::onPlaybackFinished);

  dvkWidget_->loadSettings(QStringLiteral("dvk"));

  buildN1mmCat();
}

// ---------------------------------------------------------------------------
// patchBrandBar
// ---------------------------------------------------------------------------
void MainWindowPlus::patchBrandBar()
{
  auto* old = findChild<QWidget*>(QStringLiteral("brandName"));
  if (!old) return;
  auto* words = old->parentWidget();
  if (!words) return;
  auto* vbl = qobject_cast<QBoxLayout*>(words->layout());
  if (!vbl) return;
  const int idx = vbl->indexOf(old);
  old->hide();
  auto* plus = new RaisedBrandTitlePlus(words);
  if (idx >= 0)
    vbl->insertWidget(idx, plus);
  else
    vbl->addWidget(plus);
}

// ---------------------------------------------------------------------------
// showAboutPlus
// ---------------------------------------------------------------------------
void MainWindowPlus::showAboutPlus()
{
  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("About STDplus"));
  dlg.setMinimumWidth(560);

  auto* tabs = new QTabWidget(&dlg);

  {
    auto* w   = new QWidget;
    auto* vbl = new QVBoxLayout(w);
    vbl->setContentsMargins(12, 12, 12, 12);
    vbl->setSpacing(8);
    auto* tb = new QTextBrowser(w);
    tb->setOpenExternalLinks(false);
    tb->setReadOnly(true);
    tb->setTextInteractionFlags(Qt::TextBrowserInteraction);
    tb->setFrameShape(QFrame::NoFrame);
    tb->setHtml(QString::fromUtf8(R"HTML(
<p><b>STDplus</b> - Shift, Tone, Decode - <i>plus</i><br>Version %1 - PE5JW</p>
<p>STDplus is an open-source addon layer on top of STD. It adds:</p>
<ul>
  <li><b>Voice Keyer (DVK)</b> - 9 WAV slots (F1-F9) sent directly via TCI audio.</li>
  <li><b>N1MM CAT bridge</b> - TCP server on port 4532, Kenwood CAT dialect.</li>
  <li><b>CW modem</b> - Goertzel RX decoder.</li>
</ul>
<p>Copyright 2026 Joeri (PE5JW)<br>Licensed under the GNU General Public License, version 3.</p>
<p>Source: <a href="https://github.com/pe5jw/STDplus">github.com/pe5jw/STDplus</a></p>
)HTML").arg(QLatin1String(STDPLUS_VERSION)));
    vbl->addWidget(tb);
    auto* kofi = new QPushButton(QStringLiteral("Support on Ko-Fi"), w);
    connect(kofi, &QPushButton::clicked, [] {
      QDesktopServices::openUrl(QUrl(QStringLiteral("https://ko-fi.com/pe5jw")));
    });
    vbl->addWidget(kofi);
    tabs->addTab(w, QStringLiteral("STDplus"));
  }

  {
    auto* w   = new QWidget;
    auto* vbl = new QVBoxLayout(w);
    vbl->setContentsMargins(12, 12, 12, 12);
    vbl->setSpacing(8);
    auto* tb = new QTextBrowser(w);
    tb->setOpenExternalLinks(false);
    tb->setReadOnly(true);
    tb->setTextInteractionFlags(Qt::TextBrowserInteraction);
    tb->setFrameShape(QFrame::NoFrame);
    tb->setHtml(QString::fromUtf8(R"HTML(
<p><b>STD</b> - Shift, Tone, Decode<br>Version %1 - N8SDR</p>
<p>Copyright 2026 Rick Langford (N8SDR). GPL-3.</p>
<p>Source: <a href="https://github.com/n8sdr/std">github.com/n8sdr/std</a></p>
)HTML").arg(QLatin1String(STD_VERSION)));
    vbl->addWidget(tb);
    tabs->addTab(w, QStringLiteral("STD (upstream)"));
  }

  auto* bbox = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
  connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
  auto* layout = new QVBoxLayout(&dlg);
  layout->addWidget(tabs);
  layout->addWidget(bbox);
  dlg.exec();
}

// ---------------------------------------------------------------------------
// buildN1mmCat
// ---------------------------------------------------------------------------
void MainWindowPlus::buildN1mmCat()
{
  QSettings settings;
  const quint16 port = static_cast<quint16>(
      settings.value(QStringLiteral("n1mm/port"),
                     N1mmCat::kDefaultPort).toInt());
  if (!n1mmCat_->listen(port)) {
    statusBar()->showMessage(
        tr("N1MM CAT: could not bind port %1").arg(port), 5000);
    return;
  }
  connect(n1mmCat_, &N1mmCat::clientConnected, this,
          [this](const QString& addr) {
    statusBar()->showMessage(tr("N1MM CAT: connected from %1").arg(addr), 3000);
  });
  connect(n1mmCat_, &N1mmCat::clientDisconnected, this, [this] {
    statusBar()->showMessage(tr("N1MM CAT: disconnected"), 2000);
  });
  connect(n1mmCat_, &N1mmCat::error, this, [this](const QString& msg) {
    statusBar()->showMessage(tr("N1MM CAT: %1").arg(msg), 4000);
  });
}

// ---------------------------------------------------------------------------
// Unused stubs (declared in header for compat)
// ---------------------------------------------------------------------------
void MainWindowPlus::buildVoiceKeyerBar() {}
void MainWindowPlus::loadVoiceKeyerSettings() {}
void MainWindowPlus::saveVoiceKeyerSettings() {}
void MainWindowPlus::onVkPlaybackFinished(int /*slot*/) {}
