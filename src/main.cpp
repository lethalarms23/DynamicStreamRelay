#include "app/ApplicationController.h"
#include "ui/MainWindow.h"
#include <QApplication>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RTMPTimeShiftProxy"); QCoreApplication::setApplicationName("RTMP TimeShift Proxy");
    QApplication::setStyle("Fusion");
    app.setStyleSheet(
        "QWidget{background:#15181d;color:#e7eaf0;font-size:13px;}"
        "QGroupBox{border:1px solid #303641;border-radius:7px;margin-top:12px;padding:12px 9px 9px;font-weight:600;background:#191d23;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 5px;color:#f4f7fb;}"
        "QLineEdit,QSpinBox,QComboBox,QTableWidget{background:#20252d;border:1px solid #39414d;border-radius:5px;padding:6px;selection-background-color:#2f6fdd;}"
        "QLineEdit:focus,QSpinBox:focus,QComboBox:focus{border-color:#3977d5;}"
        "QTableWidget{alternate-background-color:#1b2027;padding:0;}"
        "QPushButton{background:#2f6fdd;border:0;border-radius:5px;padding:8px 13px;font-weight:600;}"
        "QPushButton:hover{background:#3a7cf0;}QPushButton:pressed{background:#285fc2;}"
        "QPushButton:disabled{background:#303641;color:#737b88;}"
        "QTabWidget::pane{border:1px solid #303641;border-radius:5px;top:-1px;}"
        "QTabBar::tab{background:#1b2027;border:1px solid #303641;padding:9px 14px;margin-right:2px;border-top-left-radius:5px;border-top-right-radius:5px;}"
        "QTabBar::tab:selected{background:#252b34;color:#fff;border-bottom-color:#252b34;}"
        "QHeaderView::section{background:#252b34;padding:7px;border:0;border-right:1px solid #303641;font-weight:600;}"
        "QCheckBox{spacing:7px;}QSlider::groove:horizontal{height:5px;background:#303641;border-radius:2px;}"
        "QSlider::handle:horizontal{width:16px;margin:-6px 0;background:#3977d5;border-radius:8px;}");
    rtsp::ApplicationController controller; rtsp::MainWindow window(controller); window.resize(1100, 820); window.show();
    return app.exec();
}
