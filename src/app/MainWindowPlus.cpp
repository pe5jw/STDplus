// STDplus — MainWindowPlus implementation
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MainWindowPlus.h"
#include "CwModem.h"
#include "N1mmCat.h"
#include "VoiceKeyer.h"
#include "TciClient.h"

#include <QAction>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QWidgetAction>

// ---------------------------------------------------------------------------
// STDplus brand title widget — draws "STD+" in the same neon style as the
// upstream "STD" RaisedBrandTitle, but wider.
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

    // Glow
    p.setBrush(Qt::NoBrush);
    const int gw[] = {18, 12, 7};
    const int ga[] = {18, 32, 54};
    for (int i = 0; i < 3; ++i) {
      p.setPen(QPen(QColor(255, 146, 38, ga[i]), gw[i],
                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p.drawPath(letters);
    }

    // Shadow
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(22, 4, 16, 220));
    p.drawPath(letters.translated(2.6, 3.4));
    p.setBrush(QColor(96, 28, 78, 235));
    p.drawPath(letters.translated(1.3, 1.8));

    // Face gradient
    const QRectF b = letters.boundingRect();
    QLinearGradient face(b.topLeft(), b.bottomLeft());
    face.setColorAt(0.0,  QColor(232, 206, 246));
    face.setColorAt(0.38, QColor(154,  72, 196));
    face.setColorAt(0.72, QColor(108,  36, 158));
    face.setColorAt(1.0,  QColor( 62,  16,  96));
    p.setBrush(face);
    p.drawPath(letters);

    // Highlight
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

  // Override the title and name set by MainWindow's constructor.
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

  // Redirect the Help → About STD menu action to our tabbed dialog.
  for (QAction* act : menuBar()->actions()) {
    if (act->text() == QLatin1String("Help")) {
      for (QAction* sub : act->menu()->actions()) {
        if (sub->text().startsWith(QLatin1String("About"))) {
          // disconnect original slot and connect ours
          disconnect(sub, nullptr, nullptr, nullptr);
          connect(sub, &QAction::triggered,
                  this, &MainWindowPlus::showAboutPlus);
          sub->setText(QStringLiteral("About STDplus…"));
          break;
        }
      }
      break;
    }
  }

  // Find the TciClient that MainWindow created in its constructor.
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
  // CW mode: add "CW" to the mode menu and wire CwModem to TCI audio
  // -----------------------------------------------------------------------
  cwModem_ = new CwModem(this);
  if (tci_)
    cwModem_->setClient(tci_);

  // Decoded text → N1MM callsign broadcast
  connect(cwModem_, &CwModem::textDecoded,
          n1mmCat_, &N1mmCat::sendCallToN1mm);

  // Add CW entry to the mode menu (objectName set by MainWindow).
  auto* modeMenu = findChild<QMenu*>(QStringLiteral("modeMenu"));
  if (modeMenu) {
    modeMenu->addSeparator();
    auto* cwAction = new QAction(QStringLiteral("CW"), this);
    cwAction->setObjectName(QStringLiteral("modeCw"));
    cwAction->setCheckable(true);
    cwAction->setToolTip(QStringLiteral(
        "CW decoder (fldigi-derived Schmitt-trigger AGC).\n"
        "Decodes CW from TCI audio stream.\n"
        "Decoded callsigns are forwarded to N1MM Logger+ via UDP."));
    modeMenu->addAction(cwAction);

    connect(cwAction, &QAction::toggled, this, [this, cwAction](bool on) {
      if (!tci_) return;
      if (on) {
        // Uncheck other checkable mode actions
        if (auto* menu = qobject_cast<QMenu*>(cwAction->parent())) {
          for (QAction* a : menu->actions()) {
            if (a->isCheckable() && a != cwAction)
              a->setChecked(false);
          }
        }
        // rxAudio delivers raw float samples as QByteArray; convert and forward.
        connect(tci_, &TciClient::rxAudio, cwModem_,
                [this](int /*rate*/, const QByteArray& mono) {
                  const int n = mono.size() / int(sizeof(float));
                  const auto* p = reinterpret_cast<const float*>(mono.constData());
                  cwModem_->processAudio(QVector<float>(p, p + n));
                }, Qt::UniqueConnection);
      } else {
        disconnect(tci_, &TciClient::rxAudio, cwModem_, nullptr);
      }
    });
  }

  buildVoiceKeyerBar();
  buildN1mmCat();
  loadVoiceKeyerSettings();
}

// ---------------------------------------------------------------------------
// patchBrandBar — swap the upstream "STD" label widget for "STD+"
// ---------------------------------------------------------------------------
void MainWindowPlus::patchBrandBar()
{
  // brandName is RaisedBrandTitle inside "words" QWidget (QVBoxLayout).
  // Layout tree: brandBar → host(QHBoxLayout) → words(QVBoxLayout) → [brandName, brandSubtitle]
  auto* old = findChild<QWidget*>(QStringLiteral("brandName"));
  if (!old) return;

  // words is brandName's direct parent; its layout is a QVBoxLayout.
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
// showAboutPlus — tabbed About dialog: "STDplus" tab first, "STD" tab second
// ---------------------------------------------------------------------------
void MainWindowPlus::showAboutPlus()
{
  QDialog dlg(this);
  dlg.setWindowTitle(QStringLiteral("About STDplus"));
  dlg.setMinimumWidth(560);

  auto* tabs = new QTabWidget(&dlg);

  // --- Tab 1: STDplus ---
  {
    auto* w   = new QWidget;
    auto* vbl = new QVBoxLayout(w);
    vbl->setContentsMargins(12, 12, 12, 12);
    vbl->setSpacing(8);

    auto* tb = new QTextBrowser(w);
    tb->setOpenExternalLinks(false);  // we handle clicks ourselves
    tb->setReadOnly(true);
    tb->setTextInteractionFlags(Qt::TextBrowserInteraction);
    tb->setFrameShape(QFrame::NoFrame);
    tb->setHtml(QString::fromUtf8(R"HTML(
<p><b>STDplus</b> — Shift, Tone, Decode — <i>plus</i><br>
Version %1 · PE5JW</p>

<p>STDplus is an open-source addon layer on top of STD. It adds:</p>
<ul>
  <li><b>Voice Keyer (DVK)</b> — 9 WAV slots (F1–F9) sent directly via TCI audio.</li>
  <li><b>N1MM CAT bridge</b> — TCP server on port 4532, Kenwood CAT dialect.<br>
      N1MM Logger+ connects here for frequency, mode, and PTT.</li>
  <li><b>TCI dialect detection</b> — auto-detects ESDR / Expert / ExpertSDR2 variants.</li>
  <li><b>CW modem</b> — Goertzel RX decoder + Morse TX encoder (work in progress).</li>
</ul>

<p>Copyright © 2026 Joeri (PE5JW)<br>
Licensed under the <b>GNU General Public License, version 3</b> or any later version.</p>

<p>Source: <a href="https://github.com/pe5jw/STDplus">github.com/pe5jw/STDplus</a></p>

<p style='color:#e8c477'><b>If STDplus saves you time in a contest, buy me a coffee ☕</b></p>
)HTML").arg(QLatin1String(STDPLUS_VERSION)));
    vbl->addWidget(tb);

    auto* kofi = new QPushButton(QStringLiteral("☕  Support on Ko-Fi"), w);
    kofi->setToolTip(QStringLiteral("https://ko-fi.com/pe5jw"));
    connect(kofi, &QPushButton::clicked, [] {
      QDesktopServices::openUrl(QUrl(QStringLiteral("https://ko-fi.com/pe5jw")));
    });
    vbl->addWidget(kofi);
    tabs->addTab(w, QStringLiteral("STDplus"));
  }

  // --- Tab 2: STD (original) ---
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
<p><b>STD</b> — Shift, Tone, Decode<br>
Version %1 · N8SDR</p>

<p>Copyright © 2026 Rick Langford (N8SDR)<br>
Licensed under the <b>GNU General Public License, version 3</b> or any later version.</p>

<p>STD speaks TCI. Connect sends <b>AUDIO_START</b> for receiver 0.
FT8, FT4, FT2, Feld Hell, RTTY, the BPSK speeds, QPSK, Olivia, Contestia, MFSK,
DominoEX, Thor, JS8, and WSPR transmit when TX is pressed.
WEFAX-576 receives only. CW stays in Lyra.</p>

<p style='color:#e8c477'><b>STD: Help keep the infections spreading and buy us a shot of bourbon.</b></p>

<p><b>Sources and references.</b></p>
<ul>
<li><b>MSHV</b> (Christo, LZ2HV) — FT8, FT4, and FT2 decoder.</li>
<li><b>WSJT-X</b> (Joe Taylor, K1JT, GPL-3) and <b>JTDX-improved</b> — WSPR-2, WSPRnet, PSK Reporter.</li>
<li><b>Fldigi</b> (Dave Freese, W1HKJ, GPL-3) — Feld Hell, RTTY, BPSK, Olivia, Contestia, MFSK, DominoEX, Thor, WEFAX-576.</li>
<li><b>TCIdigi</b> — TCI audio for a Fldigi-style keyboard.</li>
<li><b>Lyra</b> (N8SDR, GPL-3) — the TCI client this socket follows. CW stays there.</li>
</ul>
<p>Source: <a href="https://github.com/n8sdr/std">github.com/n8sdr/std</a></p>
)HTML").arg(QLatin1String(STD_VERSION)));
    vbl->addWidget(tb);

    const QString payPal = QStringLiteral(
        "https://www.paypal.com/donate/?business=NP2ZQS4LR454L"
        "&no_recurring=0&item_name=STD%3A+Help+keep+the+infections+"
        "spreading+and+buy+us+a+shot+of+bourbon.&currency_code=USD");
    auto* donateBtn = new QPushButton(QStringLiteral("☕  Donate via PayPal (N8SDR)"), w);
    connect(donateBtn, &QPushButton::clicked, [payPal] {
      QDesktopServices::openUrl(QUrl(payPal));
    });
    vbl->addWidget(donateBtn);
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
// Voice Keyer toolbar  (F1 – F9)
// ---------------------------------------------------------------------------
void MainWindowPlus::buildVoiceKeyerBar()
{
  auto* bar = addToolBar(tr("Voice Keyer"));
  bar->setObjectName(QStringLiteral("vkBar"));
  bar->setMovable(false);

  auto* stopBtn = new QToolButton(bar);
  stopBtn->setText(tr("■ Stop"));
  stopBtn->setToolTip(tr("Stop voice keyer playback"));
  connect(stopBtn, &QToolButton::clicked, voiceKeyer_, &VoiceKeyer::stop);
  bar->addWidget(stopBtn);
  bar->addSeparator();

  for (int i = 0; i < VoiceKeyer::kSlots; ++i) {
    const QString label = QStringLiteral("F%1").arg(i + 1);
    auto* btn = new QToolButton(bar);
    btn->setText(label);
    btn->setObjectName(QStringLiteral("vkBtn%1").arg(i));
    btn->setToolTip(tr("Play slot %1 (right-click to load WAV)").arg(label));
    btn->setContextMenuPolicy(Qt::CustomContextMenu);

    const int slot = i;
    connect(btn, &QToolButton::clicked, this, [this, slot] {
      if (voiceKeyer_->isPlaying() && voiceKeyer_->currentSlot() == slot)
        voiceKeyer_->stop();
      else
        voiceKeyer_->play(slot);
    });

    connect(btn, &QToolButton::customContextMenuRequested, this,
            [this, btn, slot](const QPoint& pos) {
      QMenu menu;
      menu.addAction(tr("Load WAV for %1…").arg(btn->text()), this,
                     [this, slot, btn] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Load WAV for %1").arg(btn->text()),
            QSettings().value(QStringLiteral("vk/lastDir")).toString(),
            tr("WAV files (*.wav);;All files (*)"));
        if (path.isEmpty()) return;
        QSettings s;
        s.setValue(QStringLiteral("vk/lastDir"),
                   QFileInfo(path).absolutePath());
        s.setValue(QStringLiteral("vk/slot%1").arg(slot), path);
        if (voiceKeyer_->loadSlot(slot, path))
          btn->setToolTip(QFileInfo(path).fileName());
        else
          QMessageBox::warning(this, tr("Voice Keyer"),
                               tr("Could not load %1").arg(path));
      });
      if (!voiceKeyer_->isPlaying() || voiceKeyer_->currentSlot() != slot) {
        menu.addAction(tr("Clear slot"), this, [this, slot, btn] {
          voiceKeyer_->clearSlot(slot);
          QSettings().remove(QStringLiteral("vk/slot%1").arg(slot));
          btn->setToolTip(tr("Play slot F%1 (right-click to load WAV)")
                          .arg(slot + 1));
        });
      }
      menu.exec(btn->mapToGlobal(pos));
    });

    bar->addWidget(btn);
  }

  connect(voiceKeyer_, &VoiceKeyer::playbackStarted, this,
          [this, bar](int slot) {
    for (int i = 0; i < VoiceKeyer::kSlots; ++i) {
      auto* b = bar->findChild<QToolButton*>(
          QStringLiteral("vkBtn%1").arg(i));
      if (b) b->setEnabled(i == slot);
    }
  });
  connect(voiceKeyer_, &VoiceKeyer::playbackFinished, this,
          [this, bar](int) {
    for (int i = 0; i < VoiceKeyer::kSlots; ++i) {
      auto* b = bar->findChild<QToolButton*>(
          QStringLiteral("vkBtn%1").arg(i));
      if (b) b->setEnabled(true);
    }
  });
}

// ---------------------------------------------------------------------------
// N1MM CAT bridge
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
    statusBar()->showMessage(
        tr("N1MM CAT: connected from %1").arg(addr), 3000);
  });
  connect(n1mmCat_, &N1mmCat::clientDisconnected, this, [this] {
    statusBar()->showMessage(tr("N1MM CAT: disconnected"), 2000);
  });
  connect(n1mmCat_, &N1mmCat::error, this, [this](const QString& msg) {
    statusBar()->showMessage(tr("N1MM CAT: %1").arg(msg), 4000);
  });
}

// ---------------------------------------------------------------------------
// Persist WAV slot assignments across sessions
// ---------------------------------------------------------------------------
void MainWindowPlus::loadVoiceKeyerSettings()
{
  QSettings s;
  for (int i = 0; i < VoiceKeyer::kSlots; ++i) {
    const QString key  = QStringLiteral("vk/slot%1").arg(i);
    const QString path = s.value(key).toString();
    if (path.isEmpty() || !QFileInfo::exists(path))
      continue;
    auto* bar = findChild<QToolBar*>(QStringLiteral("vkBar"));
    auto* btn = bar
        ? bar->findChild<QToolButton*>(QStringLiteral("vkBtn%1").arg(i))
        : nullptr;
    if (voiceKeyer_->loadSlot(i, path) && btn)
      btn->setToolTip(QFileInfo(path).fileName());
  }
}
