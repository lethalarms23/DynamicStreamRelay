#include "app/ApplicationController.h"
#include "panel/WebPanelServer.h"
#include "ui/MainWindow.h"
#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RTMPTimeShiftProxy"); QCoreApplication::setApplicationName("RTMP TimeShift Proxy");
    QApplication::setStyle("Fusion");
    app.setStyleSheet(
        /* Base surfaces */
        "QWidget{background:#14171c;color:#e8eaed;font-size:13px;}"
        "QMainWindow{background:#0f1114;}"
        "#pages{background:transparent;}"

        /* Sidebar */
        "#sidebar{background:#0f1114;border-right:1px solid #23272e;}"
        "#nav{background:transparent;border:0;outline:0;padding:8px 10px;}"
        "#nav::item{padding:9px 14px;border-radius:6px;color:#9199a6;margin:1px 0;}"
        "#nav::item:hover{background:#1a1e24;color:#e8eaed;}"
        "#nav::item:selected{background:#152238;color:#7ab8ff;}"

        /* Top status strip */
        "#statusBar{background:#171a20;border-bottom:1px solid #23272e;}"
        "#overallBadge{background:#1e2229;border:1px solid #2b303a;border-radius:14px;}"

        /* Group panels within pages */
        "QGroupBox{border:1px solid #23272e;border-radius:8px;margin-top:14px;padding:14px 12px 12px;font-weight:600;background:#171a20;}"
        "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;color:#f4f7fb;}"

        /* Inputs */
        "QLineEdit,QSpinBox,QComboBox,QTableWidget{background:#1b1f26;border:1px solid #323844;border-radius:5px;padding:6px;selection-background-color:#3979d9;selection-color:#f4f7fb;}"
        "QLineEdit:focus,QSpinBox:focus,QComboBox:focus{border-color:#3979d9;}"
        "QTableWidget{alternate-background-color:#191d23;padding:0;gridline-color:#23272e;}"
        "QTableWidget::item{padding:5px 4px;}"
        "QTableWidget::item:selected{background:#152238;color:#7ab8ff;}"

        /* Buttons by role */
        "QPushButton{background:#262b33;border:1px solid #363c47;border-radius:6px;padding:8px 14px;font-weight:600;color:#e8eaed;}"
        "QPushButton:hover{background:#2e343d;border-color:#454c58;}"
        "QPushButton:pressed{background:#20242b;}"
        "QPushButton:disabled{background:#1a1d22;color:#565c66;border-color:#262b33;}"
        "QPushButton[role=\"primary\"]{background:#3979d9;border:0;color:#f4f7fb;}"
        "QPushButton[role=\"primary\"]:hover{background:#4c8bf0;}"
        "QPushButton[role=\"primary\"]:pressed{background:#2d63b8;}"
        "QPushButton[role=\"secondary\"]{background:#20242b;border:1px solid #323844;color:#e8eaed;}"
        "QPushButton[role=\"secondary\"]:hover{background:#262b33;border-color:#454c58;}"
        "QPushButton[role=\"danger\"]{background:transparent;border:1px solid #6e3038;color:#e5828a;}"
        "QPushButton[role=\"danger\"]:hover{background:#2a1a1c;border-color:#8a3a43;color:#ff9a9a;}"

        /* Tables/headers */
        "QHeaderView::section{background:#1b1f26;padding:8px;border:0;border-bottom:1px solid #23272e;font-weight:700;color:#9199a6;}"
        "QCheckBox{spacing:7px;}QSlider::groove:horizontal{height:5px;background:#262b33;border-radius:2px;}"
        "QSlider::handle:horizontal{width:16px;margin:-6px 0;background:#3979d9;border-radius:8px;}"
        "QScrollBar:vertical{background:#14171c;width:11px;margin:0;}"
        "QScrollBar::handle:vertical{background:#323844;border-radius:5px;min-height:24px;}"
        "QScrollBar::handle:vertical:hover{background:#454c58;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");
    rtsp::ApplicationController controller;
    // Created before the window and started immediately: independent of any
    // profile's relay lifecycle, and MainWindow needs it to display the
    // panel's URL/token in Ingest Settings.
    rtsp::WebPanelServer webPanel(controller);
    webPanel.start();
    rtsp::MainWindow window(controller, webPanel); window.resize(1100, 820); window.show();
    return app.exec();
}
