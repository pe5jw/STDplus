// STDplus — MainWindow subclass that wires in the STDplus features
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MainWindow.h"

class TciClient;
class N1mmCat;
class VoiceKeyer;
class CwModem;
class DvkModeWidget;
class QDockWidget;

// MainWindowPlus extends MainWindow with:
//  - STDplus branding (logo "STD+" replacing "STD", window icon overlay)
//  - VoiceKeyer (DVK) on F1-F9 with a toolbar
//  - N1mmCat TCP CAT bridge on port 4532
//  - About dialog with STDplus tab (Ko-Fi) + original STD tab
//
// It cannot replace tci_ (private in MainWindow), so it finds the
// TciClient via findChild<TciClient*>() after construction and wires
// the companions to it.
class MainWindowPlus : public MainWindow {
  Q_OBJECT
public:
  explicit MainWindowPlus(QWidget* parent = nullptr);

protected:
  void showEvent(QShowEvent* event) override;

private:
  void buildVoiceKeyerBar();
  void buildN1mmCat();
  void loadVoiceKeyerSettings();
  void saveVoiceKeyerSettings();
  void onVkPlaybackFinished(int slot);
  void patchBrandBar();   // replace "STD" label with "STD+"
  void showAboutPlus();   // tabbed about: STDplus + STD (original)

  TciClient* tci_         = nullptr;  // borrowed from MainWindow
  VoiceKeyer* voiceKeyer_ = nullptr;
  N1mmCat*    n1mmCat_    = nullptr;
  CwModem*       cwModem_    = nullptr;
  DvkModeWidget* dvkWidget_  = nullptr;
  QDockWidget*    dvkDock_    = nullptr;
  bool           featuresWired_ = false;
};


