#pragma once
#include "app/ApplicationController.h"
#include "app/CapacityEstimator.h"
#include <QHash>
#include <QMainWindow>

class QLabel; class QLineEdit; class QSpinBox; class QSlider; class QTableWidget; class QComboBox; class QPushButton; class QCheckBox;

namespace rtsp {
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(ApplicationController& controller, QWidget* parent = nullptr);
protected: void closeEvent(QCloseEvent* event) override;
private slots:
    void updateSnapshot(AppSnapshot snapshot); void updateMetrics(StreamStatistics stats);
    void appendLog(QString timestamp, int severity, QString component, QString message);
private:
    QWidget* createStatus(); QWidget* createDashboard(); QWidget* createIngest(); QWidget* createIngestSettings(); QWidget* createDestination();
    QWidget* createDelay(); QWidget* createOutput(); QWidget* createCapacity(); QWidget* createMetrics(); QWidget* createLogs();
    void updateIngestModeUi();
    void updateProfileDashboard();
    ApplicationController& controller_; CapacityEstimator capacityEstimator_; AppSnapshot last_;
    QLabel *overall_{}, *ingestStatus_{}, *sourceStatus_{}, *destinationStatus_{}, *effective_{}, *requested_{}, *buffer_{}, *uptime_{}, *delayProgress_{};
    QLineEdit *serverUrl_{}, *localKey_{}, *fullUrl_{}, *destinationUrl_{}, *destinationKey_{};
    QLineEdit* listenInterface_{};
    QLineEdit *advertisedHost_{}, *srtPassphrase_{};
    QSpinBox* delaySeconds_{}; QSlider* delaySlider_{}; QTableWidget *metricsTable_{}, *logsTable_{}; QComboBox* severityFilter_{};
    QPushButton *startRelay_{}, *stopRelay_{};
    QCheckBox* rememberKey_{};
    QCheckBox* autoStartProfile_{};
    QComboBox* servicePreset_{};
    QComboBox* inputProtocol_{};
    QComboBox* profileSelector_{};
    QComboBox* delayPreset_{};
    QComboBox* videoEncoder_{};
    QComboBox* resolution_{};
    QSpinBox *outputFps_{}, *videoBitrate_{}, *keyframeInterval_{}, *audioBitrate_{};
    QComboBox* fillerMode_{}; QLineEdit* standbyImage_{}; QPushButton* browseStandby_{};
    QLineEdit* delayOverlayText_{};
    QCheckBox* allowLan_{};
    QCheckBox* srtEncryption_{};
    QSpinBox *srtPort_{}, *srtLatency_{};
    QLabel* ingestInstructions_{};
    QWidget *rtmpConnectionDetails_{}, *srtConnectionDetails_{};
    QTableWidget* profileTable_{};
    QLabel* profileSummary_{};
    QLabel* capacityResult_{};
    QPushButton* runCapacityBenchmark_{};
    QComboBox* capacityEncoder_{};
    QSpinBox *capacityWidth_{}, *capacityHeight_{}, *capacityFps_{}, *capacityBitrate_{},
        *capacityBuffer_{}, *capacityCandidates_{}, *capacitySeconds_{}, *capacitySafety_{};
    QHash<QString, AppSnapshot> sessionSnapshots_;
    QSpinBox *maximumBufferDuration_{}, *maximumBufferMemory_{};
};
}
