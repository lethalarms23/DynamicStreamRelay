#include "ui/MainWindow.h"
#include "media/UrlUtils.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QColor>
#include <QDesktopServices>
#include <QFormLayout>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace rtsp {
namespace {
QLabel* valueLabel(const QString& text = "—") { auto* l = new QLabel(text); l->setStyleSheet("font-size:15px;font-weight:600;color:#f4f7fb;"); return l; }
QWidget* row(std::initializer_list<QWidget*> widgets) { auto* w = new QWidget; auto* l = new QHBoxLayout(w); l->setContentsMargins(0,0,0,0); for (auto* x : widgets) l->addWidget(x); l->addStretch(); return w; }
QPushButton* copyButton(QLineEdit* source, const QString& text = "Copy") { auto* b = new QPushButton(text); QObject::connect(b, &QPushButton::clicked, source, [source]{ QApplication::clipboard()->setText(source->text()); }); return b; }
QString seconds(qint64 us) { return QString::number(us / 1000000.0, 'f', 1) + " s"; }
}

MainWindow::MainWindow(ApplicationController& c, QWidget* parent) : QMainWindow(parent), controller_(c), capacityEstimator_(this) {
    setWindowTitle("RTMP TimeShift Proxy");
    auto* root = new QWidget; auto* layout = new QVBoxLayout(root); layout->addWidget(createStatus());
    auto* tabs = new QTabWidget; auto* setup = new QWidget; auto* setupLayout = new QVBoxLayout(setup);
    setupLayout->addWidget(createIngest()); setupLayout->addWidget(createDestination()); setupLayout->addStretch();
    tabs->addTab(createDashboard(), "Streams"); tabs->addTab(setup, "Selected Profile"); tabs->addTab(createIngestSettings(), "Ingest Settings");
    tabs->addTab(createDelay(), "Delay"); tabs->addTab(createOutput(), "Output"); tabs->addTab(createCapacity(), "Capacity");
    tabs->addTab(createMetrics(), "Metrics"); tabs->addTab(createLogs(), "Logs");
    layout->addWidget(tabs, 1); setCentralWidget(root);
    connect(&controller_, &ApplicationController::snapshotChanged, this, &MainWindow::updateSnapshot);
    connect(&controller_, &ApplicationController::metricsChanged, this, &MainWindow::updateMetrics);
    connect(controller_.logger(), &Logger::entry, this, &MainWindow::appendLog);
    connect(&controller_, &ApplicationController::profilesChanged, this, [this](const QList<AppConfig>&, const QString& selected) {
        if (profileSelector_) {
            const QSignalBlocker blocker(profileSelector_);
            profileSelector_->clear();
            for (const auto& profile : controller_.profiles())
                profileSelector_->addItem(profile.profileName, profile.profileId);
            profileSelector_->setCurrentIndex(std::max(0, profileSelector_->findData(selected)));
        }
        updateProfileDashboard();
    });
    connect(&controller_, &ApplicationController::sessionSnapshotsChanged, this, [this](const QList<AppSnapshot>& snapshots) {
        sessionSnapshots_.clear();
        for (const auto& snapshot : snapshots) sessionSnapshots_.insert(snapshot.profileId, snapshot);
        updateProfileDashboard();
    });
    connect(&controller_, &ApplicationController::configChanged, this, [this](const AppConfig& c){
        localKey_->setText(c.localStreamKey);
        serverUrl_->setText(controller_.ingestServerUrl());
        fullUrl_->setText(controller_.ingestFullUrl());
        if (listenInterface_) listenInterface_->setText(c.listenAddress);
        if (advertisedHost_) advertisedHost_->setText(c.advertisedHost);
        if (inputProtocol_) {
            const QSignalBlocker blocker(inputProtocol_);
            inputProtocol_->setCurrentIndex(std::max(0, inputProtocol_->findData(c.inputProtocol)));
        }
        if (allowLan_) {
            const QSignalBlocker blocker(allowLan_);
            allowLan_->setChecked(c.listenAddress == "0.0.0.0");
        }
        if (srtPort_) { const QSignalBlocker blocker(srtPort_); srtPort_->setValue(c.srtPort); }
        if (srtLatency_) { const QSignalBlocker blocker(srtLatency_); srtLatency_->setValue(c.srtLatencyMs); }
        if (srtEncryption_) { const QSignalBlocker blocker(srtEncryption_); srtEncryption_->setChecked(c.srtEncryption); }
        if (delaySeconds_) delaySeconds_->setMaximum(c.maximumDelaySeconds);
        if (delaySlider_) delaySlider_->setMaximum(c.maximumDelaySeconds);
        if (maximumBufferDuration_) maximumBufferDuration_->setValue(c.maximumDelaySeconds);
        if (maximumBufferMemory_) maximumBufferMemory_->setValue(c.maximumBufferMiB);
        if (destinationUrl_) destinationUrl_->setText(c.destinationUrl);
        if (servicePreset_) { const QSignalBlocker blocker(servicePreset_); servicePreset_->setCurrentText(c.servicePreset); }
        if (autoStartProfile_) { const QSignalBlocker blocker(autoStartProfile_); autoStartProfile_->setChecked(c.autoStartRelay); }
        if (delaySeconds_) delaySeconds_->setValue(c.requestedDelaySeconds);
        if (delaySlider_) delaySlider_->setValue(c.requestedDelaySeconds);
        if (videoEncoder_) { const QSignalBlocker blocker(videoEncoder_); videoEncoder_->setCurrentIndex(std::max(0,videoEncoder_->findData(c.videoEncoder))); }
        if (resolution_) { const QSignalBlocker blocker(resolution_); resolution_->setCurrentIndex(std::max(0,resolution_->findData(QString("%1x%2").arg(c.width).arg(c.height)))); }
        if (outputFps_) outputFps_->setValue(c.fps);
        if (videoBitrate_) videoBitrate_->setValue(c.videoBitrateKbps);
        if (keyframeInterval_) keyframeInterval_->setValue(c.keyframeIntervalSeconds);
        if (audioBitrate_) audioBitrate_->setValue(c.audioBitrateKbps);
        if (fillerMode_) { const QSignalBlocker blocker(fillerMode_); fillerMode_->setCurrentIndex(std::max(0,fillerMode_->findData(c.fillerMode))); }
        if (standbyImage_) standbyImage_->setText(c.standbyImagePath);
        if (delayOverlayText_) delayOverlayText_->setText(c.delayOverlayText);
        updateIngestModeUi();
    });
    connect(&controller_, &ApplicationController::destinationCredentialLoaded, this, [this](const QString& key, bool remembered, bool secure){
        destinationKey_->setText(key); const QSignalBlocker blocker(rememberKey_); rememberKey_->setChecked(remembered);
        rememberKey_->setToolTip(remembered ? (secure ? "Stored in the operating-system keychain." : "Stored insecurely in application settings.") : QString());
    });
    connect(&controller_, &ApplicationController::credentialStorageFailed, this, [this](const QString& message, bool fallback){
        if (fallback && !destinationKey_->text().isEmpty() && QMessageBox::warning(this, "Secure storage unavailable",
            message + "\n\nSave the stream key in plain application settings instead? Other users with access to this account may be able to read it.",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
            controller_.saveDestinationCredential(destinationKey_->text(), true);
        else if (!fallback) QMessageBox::warning(this, "Credential storage", message);
    });
    connect(&controller_, &ApplicationController::srtPassphraseChanged, this, [this](const QString& passphrase, bool secure){
        if (srtPassphrase_) {
            srtPassphrase_->setText(passphrase);
            srtPassphrase_->setToolTip(secure ? "Stored in the operating-system keychain."
                                             : "Not stored securely; it may change after restart.");
        }
        if (fullUrl_) fullUrl_->setText(controller_.ingestFullUrl());
    });
    connect(&controller_, &ApplicationController::srtCredentialStorageFailed, this, [this](const QString& message, bool fallback){
        if (fallback && QMessageBox::warning(this, "SRT credential storage",
            message + "\n\nSave the SRT passphrase in plain application settings instead?",
            QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
            controller_.saveSrtPassphraseCredential(true);
        else if (!fallback) QMessageBox::warning(this, "SRT credential storage", message);
    });
    QTimer::singleShot(0, &controller_, &ApplicationController::loadDestinationCredential);
    QTimer::singleShot(0, this, [this]{ updateProfileDashboard(); });
}
QWidget* MainWindow::createStatus() {
    auto* box = new QGroupBox("Status"); auto* g = new QGridLayout(box);
    overall_=valueLabel(); ingestStatus_=valueLabel(); sourceStatus_=valueLabel(); destinationStatus_=valueLabel(); effective_=valueLabel(); requested_=valueLabel(); buffer_=valueLabel(); uptime_=valueLabel();
    const std::array<std::pair<QString,QLabel*>,8> fields{{{"Overall",overall_},{"Local ingest",ingestStatus_},{"OBS source",sourceStatus_},{"Destination",destinationStatus_},{"Effective delay",effective_},{"Requested delay",requested_},{"Buffer",buffer_},{"Uptime",uptime_}}};
    for (int i=0;i<static_cast<int>(fields.size());++i) { g->addWidget(new QLabel(fields[i].first), (i/4)*2, i%4); g->addWidget(fields[i].second,(i/4)*2+1,i%4); }
    return box;
}
QWidget* MainWindow::createDashboard() {
    auto* page=new QWidget; auto* layout=new QVBoxLayout(page);
    auto* heading=new QLabel("Stream profiles");
    heading->setStyleSheet("font-size:20px;font-weight:700;");
    auto* help=new QLabel("Each profile has an independent source, buffer, delay, encoder and destination. Profiles share the local RTMP/SRT gateway and can run simultaneously.");
    help->setWordWrap(true); help->setStyleSheet("color:#aeb6c2;");
    profileSummary_=new QLabel;
    profileSummary_->setStyleSheet("font-size:14px;font-weight:600;padding:7px;background:#20252d;border-radius:4px;");
    profileTable_=new QTableWidget(0,8);
    profileTable_->setHorizontalHeaderLabels({"Profile","Input","Source","Destination","Encoder","Delay","State","Buffer"});
    profileTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    profileTable_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
    profileTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    profileTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    profileTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    profileTable_->setAlternatingRowColors(true);
    profileTable_->setShowGrid(false);
    profileTable_->verticalHeader()->hide();
    connect(profileTable_,&QTableWidget::cellDoubleClicked,this,[this](int row,int){
        if(auto* item=profileTable_->item(row,0))controller_.selectProfile(item->data(Qt::UserRole).toString());
    });
    auto selectedId=[this]{
        const auto rows=profileTable_->selectionModel()->selectedRows();
        return rows.isEmpty()?controller_.selectedProfileId():profileTable_->item(rows.front().row(),0)->data(Qt::UserRole).toString();
    };
    auto* add=new QPushButton("New profile");
    connect(add,&QPushButton::clicked,this,[this]{
        bool ok=false; const auto name=QInputDialog::getText(this,"New stream profile","Profile name",QLineEdit::Normal,{},&ok);
        if(ok){const auto id=controller_.createProfile(name);controller_.selectProfile(id);}
    });
    auto* duplicate=new QPushButton("Duplicate");
    connect(duplicate,&QPushButton::clicked,this,[this,selectedId]{const auto id=controller_.duplicateProfile(selectedId());if(!id.isEmpty())controller_.selectProfile(id);});
    auto* rename=new QPushButton("Rename");
    connect(rename,&QPushButton::clicked,this,[this,selectedId]{
        const auto id=selectedId(); const auto& profiles=controller_.profiles();
        const auto profile=std::find_if(profiles.cbegin(),profiles.cend(),[&](const auto&p){return p.profileId==id;});
        if(profile==profiles.cend())return;bool ok=false;const auto name=QInputDialog::getText(this,"Rename profile","Profile name",QLineEdit::Normal,profile->profileName,&ok);if(ok)controller_.renameProfile(id,name);
    });
    auto* remove=new QPushButton("Remove");
    connect(remove,&QPushButton::clicked,this,[this,selectedId]{
        if(QMessageBox::question(this,"Remove profile","Remove this inactive profile and its saved credentials?")==QMessageBox::Yes&&!controller_.removeProfile(selectedId()))
            QMessageBox::warning(this,"Cannot remove","Stop the profile first. At least one profile must remain.");
    });
    auto* start=new QPushButton("Start selected");
    connect(start,&QPushButton::clicked,this,[this,selectedId]{controller_.startProfile(selectedId());});
    auto* stop=new QPushButton("Stop selected");
    connect(stop,&QPushButton::clicked,this,[this,selectedId]{controller_.stopProfile(selectedId());});
    auto* startAll=new QPushButton("Start all");
    connect(startAll,&QPushButton::clicked,&controller_,&ApplicationController::startAllProfiles);
    auto* stopAll=new QPushButton("Stop all");
    connect(stopAll,&QPushButton::clicked,&controller_,&ApplicationController::stopAllRelays);
    layout->addWidget(heading);layout->addWidget(help);layout->addWidget(profileSummary_);layout->addWidget(profileTable_,1);
    layout->addWidget(row({add,duplicate,rename,remove,start,stop,startAll,stopAll}));
    return page;
}
void MainWindow::updateProfileDashboard() {
    if(!profileTable_)return;
    const auto& profiles=controller_.profiles();
    profileTable_->setRowCount(profiles.size());
    int active=0,connectedSources=0;
    for(int rowIndex=0;rowIndex<profiles.size();++rowIndex){
        const auto& profile=profiles[rowIndex];
        const auto snapshot=sessionSnapshots_.value(profile.profileId);
        if(snapshot.destinationConnected||snapshot.state==ApplicationState::ConnectingDestination||snapshot.state==ApplicationState::ReconnectingDestination)++active;
        if(snapshot.sourceConnected)++connectedSources;
        const QString destination=profile.destinationUrl.isEmpty()?"Not configured":
            (profile.servicePreset=="Custom"?QUrl(profile.destinationUrl).host():profile.servicePreset);
        const QStringList values{
            profile.profileName,
            profile.inputProtocol.toUpper(),
            snapshot.sourceConnected?"Connected":"Waiting",
            destination,
            snapshot.activeEncoder.isEmpty() ? "—" : snapshot.activeEncoder,
            QString::number(profile.requestedDelaySeconds)+" s",
            toString(snapshot.state),
            QString::number(snapshot.bufferBytes/1048576.0,'f',1)+" MiB"
        };
        for(int column=0;column<values.size();++column){
            auto* item=profileTable_->item(rowIndex,column);
            if(!item){item=new QTableWidgetItem;profileTable_->setItem(rowIndex,column,item);}
            item->setText(values[column]);
            if(column==0)item->setData(Qt::UserRole,profile.profileId);
            if(column==0)item->setForeground(QColor(profile.profileColor));
        }
        if(profile.profileId==controller_.selectedProfileId())profileTable_->selectRow(rowIndex);
    }
    if(profileSummary_){
        const int estimated=QSettings().value("capacity/lastSafeStreams",-1).toInt();
        const QString capacity=estimated>=0?QString("%1 / %2 estimated safe").arg(active).arg(estimated)
                                           :QString("%1 active • capacity not benchmarked").arg(active);
        profileSummary_->setText(QString("%1  •  %2 source(s) connected  •  %3 configured profile(s)")
            .arg(capacity).arg(connectedSources).arg(profiles.size()));
        profileSummary_->setStyleSheet(estimated>=0&&active>=estimated&&estimated>0
            ?"font-size:14px;font-weight:600;padding:7px;background:#4a321d;color:#ffd08a;border-radius:4px;"
            :"font-size:14px;font-weight:600;padding:7px;background:#20252d;border-radius:4px;");
    }
}
QWidget* MainWindow::createIngest() {
    auto* box = new QGroupBox("Connect OBS to this relay"); auto* layout = new QVBoxLayout(box); const auto& c=controller_.config();
    inputProtocol_=new QComboBox;
    inputProtocol_->addItem("RTMP — same machine or reliable LAN", "rtmp");
    inputProtocol_->addItem("SRT — remote or unstable network", "srt");
    inputProtocol_->setCurrentIndex(std::max(0,inputProtocol_->findData(c.inputProtocol)));
    profileSelector_=new QComboBox;
    for(const auto& profile:controller_.profiles())profileSelector_->addItem(profile.profileName,profile.profileId);
    profileSelector_->setCurrentIndex(std::max(0,profileSelector_->findData(controller_.selectedProfileId())));
    connect(profileSelector_,&QComboBox::currentIndexChanged,this,[this](int index){controller_.selectProfile(profileSelector_->itemData(index).toString());});
    auto* protocolForm=new QFormLayout;
    protocolForm->addRow("Editing profile",profileSelector_);
    protocolForm->addRow("How OBS connects",inputProtocol_);
    layout->addLayout(protocolForm);

    serverUrl_=new QLineEdit(controller_.ingestServerUrl()); localKey_=new QLineEdit(c.localStreamKey); fullUrl_=new QLineEdit(controller_.ingestFullUrl());
    for(auto* field:{serverUrl_,localKey_,fullUrl_}) field->setReadOnly(true);
    localKey_->setEchoMode(QLineEdit::Password);

    rtmpConnectionDetails_=new QWidget; auto* rtmpForm=new QFormLayout(rtmpConnectionDetails_);
    rtmpForm->setContentsMargins(0,0,0,0);
    auto* showKey=new QCheckBox("Show key");
    connect(showKey,&QCheckBox::toggled,localKey_,[this](bool on){localKey_->setEchoMode(on?QLineEdit::Normal:QLineEdit::Password);});
    rtmpForm->addRow("OBS Server",row({serverUrl_,copyButton(serverUrl_)}));
    rtmpForm->addRow("OBS Stream Key",row({localKey_,copyButton(localKey_),showKey}));

    srtConnectionDetails_=new QWidget; auto* srtLayout=new QVBoxLayout(srtConnectionDetails_);
    srtLayout->setContentsMargins(0,0,0,0);
    auto* srtHeading=new QLabel("OBS Server URL — copy the entire value");
    srtHeading->setStyleSheet("font-size:15px;font-weight:600;");
    fullUrl_->setStyleSheet("font-family:monospace;font-size:13px;padding:7px;border:1px solid #3977d5;");
    fullUrl_->setMinimumHeight(36);
    auto* privateNote=new QLabel("Private connection URL — it includes the path key and encryption passphrase.");
    privateNote->setStyleSheet("color:#f4b942;");
    privateNote->setWordWrap(true);
    srtLayout->addWidget(srtHeading);
    srtLayout->addWidget(row({fullUrl_,copyButton(fullUrl_,"Copy SRT URL")}));
    srtLayout->addWidget(privateNote);

    layout->addWidget(rtmpConnectionDetails_);
    layout->addWidget(srtConnectionDetails_);
    connect(inputProtocol_,&QComboBox::currentIndexChanged,this,[this](int index){
        const auto protocol=inputProtocol_->itemData(index).toString();
        if(last_.sourceConnected && protocol!=controller_.config().inputProtocol
            && QMessageBox::question(this,"Change ingest protocol",
                "OBS is connected. Changing protocol restarts the ingest server and disconnects OBS. Continue?",
                QMessageBox::Yes|QMessageBox::No)!=QMessageBox::Yes) {
            QSignalBlocker blocker(inputProtocol_);
            inputProtocol_->setCurrentIndex(std::max(0,inputProtocol_->findData(controller_.config().inputProtocol)));
            return;
        }
        controller_.setInputProtocol(protocol);
    });
    ingestInstructions_=new QLabel; ingestInstructions_->setWordWrap(true);
    ingestInstructions_->setStyleSheet("padding:8px;background:#20252d;border-radius:4px;");
    layout->addWidget(ingestInstructions_);
    auto* restart=new QPushButton("Start / restart ingest server");
    connect(restart,&QPushButton::clicked,&controller_,&ApplicationController::restartIngest);
    layout->addWidget(restart);
    updateIngestModeUi(); return box;
}
QWidget* MainWindow::createIngestSettings() {
    auto* page=new QWidget; auto* pageLayout=new QVBoxLayout(page); const auto& c=controller_.config();

    auto* networkBox=new QGroupBox("Network"); auto* networkForm=new QFormLayout(networkBox);
    listenInterface_=new QLineEdit(c.listenAddress); listenInterface_->setReadOnly(true);
    advertisedHost_=new QLineEdit(c.advertisedHost);
    advertisedHost_->setPlaceholderText("Automatic LAN address, public IP, or DNS name");
    allowLan_=new QCheckBox("Accept OBS connections from other devices");
    allowLan_->setChecked(c.listenAddress=="0.0.0.0");
    networkForm->addRow("Listen interface",listenInterface_);
    networkForm->addRow(allowLan_);
    networkForm->addRow("Address shown to OBS",advertisedHost_);
    auto* hostHelp=new QLabel("Use this relay computer's LAN address, or its public DNS name for remote SRT. Never enter 0.0.0.0 here.");
    hostHelp->setWordWrap(true); hostHelp->setStyleSheet("color:#aeb6c2;");
    networkForm->addRow(hostHelp);

    auto* protocolBox=new QGroupBox("Protocol details"); auto* protocolForm=new QFormLayout(protocolBox);
    auto* rtmpPort=new QLineEdit(QString::number(c.rtmpPort)); rtmpPort->setReadOnly(true);
    auto* app=new QLineEdit(c.applicationName); app->setReadOnly(true);
    srtPort_=new QSpinBox; srtPort_->setRange(1,65535); srtPort_->setValue(c.srtPort);
    srtLatency_=new QSpinBox; srtLatency_->setRange(120,20000); srtLatency_->setSuffix(" ms"); srtLatency_->setValue(c.srtLatencyMs);
    protocolForm->addRow("Internal RTMP port",rtmpPort);
    protocolForm->addRow("Application path",app);
    protocolForm->addRow("SRT UDP port",srtPort_);
    protocolForm->addRow("SRT recovery latency",srtLatency_);
    auto* latencyHelp=new QLabel("Recovery latency handles network packet loss. It is separate from the viewer delay on the Delay tab.");
    latencyHelp->setWordWrap(true); latencyHelp->setStyleSheet("color:#aeb6c2;");
    protocolForm->addRow(latencyHelp);

    auto* securityBox=new QGroupBox("Ingest credentials"); auto* securityForm=new QFormLayout(securityBox);
    srtEncryption_=new QCheckBox("Require encrypted SRT contribution"); srtEncryption_->setChecked(c.srtEncryption);
    srtPassphrase_=new QLineEdit(controller_.srtPassphrase()); srtPassphrase_->setReadOnly(true); srtPassphrase_->setEchoMode(QLineEdit::Password);
    auto* showSecrets=new QCheckBox("Show");
    connect(showSecrets,&QCheckBox::toggled,this,[this](bool on){srtPassphrase_->setEchoMode(on?QLineEdit::Normal:QLineEdit::Password);});
    auto* generateKey=new QPushButton("Generate new RTMP path key");
    connect(generateKey,&QPushButton::clicked,this,[this]{
        if(last_.sourceConnected && QMessageBox::question(this,"Regenerate key","OBS is connected. Regenerating the key disconnects it. Continue?")!=QMessageBox::Yes)return;
        controller_.regenerateLocalKey();
    });
    auto* generateSrt=new QPushButton("Generate new SRT passphrase");
    connect(generateSrt,&QPushButton::clicked,this,[this]{
        if(last_.sourceConnected && QMessageBox::question(this,"Regenerate SRT passphrase","The source is connected. Regenerating the passphrase disconnects it. Continue?")!=QMessageBox::Yes)return;
        controller_.regenerateSrtPassphrase();
    });
    securityForm->addRow(srtEncryption_);
    securityForm->addRow("SRT passphrase",row({srtPassphrase_,copyButton(srtPassphrase_),showSecrets}));
    securityForm->addRow(row({generateKey,generateSrt}));

    connect(allowLan_,&QCheckBox::toggled,this,[this](bool enabled){
        if (enabled && QMessageBox::warning(this,"Allow network connections",
                "This exposes the selected ingest protocol to devices on your network. Continue?",
                QMessageBox::Yes|QMessageBox::No)!=QMessageBox::Yes) {
            QSignalBlocker blocker(allowLan_); allowLan_->setChecked(false); return;
        }
        controller_.setAllowLan(enabled);
    });
    connect(advertisedHost_,&QLineEdit::editingFinished,this,[this]{ controller_.setAdvertisedHost(advertisedHost_->text()); });
    auto applySrt=[this]{ controller_.setSrtSettings(srtPort_->value(),srtLatency_->value()); };
    connect(srtPort_,&QSpinBox::editingFinished,this,applySrt);
    connect(srtLatency_,&QSpinBox::editingFinished,this,applySrt);
    connect(srtEncryption_,&QCheckBox::toggled,&controller_,&ApplicationController::setSrtEncryption);

    pageLayout->addWidget(networkBox); pageLayout->addWidget(protocolBox); pageLayout->addWidget(securityBox); pageLayout->addStretch();
    updateIngestModeUi();
    return page;
}
void MainWindow::updateIngestModeUi() {
    if (!inputProtocol_) return;
    const bool srt=inputProtocol_->currentData().toString()=="srt";
    if(rtmpConnectionDetails_)rtmpConnectionDetails_->setVisible(!srt);
    if(srtConnectionDetails_)srtConnectionDetails_->setVisible(srt);
    if(srtPort_)srtPort_->setEnabled(srt); if(srtLatency_)srtLatency_->setEnabled(srt);
    if(srtEncryption_)srtEncryption_->setEnabled(srt); if(srtPassphrase_)srtPassphrase_->setEnabled(srt && srtEncryption_->isChecked());
    if(ingestInstructions_) {
        QString instructions=srt
            ? "<b>In OBS:</b> Settings → Stream → Service: Custom. Paste the complete URL above into <b>Server</b>, leave <b>Stream Key empty</b>, then start streaming."
            : "<b>In OBS:</b> Settings → Stream → Service: Custom. Copy the Server and Stream Key values above into their matching OBS fields.";
        if(controller_.config().listenAddress=="127.0.0.1")
            instructions += "<br><span style='color:#f4b942'><b>Local-only:</b> For OBS on another computer, open Ingest Settings and enable “Accept OBS connections from other devices”.</span>";
        ingestInstructions_->setText(instructions);
    }
}
QWidget* MainWindow::createDestination() {
    auto* box=new QGroupBox("Destination"); auto* form=new QFormLayout(box); servicePreset_=new QComboBox; servicePreset_->addItems({"Custom","Twitch","YouTube","Kick","Facebook"});
    servicePreset_->setCurrentText(controller_.config().servicePreset);
    connect(servicePreset_, &QComboBox::currentTextChanged, &controller_, &ApplicationController::setServicePreset);
    destinationUrl_=new QLineEdit(controller_.config().destinationUrl); destinationUrl_->setPlaceholderText("rtmps://live.example.com/app"); destinationKey_=new QLineEdit; destinationKey_->setEchoMode(QLineEdit::Password);
    auto* show=new QCheckBox("Show stream key"); connect(show,&QCheckBox::toggled,destinationKey_,[this](bool on){destinationKey_->setEchoMode(on?QLineEdit::Normal:QLineEdit::Password);}); rememberKey_=new QCheckBox("Remember using secure keychain");
    autoStartProfile_=new QCheckBox("Start this relay automatically when the application opens");
    autoStartProfile_->setChecked(controller_.config().autoStartRelay);
    autoStartProfile_->setToolTip("Requires a remembered destination stream key.");
    connect(autoStartProfile_,&QCheckBox::toggled,&controller_,&ApplicationController::setProfileAutoStart);
    auto saveDestination=[this]{controller_.configureDestination(destinationUrl_->text(),destinationKey_->text(),rememberKey_->isChecked());};
    connect(destinationUrl_,&QLineEdit::editingFinished,this,saveDestination);
    connect(destinationKey_,&QLineEdit::editingFinished,this,saveDestination);
    connect(rememberKey_,&QCheckBox::toggled,this,[saveDestination](bool){saveDestination();});
    auto* validate=new QPushButton("Validate URL"); connect(validate,&QPushButton::clicked,this,[this]{auto v=validateRtmpUrl(destinationUrl_->text()); QMessageBox::information(this,"URL validation",v.valid?"The RTMP URL is syntactically valid.":v.error);});
    startRelay_=new QPushButton("Start relay"); stopRelay_=new QPushButton("Stop relay"); stopRelay_->setEnabled(false);
    connect(startRelay_,&QPushButton::clicked,this,[this]{controller_.startRelay(destinationUrl_->text(),destinationKey_->text(),rememberKey_->isChecked());}); connect(stopRelay_,&QPushButton::clicked,&controller_,&ApplicationController::stopRelay);
    form->addRow("Service preset",servicePreset_); form->addRow("Server URL",row({destinationUrl_,validate,copyButton(destinationUrl_)})); form->addRow("Stream key",row({destinationKey_,show,rememberKey_})); form->addRow(autoStartProfile_); form->addRow(row({startRelay_,stopRelay_})); return box;
}
QWidget* MainWindow::createDelay() {
    auto* page=new QWidget; auto* v=new QVBoxLayout(page); auto* box=new QGroupBox("Dynamic stream delay"); auto* l=new QVBoxLayout(box);
    delayPreset_=new QComboBox;
    delayPreset_->addItem("Custom", -1);
    const std::array<std::pair<int,int>,6> presets{{{0,64},{30,64},{60,96},{120,160},{180,256},{300,384}}};
    for (const auto& [delay, memory] : presets)
        delayPreset_->addItem(delay == 0 ? "No delay — no idle buffering"
                                        : QString("%1 seconds — %2 MiB buffer").arg(delay).arg(memory),
                              delay);
    l->addWidget(new QLabel("Initial delay preset")); l->addWidget(delayPreset_);
    delaySeconds_=new QSpinBox; delaySeconds_->setRange(0,controller_.config().maximumDelaySeconds); delaySeconds_->setSuffix(" seconds"); delaySeconds_->setValue(controller_.config().requestedDelaySeconds);
    delaySlider_=new QSlider(Qt::Horizontal); delaySlider_->setRange(0,controller_.config().maximumDelaySeconds); delaySlider_->setValue(delaySeconds_->value());
    connect(delaySeconds_,&QSpinBox::valueChanged,delaySlider_,&QSlider::setValue); connect(delaySlider_,&QSlider::valueChanged,delaySeconds_,&QSpinBox::setValue);
    connect(delayPreset_,&QComboBox::currentIndexChanged,this,[this,presets](int index){
        if(index<=0)return;
        const auto [delay,memory]=presets.at(static_cast<std::size_t>(index-1));
        controller_.setBufferLimits(std::max(1,delay),memory);
        delaySeconds_->setValue(delay);
    });
    l->addWidget(new QLabel("Requested delay")); l->addWidget(delaySeconds_); l->addWidget(delaySlider_);
    auto* buttons=new QWidget; auto* h=new QHBoxLayout(buttons); h->setContentsMargins(0,0,0,0);
    for(int delta:{-30,-10,-5,-1,1,5,10,30}) { auto* b=new QPushButton(QString(delta>0?"+%1 s":"%1 s").arg(delta)); connect(b,&QPushButton::clicked,this,[this,delta]{delaySeconds_->setValue(delaySeconds_->value()+delta);}); h->addWidget(b); }
    l->addWidget(buttons); delayProgress_=new QLabel("No delay change pending."); l->addWidget(delayProgress_);
    delayOverlayText_=new QLineEdit(controller_.config().delayOverlayText); delayOverlayText_->setMaxLength(200);
    delayOverlayText_->setPlaceholderText("Optional message shown only while increasing delay");
    connect(delayOverlayText_, &QLineEdit::editingFinished, this, [this]{controller_.setDelayOverlayText(delayOverlayText_->text());});
    l->addWidget(new QLabel("Message displayed while increasing delay")); l->addWidget(delayOverlayText_);
    auto* apply=new QPushButton("Apply delay"); auto* cancel=new QPushButton("Cancel pending increase"); connect(apply,&QPushButton::clicked,this,[this]{
        if(delaySeconds_->value()*1000000LL<last_.effectiveDelayUs){ const auto skip=(last_.effectiveDelayUs-delaySeconds_->value()*1000000LL)/1000000.0; if(QMessageBox::warning(this,"Reduce delay",QString("Current delay: %1\nRequested delay: %2 s\nApproximately %3 seconds will be skipped.\n\nReducing delay skips buffered content and causes viewers to see a forward jump.").arg(seconds(last_.effectiveDelayUs)).arg(delaySeconds_->value()).arg(skip,0,'f',1),QMessageBox::Ok|QMessageBox::Cancel)!=QMessageBox::Ok)return; } controller_.applyDelay(delaySeconds_->value()); });
    connect(cancel,&QPushButton::clicked,this,[this]{if(!controller_.cancelDelayIncrease())QMessageBox::information(this,"Cannot cancel","Filler output has already started, so cancellation is no longer safe.");});
    l->addWidget(row({apply,cancel})); v->addWidget(box); v->addStretch(); return page;
}
QWidget* MainWindow::createOutput() {
    auto* page=new QWidget; auto* form=new QFormLayout(page); videoEncoder_=new QComboBox;
    const QMap<QString, QString> encoderLabels{{"auto", "Auto (hardware first)"}, {"h264_nvenc", "NVIDIA NVENC"},
        {"h264_qsv", "Intel Quick Sync"}, {"h264_amf", "AMD AMF"}, {"libx264", "Software H.264 (libx264)"}};
    for (const auto& name : controller_.availableVideoEncoders()) videoEncoder_->addItem(encoderLabels.value(name, name), name);
    videoEncoder_->setCurrentIndex(std::max(0, videoEncoder_->findData(controller_.config().videoEncoder)));
    connect(videoEncoder_, &QComboBox::currentIndexChanged, this, [this](int index){ controller_.setVideoEncoder(videoEncoder_->itemData(index).toString()); });
    fillerMode_=new QComboBox; fillerMode_->addItem("Hold last OBS frame", "hold"); fillerMode_->addItem("Custom standby image", "image"); fillerMode_->addItem("Black screen", "black");
    fillerMode_->setCurrentIndex(std::max(0, fillerMode_->findData(controller_.config().fillerMode)));
    connect(fillerMode_, &QComboBox::currentIndexChanged, this, [this](int index){ controller_.setFillerMode(fillerMode_->itemData(index).toString()); });
    standbyImage_=new QLineEdit(controller_.config().standbyImagePath); standbyImage_->setReadOnly(true); standbyImage_->setPlaceholderText("Optional PNG, JPEG, or WebP image");
    browseStandby_=new QPushButton("Browse…"); connect(browseStandby_, &QPushButton::clicked, this, [this]{
        const QString path=QFileDialog::getOpenFileName(this,"Select standby image",standbyImage_->text(),"Images (*.png *.jpg *.jpeg *.webp *.bmp)");
        if(!path.isEmpty()){standbyImage_->setText(path);controller_.setStandbyImage(path);}
    });
    resolution_=new QComboBox;
    resolution_->addItem("3840×2160 (4K)","3840x2160");resolution_->addItem("1920×1080","1920x1080");resolution_->addItem("1280×720","1280x720");
    resolution_->setCurrentIndex(std::max(0,resolution_->findData(QString("%1x%2").arg(controller_.config().width).arg(controller_.config().height))));
    auto spin=[&](int min,int max,int value,const QString& suffix){auto* s=new QSpinBox;s->setRange(min,max);s->setValue(value);s->setSuffix(suffix);return s;};
    outputFps_=spin(15,120,controller_.config().fps," FPS");
    videoBitrate_=spin(500,50000,controller_.config().videoBitrateKbps," Kbit/s");
    keyframeInterval_=spin(1,10,controller_.config().keyframeIntervalSeconds," s");
    audioBitrate_=spin(64,512,controller_.config().audioBitrateKbps," Kbit/s");
    maximumBufferDuration_=spin(1,3600,controller_.config().maximumDelaySeconds," s");
    maximumBufferMemory_=spin(64,4096,controller_.config().maximumBufferMiB," MiB");
    auto applyOutput=[this]{
        const auto parts=resolution_->currentData().toString().split('x');
        controller_.setOutputProfile(parts.value(0).toInt(),parts.value(1).toInt(),outputFps_->value(),
            videoBitrate_->value(),audioBitrate_->value(),keyframeInterval_->value());
    };
    connect(resolution_,&QComboBox::currentIndexChanged,this,[applyOutput](int){applyOutput();});
    connect(outputFps_,&QSpinBox::editingFinished,this,applyOutput);connect(videoBitrate_,&QSpinBox::editingFinished,this,applyOutput);
    connect(keyframeInterval_,&QSpinBox::editingFinished,this,applyOutput);connect(audioBitrate_,&QSpinBox::editingFinished,this,applyOutput);
    auto applyLimits=[this]{ controller_.setBufferLimits(maximumBufferDuration_->value(),maximumBufferMemory_->value()); };
    connect(maximumBufferDuration_,&QSpinBox::editingFinished,this,applyLimits);
    connect(maximumBufferMemory_,&QSpinBox::editingFinished,this,applyLimits);
    form->addRow("Video encoder",videoEncoder_); form->addRow("Filler video",fillerMode_); form->addRow("Standby image",row({standbyImage_,browseStandby_})); form->addRow("Resolution",resolution_); form->addRow("Frame rate",outputFps_); form->addRow("Video bitrate",videoBitrate_); form->addRow("Keyframe interval",keyframeInterval_); form->addRow("Audio bitrate",audioBitrate_); form->addRow("Audio sample rate",new QLabel("48 kHz stereo")); form->addRow("Maximum buffer duration",maximumBufferDuration_); form->addRow("Maximum buffer memory",maximumBufferMemory_); form->addRow(new QLabel("Output settings belong to the selected profile and are locked while that profile is live.")); return page;
}
QWidget* MainWindow::createCapacity() {
    auto* page=new QWidget;auto* layout=new QVBoxLayout(page);
    auto* heading=new QLabel("Concurrent stream capacity");
    heading->setStyleSheet("font-size:20px;font-weight:700;");
    auto* explanation=new QLabel("Benchmark the workload you normally expect. The result tests the selected FFmpeg encoder, concurrent encoder contexts, measured throughput and currently available system memory. It is a conservative estimate, not a hard restriction.");
    explanation->setWordWrap(true);explanation->setStyleSheet("color:#aeb6c2;");
    auto* workload=new QGroupBox("Expected workload per stream");auto* form=new QFormLayout(workload);
    QSettings settings;
    capacityEncoder_=new QComboBox;
    const QMap<QString,QString> labels{{"auto","Auto (hardware first)"},{"h264_nvenc","NVIDIA NVENC"},{"h264_qsv","Intel Quick Sync"},{"h264_amf","AMD AMF"},{"libx264","Software H.264"}};
    for(const auto& encoder:controller_.availableVideoEncoders())capacityEncoder_->addItem(labels.value(encoder,encoder),encoder);
    capacityEncoder_->setCurrentIndex(std::max(0,capacityEncoder_->findData(settings.value("capacity/encoder","auto").toString())));
    auto spin=[](int minimum,int maximum,int value,const QString& suffix){auto*s=new QSpinBox;s->setRange(minimum,maximum);s->setValue(value);s->setSuffix(suffix);return s;};
    capacityWidth_=spin(320,3840,settings.value("capacity/width",1920).toInt()," px");
    capacityHeight_=spin(180,2160,settings.value("capacity/height",1080).toInt()," px");
    capacityFps_=spin(15,120,settings.value("capacity/fps",60).toInt()," FPS");
    capacityBitrate_=spin(500,50000,settings.value("capacity/videoKbps",6000).toInt()," Kbit/s");
    capacityBuffer_=spin(64,4096,settings.value("capacity/bufferMiB",384).toInt()," MiB");
    capacityCandidates_=spin(1,16,settings.value("capacity/candidates",8).toInt()," streams");
    capacitySeconds_=spin(1,10,settings.value("capacity/seconds",2).toInt()," s");
    capacitySafety_=spin(40,90,settings.value("capacity/safety",65).toInt()," %");
    form->addRow("Video encoder",capacityEncoder_);form->addRow("Width",capacityWidth_);form->addRow("Height",capacityHeight_);
    form->addRow("Frame rate",capacityFps_);form->addRow("Video bitrate",capacityBitrate_);
    form->addRow("Buffer allocation",capacityBuffer_);form->addRow("Maximum streams to test",capacityCandidates_);
    form->addRow("Benchmark duration",capacitySeconds_);form->addRow("Usable measured capacity",capacitySafety_);
    auto* note=new QLabel("For your normal use, benchmark 1920×1080 at the actual FPS and encoder you will select in each profile. Run NVENC and Software H.264 separately so fallback capacity is visible.");
    note->setWordWrap(true);note->setStyleSheet("color:#f4b942;");form->addRow(note);
    runCapacityBenchmark_=new QPushButton("Run encoder benchmark");
    const int previousSafe=settings.value("capacity/lastSafeStreams",-1).toInt();
    capacityResult_=new QLabel(previousSafe>=0
        ?QString("Last estimated safe capacity: %1 concurrent relay(s). Run again after changing hardware, drivers, encoder, or workload.").arg(previousSafe)
        :"No benchmark has been run for this workload.");
    capacityResult_->setWordWrap(true);capacityResult_->setStyleSheet("padding:12px;background:#20252d;border-radius:5px;font-size:14px;");
    connect(runCapacityBenchmark_,&QPushButton::clicked,this,[this]{
        int active=0;
        for(const auto& snapshot:sessionSnapshots_)
            if(snapshot.destinationConnected||snapshot.state==ApplicationState::ConnectingDestination||snapshot.state==ApplicationState::ReconnectingDestination)++active;
        if(active>0&&QMessageBox::warning(this,"Benchmark while live",
            "The benchmark intentionally loads the selected encoder and could affect active streams. Run it now anyway?",
            QMessageBox::Yes|QMessageBox::No)!=QMessageBox::Yes)return;
        QSettings settings;
        CapacitySettings workload;
        workload.encoder=capacityEncoder_->currentData().toString();workload.width=capacityWidth_->value();workload.height=capacityHeight_->value();
        workload.fps=capacityFps_->value();workload.videoBitrateKbps=capacityBitrate_->value();workload.bufferMiB=capacityBuffer_->value();
        workload.maximumCandidates=capacityCandidates_->value();workload.benchmarkSeconds=capacitySeconds_->value();workload.safetyPercent=capacitySafety_->value();
        settings.setValue("capacity/encoder",workload.encoder);settings.setValue("capacity/width",workload.width);settings.setValue("capacity/height",workload.height);
        settings.setValue("capacity/fps",workload.fps);settings.setValue("capacity/videoKbps",workload.videoBitrateKbps);
        settings.setValue("capacity/bufferMiB",workload.bufferMiB);settings.setValue("capacity/candidates",workload.maximumCandidates);
        settings.setValue("capacity/seconds",workload.benchmarkSeconds);settings.setValue("capacity/safety",workload.safetyPercent);
        runCapacityBenchmark_->setEnabled(false);capacityResult_->setText("Starting benchmark…");
        capacityEstimator_.start(workload);
    });
    connect(&capacityEstimator_,&CapacityEstimator::progress,this,[this](const QString& message){capacityResult_->setText(message);});
    connect(&capacityEstimator_,&CapacityEstimator::completed,this,[this](const CapacityResult& result){
        runCapacityBenchmark_->setEnabled(true);
        QSettings settings;settings.setValue("capacity/lastSafeStreams",result.safeStreams);
        settings.setValue("capacity/lastEncoder",result.actualEncoder);settings.setValue("capacity/lastMeasuredFps",result.measuredFps);
        int active=0;for(const auto& snapshot:sessionSnapshots_)if(snapshot.destinationConnected||snapshot.state==ApplicationState::ConnectingDestination||snapshot.state==ApplicationState::ReconnectingDestination)++active;
        capacityResult_->setText(QString("<b>Estimated safe concurrent relays: %1</b><br>Currently active: %2<br>Encoder actually tested: %3<br>Measured throughput: %4 FPS<br>Available RAM: %5 MiB<br><br>%6")
            .arg(result.safeStreams).arg(active).arg(result.actualEncoder.isEmpty()?"Unavailable":result.actualEncoder)
            .arg(result.measuredFps,0,'f',1).arg(result.availableMemoryMiB).arg(result.detail));
        updateProfileDashboard();
    });
    layout->addWidget(heading);layout->addWidget(explanation);layout->addWidget(workload);layout->addWidget(runCapacityBenchmark_);layout->addWidget(capacityResult_);layout->addStretch();
    return page;
}
QWidget* MainWindow::createMetrics() {
    auto* page=new QWidget; auto* l=new QVBoxLayout(page); metricsTable_=new QTableWidget(0,2); metricsTable_->setHorizontalHeaderLabels({"Metric","Value"}); metricsTable_->horizontalHeader()->setStretchLastSection(true); metricsTable_->verticalHeader()->hide(); l->addWidget(metricsTable_); return page;
}
QWidget* MainWindow::createLogs() {
    auto* page=new QWidget; auto* l=new QVBoxLayout(page); severityFilter_=new QComboBox; severityFilter_->addItems({"All severities","Debug","Info","Warning","Error"}); auto* clear=new QPushButton("Clear visible logs"); auto* folder=new QPushButton("Open log folder"); connect(clear,&QPushButton::clicked,this,[this]{logsTable_->setRowCount(0);}); connect(folder,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(controller_.logger()->logDirectory()));}); l->addWidget(row({severityFilter_,clear,folder}));
    logsTable_=new QTableWidget(0,4); logsTable_->setHorizontalHeaderLabels({"Timestamp","Severity","Component","Message"}); logsTable_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch); logsTable_->verticalHeader()->hide(); l->addWidget(logsTable_); return page;
}
void MainWindow::updateSnapshot(AppSnapshot s) {
    last_=s; overall_->setText(toString(s.state)); ingestStatus_->setText(s.ingestRunning?"Connected":"Stopped"); sourceStatus_->setText(s.sourceConnected?"Connected":"Waiting");
    const bool relayActive=s.destinationConnected||s.state==ApplicationState::ConnectingDestination||s.state==ApplicationState::ReconnectingDestination;
    destinationStatus_->setText(s.destinationConnected?"Connected":(s.state==ApplicationState::ConnectingDestination?"Connecting":(s.state==ApplicationState::ReconnectingDestination?"Reconnecting":"Stopped"))); effective_->setText(seconds(s.effectiveDelayUs)); requested_->setText(seconds(s.requestedDelayUs)); buffer_->setText(seconds(s.bufferDurationUs)); uptime_->setText(QString("%1:%2:%3").arg(s.uptimeSeconds/3600,2,10,QChar('0')).arg((s.uptimeSeconds/60)%60,2,10,QChar('0')).arg(s.uptimeSeconds%60,2,10,QChar('0')));
    delayProgress_->setText(s.remainingFillerUs>0?QString("Inserting %1 of standby video and silence. Remaining: %2").arg(seconds(s.requestedDelayUs-s.effectiveDelayUs),seconds(s.remainingFillerUs)):"No delay change pending."); startRelay_->setEnabled(!relayActive); stopRelay_->setEnabled(relayActive);
    if (videoEncoder_) videoEncoder_->setEnabled(!s.destinationConnected && s.state != ApplicationState::ConnectingDestination && s.state != ApplicationState::ReconnectingDestination);
    const bool outputEditable=!s.destinationConnected && s.state != ApplicationState::ConnectingDestination && s.state != ApplicationState::ReconnectingDestination;
    if(resolution_)resolution_->setEnabled(outputEditable);if(outputFps_)outputFps_->setEnabled(outputEditable);
    if(videoBitrate_)videoBitrate_->setEnabled(outputEditable);if(keyframeInterval_)keyframeInterval_->setEnabled(outputEditable);
    if(audioBitrate_)audioBitrate_->setEnabled(outputEditable);
    if(fillerMode_) fillerMode_->setEnabled(outputEditable); if(browseStandby_) browseStandby_->setEnabled(outputEditable);
    if(maximumBufferDuration_) maximumBufferDuration_->setEnabled(outputEditable);
    if(maximumBufferMemory_) maximumBufferMemory_->setEnabled(outputEditable);
    if(delayPreset_) delayPreset_->setEnabled(outputEditable);
}
void MainWindow::updateMetrics(StreamStatistics s) {
    const QList<QPair<QString,QString>> values{{"Incoming video bitrate",QString::number(s.incomingVideoKbps,'f',1)+" Kbit/s"},{"Incoming audio bitrate",QString::number(s.incomingAudioKbps,'f',1)+" Kbit/s"},{"Outgoing bitrate",QString::number(s.outgoingKbps,'f',1)+" Kbit/s"},{"Incoming packets",QString::number(s.incomingPackets)},{"Outgoing packets",QString::number(s.outgoingPackets)},{"Dropped packets",QString::number(s.droppedPackets)},{"Decode errors",QString::number(s.decodeErrors)},{"Encode errors",QString::number(s.encodeErrors)},{"Buffer memory",QString::number(s.bufferBytes/1048576.0,'f',1)+" MiB"},{"Buffer duration",seconds(s.bufferDurationUs)},{"OBS reconnects",QString::number(s.obsReconnects)},{"Destination reconnects",QString::number(s.destinationReconnects)}};
    metricsTable_->setRowCount(values.size()); for(int i=0;i<values.size();++i){metricsTable_->setItem(i,0,new QTableWidgetItem(values[i].first));metricsTable_->setItem(i,1,new QTableWidgetItem(values[i].second));}
}
void MainWindow::appendLog(QString t,int severity,QString component,QString message) { if(severityFilter_&&severityFilter_->currentIndex()>0&&severityFilter_->currentIndex()-1!=severity)return; const int r=logsTable_->rowCount();logsTable_->insertRow(r);const QString names[]={"Debug","Info","Warning","Error"};for(int c=0;c<4;++c)logsTable_->setItem(r,c,new QTableWidgetItem(c==0?t:c==1?names[severity]:c==2?component:message));logsTable_->scrollToBottom(); }
void MainWindow::closeEvent(QCloseEvent* e) {
    int active=0;
    for(const auto& snapshot:sessionSnapshots_)
        if(snapshot.destinationConnected||snapshot.state==ApplicationState::ConnectingDestination||snapshot.state==ApplicationState::ReconnectingDestination)++active;
    if(active>0&&QMessageBox::question(this,"Stop relays and exit?",
        QString("%1 relay(s) are active. Stop all of them and exit?").arg(active))!=QMessageBox::Yes){
        e->ignore();return;
    }
    controller_.stopAll();e->accept();
}
}
