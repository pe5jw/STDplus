// STDplus — Voice Keyer (DVK)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "VoiceKeyer.h"

#include "TciClient.h"

#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// WAV file parsing — enough for PCM (format 1) and IEEE float (format 3).
// We handle 8/16/24/32-bit PCM and 32-bit float; mono and stereo (we fold).
// ---------------------------------------------------------------------------

namespace {

// RIFF/WAVE chunk IDs as four-byte LE integers.
constexpr quint32 kRiff = 0x46464952u; // "RIFF"
constexpr quint32 kWave = 0x45564157u; // "WAVE"
constexpr quint32 kFmt  = 0x20746d66u; // "fmt "
constexpr quint32 kData = 0x61746164u; // "data"

// fmt chunk audio formats.
constexpr quint16 kFmtPcm   = 1;
constexpr quint16 kFmtFloat = 3;
constexpr quint16 kFmtExt   = 0xFFFE; // extensible — check subformat GUID

// IEEE 754 float subformat GUID first four bytes (little-endian u32).
constexpr quint32 kSubFloat = 0x00000003u;

struct WavFmt {
  quint16 format   = 0;
  quint16 channels = 0;
  quint32 sampleRate = 0;
  quint16 bitsPerSample = 0;
  quint32 subformat = 0; // only for extensible
};

// Read the fmt chunk.  Stream must be positioned just after the chunk size.
bool readFmt(QDataStream& ds, quint32 chunkSize, WavFmt& fmt) {
  if (chunkSize < 16)
    return false;
  ds >> fmt.format >> fmt.channels;
  ds >> fmt.sampleRate;
  quint32 byteRate; ds >> byteRate;
  quint16 blockAlign; ds >> blockAlign;
  ds >> fmt.bitsPerSample;
  quint32 consumed = 16;

  if (fmt.format == kFmtExt && chunkSize >= 40) {
    quint16 cbSize; ds >> cbSize; consumed += 2;
    if (cbSize >= 22) {
      quint16 validBits; ds >> validBits;
      quint32 channelMask; ds >> channelMask;
      ds >> fmt.subformat; // first 4 bytes of GUID
      // Skip rest of GUID (12 bytes).
      quint32 g2; quint32 g3; quint32 g4;
      ds >> g2 >> g3 >> g4;
      consumed += 22;
      // Reclassify.
      if (fmt.subformat == kSubFloat)
        fmt.format = kFmtFloat;
      else
        fmt.format = kFmtPcm; // assume PCM subtype
    }
  }
  // Skip any remaining fmt bytes.
  if (chunkSize > consumed) {
    const quint32 skip = chunkSize - consumed;
    for (quint32 i = 0; i < skip; ++i) {
      quint8 b; ds >> b;
    }
  }
  return ds.status() == QDataStream::Ok &&
         fmt.channels >= 1 && fmt.sampleRate > 0 &&
         fmt.bitsPerSample > 0 &&
         (fmt.format == kFmtPcm || fmt.format == kFmtFloat);
}

// Convert a single frame (all channels) to a mono float sample.
bool readFrame(QDataStream& ds, const WavFmt& fmt, float& mono) {
  double sum = 0.0;
  for (quint16 ch = 0; ch < fmt.channels; ++ch) {
    double sample = 0.0;
    if (fmt.format == kFmtFloat && fmt.bitsPerSample == 32) {
      float f; ds >> f; sample = f;
    } else if (fmt.format == kFmtPcm) {
      switch (fmt.bitsPerSample) {
        case 8: {
          quint8 u; ds >> u;
          sample = (static_cast<int>(u) - 128) / 128.0;
          break;
        }
        case 16: {
          qint16 s; ds >> s;
          sample = s / 32768.0;
          break;
        }
        case 24: {
          // 3-byte little-endian signed.
          quint8 b0, b1, b2; ds >> b0 >> b1 >> b2;
          qint32 v = static_cast<qint32>(b0) |
                     (static_cast<qint32>(b1) << 8) |
                     (static_cast<qint32>(b2) << 16);
          if (v & 0x800000) v |= 0xFF000000; // sign extend
          sample = v / 8388608.0;
          break;
        }
        case 32: {
          qint32 s; ds >> s;
          sample = s / 2147483648.0;
          break;
        }
        default:
          return false;
      }
    } else {
      return false;
    }
    sum += sample;
  }
  mono = static_cast<float>(sum / fmt.channels);
  return ds.status() == QDataStream::Ok;
}

} // namespace

// ---------------------------------------------------------------------------
// VoiceKeyer
// ---------------------------------------------------------------------------

VoiceKeyer::VoiceKeyer(QObject* parent) : QObject(parent) {
  timer_ = new QTimer(this);
  timer_->setInterval(20); // 20 ms push cycle
  connect(timer_, &QTimer::timeout, this, &VoiceKeyer::onTimer);
}

VoiceKeyer::~VoiceKeyer() {
  stop();
}

void VoiceKeyer::setClient(TciClient* client) {
  if (playing_)
    stop();
  client_ = client;
}

bool VoiceKeyer::loadSlot(int slot, const QString& path) {
  if (slot < 0 || slot >= kSlots)
    return false;
  QVector<float> pcm;
  QString err;
  if (!loadWav(path, pcm, err)) {
    emit loadError(slot, err);
    return false;
  }
  slots_[slot].pcm  = std::move(pcm);
  slots_[slot].path = path;
  qDebug() << "[VoiceKeyer] slot" << slot << "loaded" << path
           << slots_[slot].pcm.size() << "samples @48kHz";
  return true;
}

void VoiceKeyer::clearSlot(int slot) {
  if (slot < 0 || slot >= kSlots)
    return;
  if (playing_ && slot_ == slot)
    stop();
  slots_[slot].pcm.clear();
  slots_[slot].path.clear();
}

bool VoiceKeyer::play(int slot) {
  if (slot < 0 || slot >= kSlots)
    return false;
  if (slots_[slot].pcm.isEmpty()) {
    qDebug() << "[VoiceKeyer] slot" << slot << "is empty";
    return false;
  }
  if (!client_) {
    qDebug() << "[VoiceKeyer] no TCI client set";
    return false;
  }
  if (playing_)
    stop();

  slot_    = slot;
  pos_     = 0;
  playing_ = true;

  client_->setTransmit(true);
  timer_->start();
  emit playbackStarted(slot);
  return true;
}

void VoiceKeyer::stop() {
  if (!playing_)
    return;
  timer_->stop();
  playing_ = false;
  const int finished = slot_;
  slot_    = -1;
  pos_     = 0;
  if (client_)
    client_->setTransmit(false);
  emit playbackFinished(finished);
}

QString VoiceKeyer::slotPath(int slot) const {
  if (slot < 0 || slot >= kSlots)
    return {};
  return slots_[slot].path;
}

void VoiceKeyer::onTimer() {
  if (!playing_ || slot_ < 0)
    return;
  const QVector<float>& pcm = slots_[slot_].pcm;
  if (pos_ >= pcm.size()) {
    // All audio sent — drop TX.
    stop();
    return;
  }
  const int remaining = pcm.size() - pos_;
  const int n         = qMin(kChunk, remaining);
  client_->pushTxAudio(pcm.constData() + pos_, n, kRate, 1);
  pos_ += n;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

bool VoiceKeyer::loadWav(const QString& path, QVector<float>& out, QString& err) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    err = QStringLiteral("cannot open: ") + file.errorString();
    return false;
  }
  QDataStream ds(&file);
  ds.setByteOrder(QDataStream::LittleEndian);
  ds.setFloatingPointPrecision(QDataStream::SinglePrecision);

  // RIFF header.
  quint32 riffId, riffSize, waveId;
  ds >> riffId >> riffSize >> waveId;
  if (ds.status() != QDataStream::Ok || riffId != kRiff || waveId != kWave) {
    err = QStringLiteral("not a WAVE file");
    return false;
  }

  WavFmt fmt{};
  bool   gotFmt  = false;
  bool   gotData = false;

  while (ds.status() == QDataStream::Ok && !file.atEnd()) {
    quint32 chunkId, chunkSize;
    ds >> chunkId >> chunkSize;
    if (ds.status() != QDataStream::Ok)
      break;

    if (chunkId == kFmt) {
      if (!readFmt(ds, chunkSize, fmt)) {
        err = QStringLiteral("unsupported fmt chunk (format or depth)");
        return false;
      }
      gotFmt = true;
    } else if (chunkId == kData) {
      if (!gotFmt) {
        err = QStringLiteral("data chunk before fmt chunk");
        return false;
      }
      const int bytesPerSample = fmt.bitsPerSample / 8;
      const int frameBytes     = bytesPerSample * fmt.channels;
      if (frameBytes <= 0) {
        err = QStringLiteral("zero frame size");
        return false;
      }
      const int numFrames = static_cast<int>(chunkSize / frameBytes);
      QVector<float> mono;
      mono.reserve(numFrames);
      for (int i = 0; i < numFrames; ++i) {
        float s = 0.f;
        if (!readFrame(ds, fmt, s))
          break;
        mono.append(s);
      }
      if (static_cast<int>(fmt.sampleRate) == kRate) {
        out = std::move(mono);
      } else {
        out = resample(mono, static_cast<int>(fmt.sampleRate), kRate);
      }
      gotData = true;
      break; // done
    } else {
      // Skip unknown chunk (pad to word boundary).
      const quint32 skip = chunkSize + (chunkSize & 1);
      file.seek(file.pos() + skip);
    }
  }

  if (!gotData) {
    err = QStringLiteral("no data chunk found");
    return false;
  }
  if (out.isEmpty()) {
    err = QStringLiteral("WAV file is empty");
    return false;
  }
  return true;
}

QVector<float> VoiceKeyer::resample(const QVector<float>& in,
                                     int srcRate, int dstRate) {
  if (srcRate <= 0 || dstRate <= 0 || in.isEmpty())
    return {};
  if (srcRate == dstRate)
    return in;

  const double ratio    = static_cast<double>(srcRate) / dstRate;
  const int    outCount = static_cast<int>(in.size() / ratio);
  QVector<float> out;
  out.reserve(outCount);

  for (int i = 0; i < outCount; ++i) {
    const double pos = i * ratio;
    const int    lo  = static_cast<int>(pos);
    const int    hi  = lo + 1;
    const double frac = pos - lo;
    const float  a = in.at(lo);
    const float  b = hi < in.size() ? in.at(hi) : 0.f;
    out.append(static_cast<float>(a + frac * (b - a)));
  }
  return out;
}
