#include "OCRService.h"
#include <iostream>
#include <mutex>
#include <atomic>

// Global memory monitoring
std::atomic<size_t> g_activeImageSize{0};
const size_t MAX_MEMORY_USAGE = 500 * 1024 * 1024; // 500MB limit

OCRServiceImpl::OCRServiceImpl(size_t numThreads) 
    : m_threadPool(numThreads)
    , m_nextProcessorIndex(0)
    , m_cleanupRunning(true)
{
    for (size_t i = 0; i < numThreads; ++i) {
        auto processor = std::make_unique<OCRProcessor>();
        if (processor->initialize()) {
            m_processors.push_back(std::move(processor));
        }
    }
    
    if (m_processors.empty()) {
        throw std::runtime_error("Failed to initialize any OCR processors");
    }
    
    // Start memory cleanup thread
    m_cleanupThread = std::thread(&OCRServiceImpl::memoryCleanupTask, this);
    
    std::cout << "OCR Service initialized with " << m_processors.size() << " processors" << std::endl;
}

OCRServiceImpl::~OCRServiceImpl() {
    m_cleanupRunning = false;
    if (m_cleanupThread.joinable()) {
        m_cleanupThread.join();
    }
    
    m_threadPool.waitAll();
}

void OCRServiceImpl::memoryCleanupTask() {
    while (m_cleanupRunning) {
        std::this_thread::sleep_for(std::chrono::seconds(30)); // Cleanup every 30 seconds
        
        // Force cleanup by recreating processors periodically
        std::cout << "Performing memory cleanup..." << std::endl;
        
        for (auto& processor : m_processors) {
            // Recreate processor to clear Tesseract memory
            processor = std::make_unique<OCRProcessor>();
            processor->initialize();
        }
        
        std::cout << "Memory cleanup completed" << std::endl;
    }
}

grpc::Status OCRServiceImpl::ProcessImages(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<ocr::OCRResult, ocr::ImageRequest>* stream) {
    
    std::cout << "Client connected" << std::endl;
    
    // Add semaphore to limit concurrent processing
    std::atomic<int> activeTasks{0};
    const int MAX_CONCURRENT_TASKS = 4; // Reduced from 8 to 4
    
    ocr::ImageRequest request;
    while (stream->Read(&request)) {
        std::string imageId = request.image_id();
        std::string filename = request.filename();
        std::string imageData = request.image_data();
        
        // Memory usage monitoring
        size_t currentMemory = g_activeImageSize.load() + imageData.size();
        if (currentMemory > MAX_MEMORY_USAGE) {
            std::cerr << "Memory limit exceeded. Rejecting image: " << filename << std::endl;
            
            ocr::OCRResult result;
            result.set_image_id(imageId);
            result.set_success(false);
            result.set_error_message("Server memory limit exceeded");
            
            std::lock_guard<std::mutex> lock(m_streamMutex);
            stream->Write(result);
            continue;
        }
        
        std::cout << "Processing image: " << filename 
                  << " Size: " << imageData.size() << " bytes" 
                  << " Active tasks: " << activeTasks.load()
                  << " Total memory: " << (g_activeImageSize.load() / 1024 / 1024) << "MB" << std::endl;
        
        // Rate limiting: wait if too many concurrent tasks
        while (activeTasks.load() >= MAX_CONCURRENT_TASKS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Validate image data
        if (imageData.empty()) {
            std::cerr << "Empty image data for: " << filename << std::endl;
            
            ocr::OCRResult result;
            result.set_image_id(imageId);
            result.set_success(false);
            result.set_error_message("Empty image data");
            
            std::lock_guard<std::mutex> lock(m_streamMutex);
            stream->Write(result);
            continue;
        }
        
        // Get a processor for this task (round-robin)
        int processorIndex = m_nextProcessorIndex++ % m_processors.size();
        OCRProcessor* processor = m_processors[processorIndex].get();
        
        activeTasks++;
        g_activeImageSize += imageData.size();
        
        // Enqueue the OCR task
        m_threadPool.enqueue([this, stream, processor, imageId, filename, imageData, &activeTasks]() {
            std::string extractedText;
            
            try {
                extractedText = processor->processImage(imageData, filename);
            } catch (const std::exception& e) {
                std::cerr << "Exception in OCR processing for " << filename << ": " << e.what() << std::endl;
                extractedText = "";
            }
            
            // Update memory usage and active tasks
            g_activeImageSize -= imageData.size();
            activeTasks--;
            
            ocr::OCRResult result;
            result.set_image_id(imageId);
            result.set_extracted_text(extractedText);
            result.set_success(!extractedText.empty());
            
            if (extractedText.empty()) {
                result.set_error_message("OCR failed to extract text");
            }
            
            // PROTECT STREAM WRITE WITH MUTEX
            {
                std::lock_guard<std::mutex> lock(m_streamMutex);
                if (!stream->Write(result)) {
                    std::cerr << "Failed to send result for image: " << imageId << std::endl;
                } else {
                    std::cout << "Sent result for image: " << imageId 
                              << " Text: " << (extractedText.empty() ? "[EMPTY]" : extractedText.substr(0, 30)) 
                              << " Memory: " << (g_activeImageSize.load() / 1024 / 1024) << "MB" << std::endl;
                }
            }
        });
        
        // Small delay between enqueuing tasks to prevent overwhelming the system
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Wait for all pending tasks to complete before returning
    m_threadPool.waitAll();
    
    std::cout << "Client disconnected. Final memory: " << (g_activeImageSize.load() / 1024 / 1024) << "MB" << std::endl;
    return grpc::Status::OK;
}