// STDplus — N1MM CAT TCP server + UDP broadcast
// SPDX-License-Identifier: GPL-3.0-or-later
#include "N1mmCat.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QDebug>
#include <QDateTime>

// Map TCI modulation tokens to IC-7300 mode bytes (N1MM MD<n>;).
static int modeToKenwood(const QString& tciMode) {
  const QString m = tciMode.toLower();
  if (m == QLatin1String("digu") || m == QLatin1String("usb"))  return 2;  // USB
  if (m == QLatin1String("digl") || m == QLatin1String("lsb"))  return 1;  // LSB
  if (m == QLatin1String("cw") || m == QLatin1String("cwu"))    return 3;  // CW
  if (m == QLatin1String("cwl"))                                 return 7;  // CW-R
  if (m == QLatin1String("am"))                                  return 5;  // AM
  if (m == QLatin1String("fm"))                                  return 4;  // FM
  return 2;
}

static QString kenwoodToTci(int mode) {
  switch (mode) {
    case 1: return QStringLiteral("digl");
    case 2: return QStringLiteral("digu");
    case 3: return QStringLiteral("cw");
    case 4: return QStringLiteral("fm");
    case 5: return QStringLiteral("am");
    case 7: return QStringLiteral("cw");
    default: return QStringLiteral("digu");
  }
}

// Map TCI mode token to N1MM mode string for the UDP ContactInfo packet.
static QString tciModeToN1mm(const QString& tciMode) {
  const QString m = tciMode.toLower();
  if (m == QLatin1String("cw") || m == QLatin1String("cwu") || m == QLatin1String("cwl"))
    return QStringLiteral("CW");
  if (m == QLatin1String("digu") || m == QLatin1String("usb"))
    return QStringLiteral("USB");
  if (m == QLatin1String("digl") || m == QLatin1String("lsb"))
    return QStringLiteral("LSB");
  if (m == QLatin1String("fm"))  return QStringLiteral("FM");
  if (m == QLatin1String("am"))  return QStringLiteral("AM");
  return QStringLiteral("USB");
}

N1mmCat::N1mmCat(QObject* parent) : QObject(parent) {
  server_ = new QTcpServer(this);
  connect(server_, &QTcpServer::newConnection, this, &N1mmCat::onNewConnection);
}

N1mmCat::~N1mmCat() {
  close();
}

bool N1mmCat::listen(quint16 port) {
  if (!server_->listen(QHostAddress::LocalHost, port)) {
    emit error(server_->errorString());
    return false;
  }
  // Create UDP socket for N1MM ContactInfo broadcast.
  if (!udp_) {
    udp_ = new QUdpSocket(this);
  }
  return true;
}

void N1mmCat::close() {
  if (client_) {
    client_->disconnectFromHost();
    client_ = nullptr;
  }
  server_->close();
  if (udp_) {
    udp_->close();
    udp_->deleteLater();
    udp_ = nullptr;
  }
}

bool N1mmCat::isListening() const { return server_->isListening(); }
quint16 N1mmCat::port() const { return server_->serverPort(); }

void N1mmCat::setFreqHz(qint64 hz)         { freqHz_  = hz; }
void N1mmCat::setMode(const QString& mode) { tciMode_ = mode; }
void N1mmCat::setPtt(bool on)              { ptt_     = on; }

// ---------------------------------------------------------------------------
// sendCallToN1mm — UDP ContactInfo datagram → N1MM fills its callsign field
// ---------------------------------------------------------------------------
// N1MM Logger+ listens on 127.0.0.1:12060 for XML ContactInfo datagrams.
// Minimal required fields: app, StationName, Freq, TXFreq, Mode, Call.
// N1MM copies Call into its Entry window callsign box automatically.
void N1mmCat::sendCallToN1mm(const QString& call)
{
  if (!udp_ || !isListening() || call.trimmed().isEmpty())
    return;

  // Frequency in kHz with 1 Hz resolution (N1MM expects "14074.000" etc.)
  const double freqKhz = freqHz_ / 1000.0;
  const QString freqStr = QString::number(freqKhz, 'f', 3);
  const QString modeStr = tciModeToN1mm(tciMode_);

  // N1MM ContactInfo XML — subset that fills the callsign field.
  const QString xml = QStringLiteral(
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<contactinfo>"
        "<app>STDplus</app>"
        "<StationName>STDplus</StationName>"
        "<Freq>%1</Freq>"
        "<TXFreq>%1</TXFreq>"
        "<Mode>%2</Mode>"
        "<Call>%3</Call>"
        "<IsRunQSO>0</IsRunQSO>"
        "<timestamp>%4</timestamp>"
      "</contactinfo>")
    .arg(freqStr)
    .arg(modeStr)
    .arg(call.trimmed().toUpper())
    .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

  const QByteArray datagram = xml.toUtf8();
  udp_->writeDatagram(datagram, QHostAddress::LocalHost, kUdpBroadcastPort);
  qDebug() << "[N1mmCat] UDP →" << call.trimmed().toUpper()
           << freqStr << modeStr;
}

// ---------------------------------------------------------------------------
// TCP CAT server
// ---------------------------------------------------------------------------
void N1mmCat::onNewConnection() {
  QTcpSocket* sock = server_->nextPendingConnection();
  if (!sock)
    return;
  if (client_) {
    sock->disconnectFromHost();
    sock->deleteLater();
    return;
  }
  client_ = sock;
  rxBuf_.clear();
  emit clientConnected(sock->peerAddress().toString());
  connect(sock, &QTcpSocket::readyRead, this, &N1mmCat::onReadyRead);
  connect(sock, &QTcpSocket::disconnected, this, [this, sock] {
    if (client_ == sock) {
      client_ = nullptr;
      rxBuf_.clear();
      emit clientDisconnected();
    }
    sock->deleteLater();
  });
}

void N1mmCat::onReadyRead() {
  if (!client_)
    return;
  rxBuf_ += client_->readAll();
  int end = -1;
  while ((end = rxBuf_.indexOf(';')) >= 0) {
    const QByteArray cmd = rxBuf_.left(end + 1);
    rxBuf_.remove(0, end + 1);
    if (!cmd.isEmpty())
      handleCommand(cmd, client_);
  }
}

void N1mmCat::handleCommand(const QByteArray& cmd, QTcpSocket* socket) {
  const QByteArray c = cmd.toUpper().trimmed();
  qDebug() << "[N1mmCat] <" << c;

  if (c == "TX;") {
    ptt_ = true;
    emit pttChangeRequested(true);
    return;
  }
  if (c == "RX;") {
    ptt_ = false;
    emit pttChangeRequested(false);
    return;
  }
  if (c == "PS;") { socket->write("PS1;");    return; }
  if (c == "ID;") { socket->write("ID019;");  return; }  // IC-7300
  if (c == "FA;") { socket->write(freqReply()); return; }
  if (c == "MD;") { socket->write(modeReply()); return; }

  // FA<11-digit freq>;
  if (c.startsWith("FA") && c.size() == 14) {
    bool ok = false;
    const qint64 hz = c.mid(2, 11).toLongLong(&ok);
    if (ok && hz > 0) {
      freqHz_ = hz;
      emit freqChangeRequested(hz);
    }
    return;
  }
  // MD<1 digit>;
  if (c.startsWith("MD") && c.size() == 4) {
    bool ok = false;
    const int mode = c.mid(2, 1).toInt(&ok);
    if (ok) {
      tciMode_ = kenwoodToTci(mode);
      emit modeChangeRequested(tciMode_);
    }
    return;
  }
  qDebug() << "[N1mmCat] unhandled:" << c;
}

QByteArray N1mmCat::freqReply() const {
  return QByteArray("FA")
       + QByteArray::number(freqHz_).rightJustified(11, '0')
       + ';';
}

QByteArray N1mmCat::modeReply() const {
  return QByteArray("MD")
       + QByteArray::number(modeToKenwood(tciMode_))
       + ';';
}
