#include "SettingsWindow.h"
#include <QTimer>
#include <QWidget>
#include <qt_windows.h>

void showSettingsWindow(QWidget* window) {
    // Finish closing the tray menu before activating its settings window.
    QTimer::singleShot(0, window, [window] {
        window->showNormal();
        const auto handle = reinterpret_cast<HWND>(window->winId());
        // STARTF_USESHOWWINDOW/SW_HIDE can suppress Qt's first native show.
        // Explicitly show it again after that first show has been consumed.
        ShowWindow(handle, SW_SHOWNORMAL);
        window->raise(); window->activateWindow();
        SetForegroundWindow(handle);
    });
}
