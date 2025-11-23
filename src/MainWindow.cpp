#include "MainWindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QUuid>
#include <QFile>
#include <QDebug>

// ===== ImageResultWidget Implementation =====

ImageResultWidget::ImageResultWidget(const QString& filename, QWidget* parent)
    : QFrame(parent)
{
    setFrameStyle(QFrame::Box | QFrame::Raised);
    setLineWidth(2);
    setMinimumSize(250, 150);
    setMaximumWidth(350);

    auto* layout = new QVBoxLayout(this);

    m_filenameLabel = new QLabel(filename, this);
    m_filenameLabel->setWordWrap(true);
    m_filenameLabel->setStyleSheet("font-weight: bold; font-size: 10pt;");

    m_statusLabel = new QLabel("Processing...", this);
    m_statusLabel->setStyleSheet("color: orange; font-style: italic;");

    m_textLabel = new QLabel("", this);
    m_textLabel->setWordWrap(true);
    m_textLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_textLabel->setMinimumHeight(80);
    m_textLabel->setMaximumHeight(200);
    m_textLabel->setStyleSheet("background-color: #000000; padding: 5px; border: 1px solid #ccc;");

    layout->addWidget(m_filenameLabel);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_textLabel);
    layout->addStretch();
}

void ImageResultWidget::setResult(const QString& text, bool success, const QString& errorMessage) {
    if (success) {
        m_statusLabel->setText("✓ Completed");
        m_statusLabel->setStyleSheet("color: green; font-weight: bold;");
        m_textLabel->setText(text.isEmpty() ? "(No text detected)" : text);
    }
    else {
        m_statusLabel->setText("✗ Failed");
        m_statusLabel->setStyleSheet("color: red; font-weight: bold;");
        m_textLabel->setText(errorMessage.isEmpty() ? "Unknown error" : errorMessage);
        m_textLabel->setStyleSheet("background-color: #ffe0e0; padding: 5px; border: 1px solid #cc0000; color: red;");
    }
}

void ImageResultWidget::setPending() {
    m_statusLabel->setText("Processing...");
    m_statusLabel->setStyleSheet("color: orange; font-style: italic;");
}

// ===== MainWindow Implementation =====

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_totalImages(0)
    , m_completedImages(0)
    , m_batchInProgress(false)
{
    setupUI();
    setWindowTitle("Distributed OCR Client");
    resize(1000, 700);
}

MainWindow::~MainWindow() {
    if (m_ocrClient) {
        m_ocrClient->stop();
    }
}

void MainWindow::setupUI() {
    m_centralWidget = new QWidget(this);
    setCentralWidget(m_centralWidget);

    auto* mainLayout = new QVBoxLayout(m_centralWidget);

    // Connection area
    auto* connectionLayout = new QHBoxLayout();
    auto* serverLabel = new QLabel("Server Address:", this);
    m_serverAddressInput = new QLineEdit("localhost:50051", this);
    m_serverAddressInput->setMinimumWidth(200);

    m_connectButton = new QPushButton("Connect", this);
    m_connectButton->setMaximumWidth(120);

    connectionLayout->addWidget(serverLabel);
    connectionLayout->addWidget(m_serverAddressInput);
    connectionLayout->addWidget(m_connectButton);
    connectionLayout->addStretch();

    // Upload and progress area
    auto* controlLayout = new QHBoxLayout();
    m_uploadButton = new QPushButton("Upload Images", this);
    m_uploadButton->setEnabled(false);
    m_uploadButton->setMinimumHeight(40);
    m_uploadButton->setStyleSheet("font-size: 12pt; font-weight: bold;");

    m_progressBar = new QProgressBar(this);
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setMinimumHeight(40);

    controlLayout->addWidget(m_uploadButton, 1);
    controlLayout->addWidget(m_progressBar, 2);

    // Status label
    m_statusLabel = new QLabel("Not connected", this);
    m_statusLabel->setStyleSheet("color: red; font-weight: bold; padding: 5px;");
    m_statusLabel->setAlignment(Qt::AlignCenter);

    // Results area with scroll
    auto* resultsLabel = new QLabel("OCR Results:", this);
    resultsLabel->setStyleSheet("font-size: 11pt; font-weight: bold;");

    m_resultsContainer = new QWidget();
    m_resultsGrid = new QGridLayout(m_resultsContainer);
    m_resultsGrid->setSpacing(10);
    m_resultsGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidget(m_resultsContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumHeight(400);

    // Add all to main layout
    mainLayout->addLayout(connectionLayout);
    mainLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(controlLayout);
    mainLayout->addWidget(resultsLabel);
    mainLayout->addWidget(scrollArea, 1);

    // Connect signals
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_uploadButton, &QPushButton::clicked, this, &MainWindow::onUploadClicked);
}

void MainWindow::onConnectClicked() {
    if (m_ocrClient && m_ocrClient->isConnected()) {
        // Disconnect
        m_ocrClient->stop();
        m_ocrClient.reset();
        m_connectButton->setText("Connect");
        m_uploadButton->setEnabled(false);
        m_statusLabel->setText("Not connected");
        m_statusLabel->setStyleSheet("color: red; font-weight: bold; padding: 5px;");
    }
    else {
        // Connect
        QString serverAddr = m_serverAddressInput->text().trimmed();
        if (serverAddr.isEmpty()) {
            QMessageBox::warning(this, "Error", "Please enter a server address");
            return;
        }

        m_ocrClient = std::make_unique<OCRClient>(serverAddr, this);

        connect(m_ocrClient.get(), &OCRClient::resultReceived,
            this, &MainWindow::onResultReceived);
        connect(m_ocrClient.get(), &OCRClient::connectionStatusChanged,
            this, &MainWindow::onConnectionStatusChanged);
        connect(m_ocrClient.get(), &OCRClient::connectionError,
            this, &MainWindow::onConnectionError);

        m_ocrClient->start();
    }
}

void MainWindow::onUploadClicked() {
    if (!m_ocrClient || !m_ocrClient->isConnected()) {
        QMessageBox::warning(this, "Error", "Not connected to server");
        return;
    }

    // If batch was completed (100%), start a new batch
    if (m_completedImages == m_totalImages && m_totalImages > 0) {
        clearResults();
    }

    QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        "Select Images",
        "",
        "Images (*.png *.jpg *.jpeg *.bmp *.tiff *.gif)"
    );

    if (filePaths.isEmpty()) {
        return;
    }

    m_batchInProgress = true;

    for (const QString& filePath : filePaths) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qDebug() << "Failed to open file:" << filePath;
            continue;
        }

        QByteArray imageData = file.readAll();
        file.close();

        QString imageId = QUuid::createUuid().toString();
        QString filename = QFileInfo(filePath).fileName();

        // Add to UI grid
        addImageToGrid(imageId, filename);

        // Send to server
        m_ocrClient->sendImage(imageId, filename, imageData);

        m_totalImages++;
    }

    updateProgressBar();
}

void MainWindow::onResultReceived(QString imageId, QString extractedText, bool success, QString errorMessage) {
    auto it = m_imageWidgets.find(imageId);
    if (it != m_imageWidgets.end()) {
        it.value()->setResult(extractedText, success, errorMessage);
        m_completedImages++;
        updateProgressBar();
    }
}

void MainWindow::onConnectionStatusChanged(bool connected) {
    if (connected) {
        m_statusLabel->setText("✓ Connected to server");
        m_statusLabel->setStyleSheet("color: green; font-weight: bold; padding: 5px;");
        m_connectButton->setText("Disconnect");
        m_uploadButton->setEnabled(true);
        m_serverAddressInput->setEnabled(false);
    }
    else {
        m_statusLabel->setText("✗ Disconnected from server");
        m_statusLabel->setStyleSheet("color: red; font-weight: bold; padding: 5px;");
        m_connectButton->setText("Connect");
        m_uploadButton->setEnabled(false);
        m_serverAddressInput->setEnabled(true);
    }
}

void MainWindow::onConnectionError(QString errorMessage) {
    QMessageBox::critical(this, "Connection Error", errorMessage);
}

void MainWindow::updateProgressBar() {
    if (m_totalImages == 0) {
        m_progressBar->setValue(0);
        m_progressBar->setFormat("0 / 0 images (0%)");
    }
    else {
        int percentage = (m_completedImages * 100) / m_totalImages;
        m_progressBar->setValue(percentage);
        m_progressBar->setFormat(QString("%1 / %2 images (%3%)")
            .arg(m_completedImages)
            .arg(m_totalImages)
            .arg(percentage));
    }
}

void MainWindow::clearResults() {
    // Remove all widgets from grid
    for (auto* widget : m_imageWidgets) {
        m_resultsGrid->removeWidget(widget);
        widget->deleteLater();
    }

    m_imageWidgets.clear();
    m_totalImages = 0;
    m_completedImages = 0;
    m_batchInProgress = false;
    updateProgressBar();
}

void MainWindow::addImageToGrid(const QString& imageId, const QString& filename) {
    auto* resultWidget = new ImageResultWidget(filename, this);
    resultWidget->setPending();

    int row = m_imageWidgets.size() / 3;
    int col = m_imageWidgets.size() % 3;

    m_resultsGrid->addWidget(resultWidget, row, col);
    m_imageWidgets[imageId] = resultWidget;
}