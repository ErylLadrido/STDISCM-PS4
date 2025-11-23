#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QLabel>
#include <QGridLayout>
#include <QLineEdit>
#include <QMap>
#include <QFrame>
#include "OCRClient.h"

class ImageResultWidget : public QFrame {
    Q_OBJECT

public:
    explicit ImageResultWidget(const QString& filename, QWidget* parent = nullptr);
    void setResult(const QString& text, bool success, const QString& errorMessage);
    void setPending();

private:
    QLabel* m_filenameLabel;
    QLabel* m_statusLabel;
    QLabel* m_textLabel;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onUploadClicked();
    void onConnectClicked();
    void onResultReceived(QString imageId, QString extractedText, bool success, QString errorMessage);
    void onConnectionStatusChanged(bool connected);
    void onConnectionError(QString errorMessage);

private:
    void setupUI();
    void updateProgressBar();
    void clearResults();
    void addImageToGrid(const QString& imageId, const QString& filename);

    // UI Components
    QLineEdit* m_serverAddressInput;
    QPushButton* m_connectButton;
    QPushButton* m_uploadButton;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    QGridLayout* m_resultsGrid;
    QWidget* m_resultsContainer;

    // OCR Client
    std::unique_ptr<OCRClient> m_ocrClient;

    // Tracking
    QMap<QString, ImageResultWidget*> m_imageWidgets;
    int m_totalImages;
    int m_completedImages;
    bool m_batchInProgress;

    // Layout
    QWidget* m_centralWidget;
};

#endif // MAINWINDOW_H