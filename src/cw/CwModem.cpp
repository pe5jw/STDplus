// STDplus — CW modem: fldigi-derived decoder + sinus TX encoder
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The CW decoding algorithm is ported from fldigi (W1HKJ et al., GPL-3+).
// No fldigi headers or libraries are required; only the algorithm is used.
// Source reference: fldigi src/cw_rtty/cw.cxx  (Schmitt-trigger AGC,
// dot/dash adaptive tracking, Morse character lookup).

#include "CwModem.h"

#include <QVector>
#include <QDebug>
#include <cmath>
#include <cstring>
#include <string>
#include <algorithm>

#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif
#define TWOPI (2.0 * M_PI)

// ---------------------------------------------------------------------------
// Resampler (integer ratio 48000/8000 = 6)
// ---------------------------------------------------------------------------
static constexpr int kTciRate    = 48000;
static constexpr int kCwRate     =  8000;  // fldigi internal rate
static constexpr int kRatio      = kTciRate / kCwRate;  // 6

static QVector<float> downsample6(const QVector<float>& in) {
    const int n = in.size() / kRatio;
    QVector<float> out(n);
    for (int i = 0; i < n; ++i) {
        float s = 0.f;
        for (int j = 0; j < kRatio; ++j) s += in[i*kRatio+j];
        out[i] = s / kRatio;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Goertzel single-bin power (replace fldigi's FFT filter for simplicity)
// ---------------------------------------------------------------------------
static double goertzelPower(const double* buf, int len, double freq, double rate) {
    const double w  = TWOPI * freq / rate;
    const double cr = 2.0 * std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < len; ++i) {
        s0 = buf[i] + cr*s1 - s2;
        s2 = s1; s1 = s0;
    }
    return s1*s1 + s2*s2 - s1*s2*cr;
}

// ---------------------------------------------------------------------------
// decayavg — ported from fldigi misc.h
// ---------------------------------------------------------------------------
static inline double decayavg(double avg, double val, int weight) {
    if (weight <= 1) return val;
    return avg + (val - avg) / weight;
}
static inline double clamp(double v, double lo, double hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

// ---------------------------------------------------------------------------
// Morse look-up table (a-z, 0-9, punctuation)
// ---------------------------------------------------------------------------
static const struct { const char* code; char ch; } kMorseTable[] = {
    {".-",   'A'}, {"-...", 'B'}, {"-.-.", 'C'}, {"-..",  'D'},
    {".",    'E'}, {"..-.", 'F'}, {"--.",  'G'}, {"....", 'H'},
    {"..",   'I'}, {".---", 'J'}, {"-.-",  'K'}, {".-..", 'L'},
    {"--",   'M'}, {"-.",   'N'}, {"---",  'O'}, {".--.", 'P'},
    {"--.-", 'Q'}, {".-.",  'R'}, {"...",  'S'}, {"-",    'T'},
    {"..-",  'U'}, {"...-", 'V'}, {".--",  'W'}, {"-..-", 'X'},
    {"-.--", 'Y'}, {"--..", 'Z'},
    {"-----",'0'}, {".----",'1'}, {"..---",'2'}, {"...--",'3'},
    {"....-",'4'}, {".....", '5'},{"-....","6"[0]},{"--...","7"[0]},
    {"---..","8"[0]},{"----.","9"[0]},
    {".-.-.-",'.'}, {"--..--",','}, {"..--..",'?'}, {"-..-.", '/'},
    {nullptr, 0}
};

static char morseToChar(const std::string& code) {
    for (int i = 0; kMorseTable[i].code; ++i)
        if (code == kMorseTable[i].code)
            return kMorseTable[i].ch;
    return '?';
}

// TX encoder table
static const struct { char c; const char* code; } kTxTable[] = {
    {'A',".-"},  {'B',"-..."},{'C',"-.-."},{'D',"-.."},{'E',"."},
    {'F',"..-."},{'G',"--."},{'H',"...."},{'I',".."},{'J',".---"},
    {'K',"-.-"},{'L',".-.."},{'M',"--"},{'N',"-."},{'O',"---"},
    {'P',".--."},{'Q',"--.-"},{'R',".-."},{'S',"..."},{'T',"-"},
    {'U',"..-"},{'V',"...-"},{'W',".--"},{'X',"-..-"},{'Y',"-.--"},
    {'Z',"--.."},
    {'0',"-----"},{'1',".----"},{'2',"..---"},{'3',"...--"},
    {'4',"....-"},{'5',"....."},{'6',"-...."},{'7',"--..."},
    {'8',"---.."},{'9',"----."},
    {'/',"-..-."},{',',"--..--"},{'?',"..--.."},{'.',".-.-.-"},
    {'\0', nullptr}
};

static const char* morseFor(char c) {
    c = (char)toupper((unsigned char)c);
    for (int i = 0; kTxTable[i].c; ++i)
        if (kTxTable[i].c == c) return kTxTable[i].code;
    return nullptr;
}

// ---------------------------------------------------------------------------
// CW RX state machine — ported from fldigi cw.cxx
// ---------------------------------------------------------------------------
struct CwModem::FldigiCw {
    // --- settings ---
    double frequency = 700.0;   // AF tone Hz
    int    wpm       = 20;

    // KWPM = 1200000 µs (Morse timing constant in microseconds)
    // At 8000 Hz: dot_samples = samplerate * 1.2 / wpm
    static constexpr int kRate = kCwRate;
    static constexpr long KWPM = 1200000L;  // microseconds

    long dotLen()  { return KWPM / wpm; }  // µs
    long dashLen() { return 3 * dotLen(); }

    // Convert µs to samples at kRate
    int usToSamples(long us) {
        return (int)(us * (long)kRate / 1000000L);
    }

    // --- AGC / Schmitt trigger state ---
    double sig_avg    = 0.0;
    double agc_peak   = 1e-6;
    double noise_floor= 1e-6;
    double metric     = 0.0;
    double siglevel   = 0.0;

    double upper_threshold = 0.6;
    double lower_threshold = 0.4;

    static constexpr int kAttack = 200;
    static constexpr int kDecay  = 1000;

    // --- dot/dash adaptive tracking ---
    long two_dots = 0;    // 2-dot threshold in µs
    long cw_noise_spike_threshold = 0;
    int  cw_speed = 20;

    // --- tone detector state ---
    enum RxState { RS_IDLE, RS_IN_TONE } rxState = RS_IDLE;
    unsigned int sampleCounter  = 0;  // current time in samples
    unsigned int toneStartSample= 0;
    unsigned int toneEndSample  = 0;
    bool         squelchPass    = true;

    // --- character buffer ---
    static constexpr int kMaxElements = 7;
    char elements[kMaxElements+1] = {};   // '.' and '-' chars
    int  nElements = 0;

    // --- output ---
    std::string decoded;  // characters ready to deliver

    // --- Goertzel state ---
    double gw = 0.0;   // Goertzel coefficient
    double gs1= 0.0;   // running IIR state
    double gs2= 0.0;
    // Block-based: re-init every kBlock samples
    static constexpr int kBlock = 64;  // ~8 ms at 8 kHz
    int    blockPos = 0;
    double blockAcc = 0.0;  // accumulated power in this block

    void init() {
        gw = 2.0 * std::cos(TWOPI * frequency / kRate);
        two_dots = 2 * dotLen();
        cw_noise_spike_threshold = dotLen() / 2;
        sampleCounter  = 0;
        rxState        = RS_IDLE;
        nElements      = 0;
        toneStartSample= 0;
        toneEndSample  = 0;
    }

    // Feed one sample, get a tone-power value using IIR Goertzel
    double processSample(double s) {
        double s0 = s + gw*gs1 - gs2;
        gs2 = gs1; gs1 = s0;
        blockAcc += (gs1*gs1 + gs2*gs2 - gs1*gs2*gw);
        ++blockPos;
        if (blockPos < kBlock) return -1.0;  // not ready yet
        double power = blockAcc / kBlock;
        blockAcc = 0.0;
        blockPos = 0;
        gs1 = gs2 = 0.0;  // reset to avoid DC drift
        return std::sqrt(std::max(power, 0.0));
    }

    // Decode current elements buffer → character, reset buffer
    void decodeSymbol(long spaceUsec) {
        if (nElements > 0) {
            elements[nElements] = '\0';
            char c = morseToChar(std::string(elements));
            decoded += c;
            nElements = 0;
            memset(elements, 0, sizeof(elements));
        }
        if (spaceUsec > 7 * dotLen() / 2)
            decoded += ' ';
    }

    // Handle a decoded tone: dur = tone duration in µs
    void handleTone(long durUsec) {
        if (durUsec < cw_noise_spike_threshold) return; // noise spike
        if (nElements >= kMaxElements) return;

        // Adaptive threshold: if duration > two_dots/2 → dash, else dot
        char sym = (durUsec > two_dots / 2) ? '-' : '.';
        elements[nElements++] = sym;

        // Update two_dots tracking (simple IIR, same as fldigi)
        two_dots = (long)(0.9 * two_dots + 0.1 * 2 * durUsec);
        // Clamp to reasonable range
        long minDot = KWPM / 60;  // 60 wpm max
        long maxDot = KWPM / 5;   // 5 wpm min
        if (two_dots < 2*minDot) two_dots = 2*minDot;
        if (two_dots > 2*maxDot) two_dots = 2*maxDot;
    }

    // Main per-block processing, called with block power value
    void decode(double value) {
        // AGC
        int attack = kAttack;
        int decay  = kDecay;
        sig_avg    = decayavg(sig_avg, value, decay);
        if (value < sig_avg)
            noise_floor = decayavg(noise_floor, value,
                          value < noise_floor ? attack : decay);
        else
            agc_peak = decayavg(agc_peak, value,
                       value > agc_peak ? attack : decay);

        double norm_noise = noise_floor / (agc_peak + 1e-12);
        double norm_sig   = sig_avg     / (agc_peak + 1e-12);
        siglevel          = norm_sig;

        double diff = norm_sig - norm_noise;
        upper_threshold = norm_sig   - 0.2 * diff;
        lower_threshold = norm_noise + 0.7 * diff;

        double normVal = agc_peak > 1e-9 ? value / agc_peak : 0.0;

        // Schmitt trigger
        if (normVal > upper_threshold && rxState != RS_IN_TONE) {
            toneStartSample = sampleCounter;
            rxState = RS_IN_TONE;
        }
        if (normVal < lower_threshold && rxState == RS_IN_TONE) {
            toneEndSample = sampleCounter;
            rxState = RS_IDLE;

            long toneDur  = (long)(toneEndSample - toneStartSample) * 1000000L / kRate;
            handleTone(toneDur);
        }

        // Check inter-character / inter-word space when idle
        if (rxState == RS_IDLE && nElements > 0) {
            long silenceDur = (long)(sampleCounter - toneEndSample) * 1000000L / kRate;
            long charSpace  = 3 * dotLen() / 2;  // threshold between element-space and char-space
            if (silenceDur > charSpace)
                decodeSymbol(silenceDur);
        }

        sampleCounter += kBlock;
    }

    std::string fetchDecoded() {
        std::string out;
        out.swap(decoded);
        return out;
    }
};

// ---------------------------------------------------------------------------
// CwModem — QObject wrapper
// ---------------------------------------------------------------------------
CwModem::CwModem(QObject* parent) : QObject(parent) {
    initFldigi();
}

CwModem::~CwModem() {
    delete fldigi_;
}

void CwModem::setClient(TciClient* client) {
    client_ = client;
}

void CwModem::setCwToneHz(double hz) {
    cwToneHz_ = hz;
    if (fldigi_) {
        fldigi_->frequency = hz;
        fldigi_->gw = 2.0 * std::cos(TWOPI * hz / kCwRate);
    }
}

void CwModem::setWpm(int wpm) {
    wpm_ = wpm;
    if (fldigi_) {
        fldigi_->wpm = wpm;
        fldigi_->two_dots = 2 * fldigi_->dotLen();
        fldigi_->cw_noise_spike_threshold = fldigi_->dotLen() / 2;
    }
}

void CwModem::initFldigi() {
    fldigi_ = new FldigiCw();
    fldigi_->frequency = cwToneHz_;
    fldigi_->wpm       = wpm_;
    fldigi_->init();
    fldigiOk_ = true;
    qDebug() << "[CwModem] fldigi-derived CW decoder active, tone="
             << cwToneHz_ << "Hz, wpm=" << wpm_;
}

// ---------------------------------------------------------------------------
// RX: 48 kHz TCI audio → downsample → fldigi algorithm → textDecoded
// ---------------------------------------------------------------------------
void CwModem::processAudio(const QVector<float>& samples) {
    if (!fldigiOk_ || !fldigi_)
        return;

    const QVector<float> down = downsample6(samples);

    for (int i = 0; i < down.size(); ++i) {
        double power = fldigi_->processSample((double)down[i]);
        if (power >= 0.0)              // block boundary
            fldigi_->decode(power);
    }

    std::string dec = fldigi_->fetchDecoded();
    if (!dec.empty())
        emit textDecoded(QString::fromStdString(dec));
}

// ---------------------------------------------------------------------------
// TX: text → Morse timing → sinus PCM at 48 kHz
// ---------------------------------------------------------------------------
void CwModem::sendText(const QString& text) {
    if (!client_)
        return;

    // dot = 1200ms/wpm samples at 48 kHz
    const int dotSamples = (int)(kTciRate * 1.2 / wpm_);
    const double omega   = TWOPI * cwToneHz_ / kTciRate;

    QVector<float> audio;
    audio.reserve(text.size() * 20 * dotSamples);

    double phase = 0.0;
    auto addTone    = [&](int n){ for(int i=0;i<n;++i){ audio.append((float)(0.7*std::sin(phase))); phase+=omega; } };
    auto addSilence = [&](int n){ for(int i=0;i<n;++i){ audio.append(0.f); } };

    for (QChar qc : text) {
        if (qc == QLatin1Char(' ')) { addSilence(7*dotSamples); continue; }
        const char* code = morseFor(qc.toLatin1());
        if (!code) continue;
        for (int i = 0; code[i]; ++i) {
            if (i > 0) addSilence(dotSamples);
            code[i] == '.' ? addTone(dotSamples) : addTone(3*dotSamples);
        }
        addSilence(3*dotSamples);
    }

    // TODO: stream audio to TciClient in 20 ms / 960-sample chunks.
    // Requires TciClient::sendAudio() — hook up when that API is available.
    Q_UNUSED(audio)
    emit txFinished();
}

void CwModem::stopTx() {
    emit txFinished();
}
