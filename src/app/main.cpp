// STDplus — application entry point
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replaces std/src/main.cpp.  Identical setup, but:
//  - Uses MainWindowPlus (adds VoiceKeyer, N1mmCat, TciDialect)
//  - Uses same QSettings as STD (org=N8SDR, app=STD) so settings are shared
//  - ApplicationDisplayName = "STDplus" (window title only)

#include "MainWindowPlus.h"
#include "WaterfallGpu.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStyleFactory>
#include <QTimer>
#include <QPalette>

namespace {

QPalette darkPalette()
{
  QPalette palette;
  const QColor window("#06090e");
  const QColor panel("#0f1720");
  const QColor text("#cdd9e5");
  const QColor muted("#6d8496");
  const QColor cyan("#00e5ff");
  palette.setColor(QPalette::Window,          window);
  palette.setColor(QPalette::WindowText,      text);
  palette.setColor(QPalette::Base,            QColor("#080d13"));
  palette.setColor(QPalette::AlternateBase,   panel);
  palette.setColor(QPalette::Text,            text);
  palette.setColor(QPalette::Button,          QColor("#121a24"));
  palette.setColor(QPalette::ButtonText,      text);
  palette.setColor(QPalette::BrightText,      cyan);
  palette.setColor(QPalette::Highlight,       cyan);
  palette.setColor(QPalette::HighlightedText, QColor("#061018"));
  palette.setColor(QPalette::PlaceholderText, muted);

  // Disabled
  palette.setColor(QPalette::Disabled, QPalette::WindowText, muted);
  palette.setColor(QPalette::Disabled, QPalette::Text,       muted);
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
  return palette;
}

QIcon stdplusIcon()
{
  // Re-use the upstream STD icon from the compiled resources.
  return QIcon(QStringLiteral(":/std/icon.png"));
}

}  // namespace

int main(int argc, char* argv[])
{
  const bool selftest = qEnvironmentVariableIsSet("STD_SELFTEST");
  WaterfallGpu::prepareProcessBackend(!selftest);

  QApplication app(argc, argv);
  QApplication::setOrganizationName("N8SDR");  // same as upstream STD — shared QSettings
  QApplication::setApplicationName("STD");     // same as upstream STD — shared QSettings
  QApplication::setApplicationDisplayName("STDplus");
  QApplication::setApplicationVersion(STDPLUS_VERSION);
  QApplication::setWindowIcon(stdplusIcon());

  if (QStyle* fusion = QStyleFactory::create("Fusion"))
    QApplication::setStyle(fusion);
  app.setPalette(darkPalette());

  QFile style(":/std/style.qss");
  if (style.open(QIODevice::ReadOnly))
    app.setStyleSheet(QString::fromUtf8(style.readAll()));
  else
    qWarning("STD stylesheet is missing from the binary resources");

  MainWindowPlus window;
  if (!selftest)
    window.primeGraphics();
  qInfo("STDplus %s — graphics backend %s",
        STDPLUS_VERSION,
        qPrintable(WaterfallGpu::processBackend()));
  window.show();

  return app.exec();
}
