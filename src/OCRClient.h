#ifndef OCRCLIENT_H
#define OCRCLIENT_H

#include <grpcpp/grpcpp.h>
#include "ocr_service.grpc.pb.h"
#include <QObject>
#include <QString>
#include <QByteArray>
#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>

class OCRClient : public QObject {
    Q_OBJECT

public:
    explicit OCRClient(const QString& serverAddress, QObject* parent = nullptr);
    ~OCRClient();

    // Send an image for OCR processing
    void sendImage(const QString& imageId, const QString& filename, const QByteArray& imageData);

    // Start/stop the client
    void start();
    void stop();

    bool isConnected() const { return m_connected; }

signals:
    void resultReceived(QString imageId, QString extractedText, bool success, QString errorMessage);
    void connectionStatusChanged(bool connected);
    void connectionError(QString errorMessage);

private:
    void processResults();
    void processSendQueue();

    std::unique_ptr<ocr::OCRService::Stub> m_stub;
    std::shared_ptr<grpc::Channel> m_channel;

    std::unique_ptr<grpc::ClientContext> m_context;
    std::unique_ptr<grpc::ClientReaderWriter<ocr::ImageRequest, ocr::OCRResult>> m_stream;

    std::thread m_readerThread;
    std::thread m_writerThread;
    std::atomic<bool> m_running;
    std::atomic<bool> m_connected;

    // Queue for images to send
    std::queue<ocr::ImageRequest> m_sendQueue;
    std::mutex m_queueMutex;

    QString m_serverAddress;
};

#endif // OCRCLIENT_H