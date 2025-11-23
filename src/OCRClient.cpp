#include "OCRClient.h"
#include <QDebug>

OCRClient::OCRClient(const QString& serverAddress, QObject* parent)
    : QObject(parent)
    , m_running(false)
    , m_connected(false)
    , m_serverAddress(serverAddress)
{
}

OCRClient::~OCRClient() {
    stop();
}

void OCRClient::start() {
    if (m_running) {
        return;
    }

    try {
        // Create channel
        m_channel = grpc::CreateChannel(
            m_serverAddress.toStdString(),
            grpc::InsecureChannelCredentials()
        );

        // Create stub
        m_stub = ocr::OCRService::NewStub(m_channel);

        // Create context and stream
        m_context = std::make_unique<grpc::ClientContext>();
        m_stream = m_stub->ProcessImages(m_context.get());

        if (!m_stream) {
            emit connectionError("Failed to create stream");
            return;
        }

        m_running = true;
        m_connected = true;
        emit connectionStatusChanged(true);

        // Start reader thread
        m_readerThread = std::thread(&OCRClient::processResults, this);

        qDebug() << "OCR Client started and connected to" << m_serverAddress;

    }
    catch (const std::exception& e) {
        qDebug() << "Error starting client:" << e.what();
        m_connected = false;
        emit connectionStatusChanged(false);
        emit connectionError(QString("Connection error: %1").arg(e.what()));
    }
}

void OCRClient::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;

    try {
        // Close the write side of the stream
        if (m_stream) {
            m_stream->WritesDone();
        }

        // Wait for reader thread to finish
        if (m_readerThread.joinable()) {
            m_readerThread.join();
        }

        // Finish the stream
        if (m_stream) {
            grpc::Status status = m_stream->Finish();
            if (!status.ok()) {
                qDebug() << "Stream finish error:" << status.error_message().c_str();
            }
        }

        m_stream.reset();
        m_context.reset();
        m_stub.reset();
        m_channel.reset();

    }
    catch (const std::exception& e) {
        qDebug() << "Error stopping client:" << e.what();
    }

    m_connected = false;
    emit connectionStatusChanged(false);
    qDebug() << "OCR Client stopped";
}

void OCRClient::sendImage(const QString& imageId, const QString& filename, const QByteArray& imageData) {
    if (!m_running || !m_stream) {
        qDebug() << "Cannot send image: client not running or stream not available";
        return;
    }

    try {
        ocr::ImageRequest request;
        request.set_image_id(imageId.toStdString());
        request.set_filename(filename.toStdString());
        request.set_image_data(imageData.constData(), imageData.size());

        bool success = m_stream->Write(request);
        if (!success) {
            qDebug() << "Failed to write image to stream:" << imageId;
            m_connected = false;
            emit connectionStatusChanged(false);
            emit connectionError("Lost connection to server");
        }
        else {
            qDebug() << "Sent image:" << imageId;
        }

    }
    catch (const std::exception& e) {
        qDebug() << "Error sending image:" << e.what();
        m_connected = false;
        emit connectionStatusChanged(false);
        emit connectionError(QString("Send error: %1").arg(e.what()));
    }
}

void OCRClient::processResults() {
    ocr::OCRResult result;

    try {
        while (m_running && m_stream->Read(&result)) {
            QString imageId = QString::fromStdString(result.image_id());
            QString text = QString::fromStdString(result.extracted_text());
            bool success = result.success();
            QString errorMsg = QString::fromStdString(result.error_message());

            qDebug() << "Received result for:" << imageId;

            emit resultReceived(imageId, text, success, errorMsg);
        }
    }
    catch (const std::exception& e) {
        qDebug() << "Error reading results:" << e.what();
    }

    qDebug() << "Result processing thread ended";

    if (m_running) {
        m_connected = false;
        emit connectionStatusChanged(false);
        emit connectionError("Connection lost while reading results");
    }
}