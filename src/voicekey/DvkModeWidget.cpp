// STDplus — DVK Mode Widget implementation
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DvkModeWidget.h"
#include "VoiceKeyer.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>
#include <QDataStream>
#include <QDir>
#include <QStandardPaths>

// Qt Multimedia for microphone recording
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QMediaDevices>

#include <cmath>
#include <algorithm>

// ===========================================================================
// DSP helpers
// ===========================================================================
namespace dsp {

struct Biquad {
  double b0=1,b1=0,b2=0,a1=0,a2=0;
  double x1=0,x2=0,y1=0,y2=0;
  float process(float x) {
    double y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
    x2=x1; x1=x; y2=y1; y1=y;
    return float(y);
  }
};

static Biquad makeHpf(float fc, float sr) {
  const double w0=2.0*M_PI*fc/sr, c=std::cos(w0), s=std::sin(w0);
  const double alpha=s/(2.0*0.7071), a0=1.0+alpha;
  Biquad bq;
  bq.b0= (1+c)/2/a0; bq.b1=-(1+c)/a0; bq.b2=(1+c)/2/a0;
  bq.a1=-2*c/a0;     bq.a2=(1-alpha)/a0;
  return bq;
}
static Biquad makeLowShelf(float gainDb, float fc, float sr) {
  const double A=std::pow(10.0,gainDb/40.0), w0=2.0*M_PI*fc/sr;
  const double c=std::cos(w0), s=std::sin(w0);
  const double alpha=s/2.0*std::sqrt((A+1/A)*(1/1.0-1)+2);
  const double a0=(A+1)+(A-1)*c+2*std::sqrt(A)*alpha;
  Biquad bq;
  bq.b0=    A*((A+1)-(A-1)*c+2*std::sqrt(A)*alpha)/a0;
  bq.b1=2.0*A*((A-1)-(A+1)*c)/a0;
  bq.b2=    A*((A+1)-(A-1)*c-2*std::sqrt(A)*alpha)/a0;
  bq.a1=-2.0  *((A-1)+(A+1)*c)/a0;
  bq.a2=       ((A+1)+(A-1)*c-2*std::sqrt(A)*alpha)/a0;
  return bq;
}
static Biquad makeHighShelf(float gainDb, float fc, float sr) {
  const double A=std::pow(10.0,gainDb/40.0), w0=2.0*M_PI*fc/sr;
  const double c=std::cos(w0), s=std::sin(w0);
  const double alpha=s/2.0*std::sqrt((A+1/A)*(1/1.0-1)+2);
  const double a0=(A+1)-(A-1)*c+2*std::sqrt(A)*alpha;
  Biquad bq;
  bq.b0=    A*((A+1)+(A-1)*c+2*std::sqrt(A)*alpha)/a0;
  bq.b1=-2.0*A*((A-1)+(A+1)*c)/a0;
  bq.b2=    A*((A+1)+(A-1)*c-2*std::sqrt(A)*alpha)/a0;
  bq.a1= 2.0  *((A-1)-(A+1)*c)/a0;
  bq.a2=       ((A+1)-(A-1)*c-2*std::sqrt(A)*alpha)/a0;
  return bq;
}
static Biquad makePeak(float gainDb, float fc, float sr) {
  const double A=std::pow(10.0,gainDb/40.0), w0=2.0*M_PI*fc/sr;
  const double alpha=std::sin(w0)/(2.0*1.0), a0=1.0+alpha/A;
  Biquad bq;
  bq.b0=(1.0+alpha*A)/a0; bq.b1=-2*std::cos(w0)/a0; bq.b2=(1.0-alpha*A)/a0;
  bq.a1=-2*std::cos(w0)/a0; bq.a2=(1.0-alpha/A)/a0;
  return bq;
}
static void compress(QVector<float>& pcm, float threshDb, float ratio) {
  if (ratio<=1.f) return;
  const float thr=float(std::pow(10.0,threshDb/20.0)), inv=1.f/ratio;
  for (float& s : pcm) {
    const float lv=std::abs(s);
    if (lv>thr && lv>0.f) s *= thr*std::pow(lv/thr,inv)/lv;
  }
}
static void applyGain(QVector<float>& pcm, float gainDb) {
  if (std::abs(gainDb)<0.01f) return;
  const float g=float(std::pow(10.0,gainDb/20.0));
  for (float& s : pcm) s*=g;
}
static void normalize(QVector<float>& pcm) {
  float pk=0.f;
  for (float s : pcm) pk=std::max(pk,std::abs(s));
  if (pk<1e-6f) return;
  const float g=float(std::pow(10.0,-1.0/20.0))/pk;
  for (float& s : pcm) s*=g;
}

} // namespace dsp

// ===========================================================================
// WAV helpers
// ===========================================================================
namespace wav {

struct Info { quint32 sampleRate=0; quint16 channels=0; quint16 bits=0; quint16 fmt=0; };

static bool read(const QString& path, QVector<float>& out, Info& info) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return false;
  QDataStream ds(&f);
  ds.setByteOrder(QDataStream::LittleEndian);
  quint32 riff, sz, wave; ds >> riff >> sz >> wave;
  if (riff!=0x46464952u || wave!=0x45564157u) return false;
  bool gotFmt=false, gotData=false;
  while (!ds.atEnd() && !gotData) {
    quint32 id, csz; ds >> id >> csz;
    if (id==0x20746d66u) {
      quint16 fmt,ch; quint32 sr,br; quint16 ba,bps;
      ds >> fmt >> ch >> sr >> br >> ba >> bps;
      if (csz>16) ds.skipRawData(int(csz)-16);
      if (fmt!=1 && fmt!=3) return false;
      info={sr,ch,bps,fmt}; gotFmt=true;
    } else if (id==0x61746164u) {
      if (!gotFmt) return false;
      const int n=int(csz)/(info.bits/8)/info.channels;
      out.resize(n);
      for (int i=0;i<n;++i) {
        double sum=0;
        for (int c=0;c<info.channels;++c) {
          if (info.fmt==3) {
            float s; ds.readRawData(reinterpret_cast<char*>(&s),4); sum+=s;
          } else if (info.bits==16) {
            qint16 s; ds>>s; sum+=s/32768.0;
          } else if (info.bits==32) {
            qint32 s; ds>>s; sum+=s/2147483648.0;
          } else {
            qint16 s; ds>>s; sum+=s/32768.0;
          }
        }
        out[i]=float(sum/info.channels);
      }
      gotData=true;
    } else {
      ds.skipRawData(int(csz));
    }
  }
  return gotFmt && gotData;
}

static bool write16(const QString& path, const QVector<float>& pcm, quint32 sr) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly|QIODevice::Truncate)) return false;
  QDataStream ds(&f); ds.setByteOrder(QDataStream::LittleEndian);
  const quint32 dSz=quint32(pcm.size())*2;
  ds << quint32(0x46464952u) << quint32(36+dSz) << quint32(0x45564157u);
  ds << quint32(0x20746d66u) << quint32(16);
  ds << quint16(1) << quint16(1) << sr << quint32(sr*2) << quint16(2) << quint16(16);
  ds << quint32(0x61746164u) << dSz;
  for (float s : pcm) {
    const float c=std::max(-1.f,std::min(1.f,s));
    ds << qint16(qRound(c*32767.f));
  }
  return true;
}

static QVector<float> peaks(const QVector<float>& pcm, int nb) {
  QVector<float> r(nb,0.f);
  if (pcm.isEmpty()||nb<=0) return r;
  const int step=std::max(1,int(pcm.size())/nb);
  for (int b=0;b<nb;++b) {
    const int s=b*step, e=std::min(s+step,int(pcm.size()));
    float pk=0.f;
    for (int i=s;i<e;++i) pk=std::max(pk,std::abs(pcm[i]));
    r[b]=pk;
  }
  return r;
}

} // namespace wav

// ===========================================================================
// WaveformWidget
// ===========================================================================
class WaveformWidget : public QWidget {
public:
  explicit WaveformWidget(QWidget* p=nullptr):QWidget(p){
    setMinimumSize(200,40); setMaximumHeight(40);
  }
  void setPeaks(const QVector<float>& p){peaks_=p; update();}
protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.fillRect(rect(),QColor(0x1a,0x1a,0x2e));
    if (peaks_.isEmpty()) {
      p.setPen(QColor(80,80,80));
      p.drawText(rect(),Qt::AlignCenter,tr("no file"));
      return;
    }
    p.setPen(QColor(0x00,0xcc,0x88));
    const int w=width(),h=height(),n=peaks_.size();
    for (int i=0;i<w;++i) {
      const int idx=i*n/w;
      const int bh=int(peaks_[idx]*h*0.9f);
      const int y=(h-bh)/2;
      p.drawLine(i,y,i,y+bh);
    }
  }
private:
  QVector<float> peaks_;
};

// ===========================================================================
// DvkSlotWidget
// ===========================================================================
DvkSlotWidget::DvkSlotWidget(int slot, VoiceKeyer* vk, QWidget* parent)
  : QWidget(parent), slot_(slot), vk_(vk)
{
  auto* col=new QVBoxLayout(this);
  col->setContentsMargins(4,2,4,2); col->setSpacing(2);

  // Top row
  auto* top=new QHBoxLayout;
  auto* lbl=new QLabel(QStringLiteral("F%1").arg(slot+1),this);
  lbl->setMinimumWidth(22);
  lbl->setStyleSheet("font-weight:bold;color:#ffa040;");
  nameLabel_=new QLabel(tr("(no file)"),this);
  nameLabel_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
  nameLabel_->setStyleSheet("color:#ccc;font-size:11px;");
  loadBtn_=new QPushButton(tr("Load..."),this); loadBtn_->setFixedWidth(60);
  recBtn_ =new QPushButton(tr("⏺"),this);    recBtn_->setFixedWidth(32);
  recBtn_->setToolTip(tr("Record from microphone"));
  recBtn_->setStyleSheet("color:#ff4040;");
  playBtn_=new QPushButton(tr("▶"),this);    playBtn_->setFixedWidth(32);
  playBtn_->setEnabled(false);
  saveBtn_=new QPushButton(tr("Save processed"),this); saveBtn_->setEnabled(false);
  top->addWidget(lbl); top->addWidget(nameLabel_);
  top->addWidget(loadBtn_); top->addWidget(recBtn_);
  top->addWidget(playBtn_); top->addWidget(saveBtn_);
  col->addLayout(top);

  // Waveform
  auto* ww=new WaveformWidget(this); waveWidget_=ww;
  col->addWidget(ww);

  // DSP row
  auto* dsp=new QHBoxLayout; dsp->setSpacing(6);
  auto mk=[this](double lo,double hi,double v,const QString& sfx){
    auto* s=new QDoubleSpinBox(this);
    s->setRange(lo,hi); s->setValue(v); s->setSuffix(sfx);
    s->setDecimals(1); s->setFixedWidth(78);
    return s;
  };
  gainSpin_ =mk(-20,20, 0," dB");
  hpfCheck_ =new QCheckBox(tr("HPF"),this);
  hpfSpin_  =mk(50,500,100," Hz"); hpfSpin_->setEnabled(false);
  lowSpin_  =mk(-12,12, 0," dB");
  midSpin_  =mk(-12,12, 0," dB");
  highSpin_ =mk(-12,12, 0," dB");
  thrSpin_  =mk(-40, 0,-18," dB");
  ratioSpin_=mk(1,20,  4,":1");

  auto lbl2=[this](const QString& t){ return new QLabel(t,this); };
  dsp->addWidget(lbl2(tr("Gain"))); dsp->addWidget(gainSpin_);
  dsp->addWidget(hpfCheck_);        dsp->addWidget(hpfSpin_);
  dsp->addWidget(lbl2(tr("Lo")));   dsp->addWidget(lowSpin_);
  dsp->addWidget(lbl2(tr("Mid")));  dsp->addWidget(midSpin_);
  dsp->addWidget(lbl2(tr("Hi")));   dsp->addWidget(highSpin_);
  dsp->addWidget(lbl2(tr("Thr"))); dsp->addWidget(thrSpin_);
  dsp->addWidget(lbl2(tr("Ratio")));dsp->addWidget(ratioSpin_);
  dsp->addStretch();
  col->addLayout(dsp);

  // Separator
  auto* sep=new QFrame(this); sep->setFrameShape(QFrame::HLine);
  sep->setStyleSheet("color:#2a2a3a;");
  col->addWidget(sep);

  connect(loadBtn_,&QPushButton::clicked,this,&DvkSlotWidget::onLoad);
  connect(recBtn_, &QPushButton::clicked,this,&DvkSlotWidget::onRecord);
  connect(playBtn_,&QPushButton::clicked,this,&DvkSlotWidget::onPlayStop);
  connect(saveBtn_,&QPushButton::clicked,this,&DvkSlotWidget::onSave);
  connect(hpfCheck_,&QCheckBox::toggled,hpfSpin_,&QWidget::setEnabled);
}

DvkSlotWidget::~DvkSlotWidget() {
  if (recording_) stopRecording();
}

void DvkSlotWidget::setPath(const QString& p) {
  path_=p;
  if (p.isEmpty()) {
    nameLabel_->setText(tr("(no file)"));
    static_cast<WaveformWidget*>(waveWidget_)->setPeaks({});
    playBtn_->setEnabled(false); saveBtn_->setEnabled(false);
  } else {
    buildWaveform(p);
    nameLabel_->setText(QFileInfo(p).fileName());
    playBtn_->setEnabled(true); saveBtn_->setEnabled(true);
  }
}

void DvkSlotWidget::setPlaying(bool on) {
  playing_=on;
  playBtn_->setText(on ? tr("■") : tr("▶"));
}

void DvkSlotWidget::onLoad() {
  const QString p=QFileDialog::getOpenFileName(
      this,tr("Load WAV for F%1").arg(slot_+1),
      QString(),tr("WAV files (*.wav);;All files (*)"));
  if (p.isEmpty()) return;
  if (vk_) vk_->loadSlot(slot_,p);
  setPath(p);
}

void DvkSlotWidget::onRecord() {
  if (recording_) {
    stopRecording();
  } else {
    startRecording();
  }
}

QString DvkSlotWidget::recordPath() const {
  QString dir;
  if (!path_.isEmpty())
    dir = QFileInfo(path_).absolutePath();
  else
    dir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
  return QDir(dir).filePath(QStringLiteral("f%1_rec.wav").arg(slot_+1));
}

void DvkSlotWidget::startRecording() {
  const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
  if (inputDevice.isNull()) {
    QMessageBox::warning(this, tr("DVK"),
        tr("No microphone found.\nConnect a microphone and try again."));
    return;
  }

  QAudioFormat fmt;
  fmt.setSampleRate(48000);
  fmt.setChannelCount(1);
  fmt.setSampleFormat(QAudioFormat::Int16);

  if (!inputDevice.isFormatSupported(fmt)) {
    fmt.setChannelCount(2);
    if (!inputDevice.isFormatSupported(fmt)) {
      fmt = inputDevice.preferredFormat();
    }
  }

  recSampleRate_ = fmt.sampleRate();
  recBuf_.clear();

  audioSource_ = new QAudioSource(inputDevice, fmt, this);
  audioDevice_ = audioSource_->start();

  if (!audioDevice_ || audioSource_->error() != QAudio::NoError) {
    QMessageBox::warning(this, tr("DVK"),
        tr("Could not open microphone for recording."));
    delete audioSource_;
    audioSource_ = nullptr;
    audioDevice_ = nullptr;
    return;
  }

  connect(audioDevice_, &QIODevice::readyRead, this, [this]() {
    const QByteArray data = audioDevice_->readAll();
    if (data.isEmpty()) return;

    const QAudioFormat fmt = audioSource_->format();
    const int ch = fmt.channelCount();

    if (fmt.sampleFormat() == QAudioFormat::Int16) {
      const qint16* p = reinterpret_cast<const qint16*>(data.constData());
      const int frames = data.size() / fmt.bytesPerFrame();
      recBuf_.reserve(recBuf_.size() + frames);
      for (int i = 0; i < frames; ++i) {
        double sum = 0;
        for (int c = 0; c < ch; ++c)
          sum += p[i * ch + c] / 32768.0;
        recBuf_.append(float(sum / ch));
      }
    } else if (fmt.sampleFormat() == QAudioFormat::Float) {
      const float* p = reinterpret_cast<const float*>(data.constData());
      const int frames = data.size() / fmt.bytesPerFrame();
      recBuf_.reserve(recBuf_.size() + frames);
      for (int i = 0; i < frames; ++i) {
        double sum = 0;
        for (int c = 0; c < ch; ++c)
          sum += p[i * ch + c];
        recBuf_.append(float(sum / ch));
      }
    }
  });

  recording_ = true;
  recBtn_->setText(tr("■"));
  recBtn_->setStyleSheet("color:#ff8800; font-weight:bold;");
  recBtn_->setToolTip(tr("Stop recording"));
  loadBtn_->setEnabled(false);
  playBtn_->setEnabled(false);
}

void DvkSlotWidget::stopRecording() {
  if (!audioSource_) return;

  audioSource_->stop();
  if (audioDevice_)
    disconnect(audioDevice_, nullptr, this, nullptr);
  delete audioSource_;
  audioSource_ = nullptr;
  audioDevice_ = nullptr;
  recording_ = false;

  recBtn_->setText(tr("⏺"));
  recBtn_->setStyleSheet("color:#ff4040;");
  recBtn_->setToolTip(tr("Record from microphone"));
  loadBtn_->setEnabled(true);

  if (recBuf_.isEmpty()) {
    QMessageBox::information(this, tr("DVK"),
        tr("Recording stopped - no audio captured."));
    return;
  }

  const QString outPath = recordPath();
  if (!wav::write16(outPath, recBuf_, quint32(recSampleRate_))) {
    QMessageBox::warning(this, tr("DVK"),
        tr("Could not save recording to:\n%1").arg(outPath));
    return;
  }

  if (vk_) vk_->loadSlot(slot_, outPath);
  setPath(outPath);
}

void DvkSlotWidget::onPlayStop() {
  if (playing_) emit stopRequested();
  else if (!path_.isEmpty()) emit playRequested(slot_);
}

void DvkSlotWidget::onSave() {
  emit saveProcessed(slot_,
      float(gainSpin_->value()),
      hpfCheck_->isChecked(), float(hpfSpin_->value()),
      float(lowSpin_->value()), float(midSpin_->value()), float(highSpin_->value()),
      float(thrSpin_->value()), float(ratioSpin_->value()));
}

void DvkSlotWidget::buildWaveform(const QString& path) {
  wav::Info info; QVector<float> pcm;
  if (!wav::read(path,pcm,info)) return;
  static_cast<WaveformWidget*>(waveWidget_)->setPeaks(wav::peaks(pcm,400));
}

// ===========================================================================
// DvkModeWidget
// ===========================================================================
DvkModeWidget::DvkModeWidget(VoiceKeyer* vk, QWidget* parent)
  : QWidget(parent), vk_(vk)
{
  auto* outer=new QVBoxLayout(this);
  outer->setContentsMargins(0,0,0,0);

  auto* title=new QLabel(tr("DVK — Voice Macro Editor"),this);
  title->setStyleSheet("font-size:13px;font-weight:bold;color:#ffa040;padding:4px 8px;");
  outer->addWidget(title);

  auto* scroll=new QScrollArea(this); scroll->setWidgetResizable(true);
  auto* inner=new QWidget(scroll);
  auto* vbl=new QVBoxLayout(inner);
  vbl->setSpacing(0); vbl->setContentsMargins(4,4,4,4);

  for (int i=0;i<9;++i) {
    rows_[i]=new DvkSlotWidget(i,vk,inner);
    vbl->addWidget(rows_[i]);
    connect(rows_[i],&DvkSlotWidget::playRequested,
            this,[this](int s){ if(vk_) vk_->play(s); });
    connect(rows_[i],&DvkSlotWidget::stopRequested,
            this,[this](){ if(vk_) vk_->stop(); });
    connect(rows_[i],&DvkSlotWidget::saveProcessed,
            this,&DvkModeWidget::onSaveProcessed);
  }
  vbl->addStretch();
  scroll->setWidget(inner);
  outer->addWidget(scroll);
}

void DvkModeWidget::triggerSlot(int slot) {
  if (slot<0||slot>=9) return;
  if (vk_&&vk_->isPlaying()) vk_->stop();
  else if (vk_) vk_->play(slot);
}

void DvkModeWidget::onPlaybackStarted(int slot) {
  for (int i=0;i<9;++i) rows_[i]->setPlaying(i==slot);
}

void DvkModeWidget::onPlaybackFinished(int slot) {
  Q_UNUSED(slot)
  for (int i=0;i<9;++i) rows_[i]->setPlaying(false);
}

void DvkModeWidget::onSaveProcessed(int slot,
    float gainDb, bool hpfOn, float hpfHz,
    float lowDb, float midDb, float highDb,
    float compThreshDb, float compRatio)
{
  if (slot<0||slot>=9) return;
  const QString path=rows_[slot]->path();
  if (path.isEmpty()) return;

  wav::Info info; QVector<float> pcm;
  if (!wav::read(path,pcm,info)) {
    QMessageBox::warning(this,tr("DVK"),tr("Cannot read:\n%1").arg(path));
    return;
  }

  if (hpfOn) {
    dsp::Biquad hpf=dsp::makeHpf(hpfHz,float(info.sampleRate));
    for (float& s:pcm) s=hpf.process(s);
  }
  if (std::abs(lowDb)>0.01f) {
    dsp::Biquad f=dsp::makeLowShelf(lowDb,200.f,float(info.sampleRate));
    for (float& s:pcm) s=f.process(s);
  }
  if (std::abs(midDb)>0.01f) {
    dsp::Biquad f=dsp::makePeak(midDb,1000.f,float(info.sampleRate));
    for (float& s:pcm) s=f.process(s);
  }
  if (std::abs(highDb)>0.01f) {
    dsp::Biquad f=dsp::makeHighShelf(highDb,4000.f,float(info.sampleRate));
    for (float& s:pcm) s=f.process(s);
  }
  dsp::compress(pcm,compThreshDb,compRatio);
  dsp::applyGain(pcm,gainDb);
  dsp::normalize(pcm);

  const QFileInfo fi(path);
  const QString outPath=fi.dir().filePath(
      fi.completeBaseName()+QStringLiteral("_processed.wav"));

  if (!wav::write16(outPath,pcm,info.sampleRate)) {
    QMessageBox::warning(this,tr("DVK"),tr("Cannot write:\n%1").arg(outPath));
    return;
  }
  if (vk_) vk_->loadSlot(slot,outPath);
  rows_[slot]->setPath(outPath);
  QMessageBox::information(this,tr("DVK"),
      tr("Saved and loaded:\n%1").arg(outPath));
}

void DvkModeWidget::saveSettings(const QString& prefix) const {
  QSettings s;
  for (int i=0;i<9;++i)
    s.setValue(prefix+QStringLiteral("/slot%1").arg(i),rows_[i]->path());
}

void DvkModeWidget::loadSettings(const QString& prefix) {
  QSettings s;
  for (int i=0;i<9;++i) {
    const QString p=s.value(prefix+QStringLiteral("/slot%1").arg(i)).toString();
    if (!p.isEmpty()) { rows_[i]->setPath(p); if(vk_) vk_->loadSlot(i,p); }
  }
}
