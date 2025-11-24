#ifndef OCRSERVICE_H
#define OCRSERVICE_H

#include "ocr_service.grpc.pb.h"
#include "OCRProcessor.h"
#include "ThreadPool.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>

class OCRServiceImpl final : public ocr::OCRService::Service {
public:
    OCRServiceImpl(size_t numThreads = 4);
    ~OCRServiceImpl();
    
    grpc::Status ProcessImages(
        grpc::ServerContext* context,
        grpc::ServerReaderWriter<ocr::OCRResult, ocr::ImageRequest>* stream
    ) override;

private:
    void memoryCleanupTask();
    
    ThreadPool m_threadPool;
    std::atomic<int> m_nextProcessorIndex;
    std::vector<std::unique_ptr<OCRProcessor>> m_processors;
    std::mutex m_streamMutex;
    std::thread m_cleanupThread;
    std::atomic<bool> m_cleanupRunning;
};

#endif // OCRSERVICE_H