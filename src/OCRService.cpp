#include "OCRService.h"
#include <iostream>
#include <mutex>

OCRServiceImpl::OCRServiceImpl(size_t numThreads) 
    : m_threadPool(numThreads)
    , m_nextProcessorIndex(0)
{
    // Create one OCRProcessor per thread for thread safety
    for (size_t i = 0; i < numThreads; ++i) {
        auto processor = std::make_unique<OCRProcessor>();
        if (processor->initialize()) {
            m_processors.push_back(std::move(processor));
        }
    }
    
    if (m_processors.empty()) {
        throw std::runtime_error("Failed to initialize any OCR processors");
    }
    
    std::cout << "OCR Service initialized with " << m_processors.size() << " processors" << std::endl;
}

OCRServiceImpl::~OCRServiceImpl() {
    // Wait for all tasks to complete
    m_threadPool.waitAll();
}

grpc::Status OCRServiceImpl::ProcessImages(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<ocr::OCRResult, ocr::ImageRequest>* stream) {
    
    std::cout << "Client connected" << std::endl;
    
    ocr::ImageRequest request;
    while (stream->Read(&request)) {
        std::string imageId = request.image_id();
        std::string filename = request.filename();
        std::string imageData = request.image_data();
        
        std::cout << "Processing image: " << filename << " (ID: " << imageId << ")" << std::endl;
        
        // Get a processor for this task (round-robin)
        int processorIndex = m_nextProcessorIndex++ % m_processors.size();
        OCRProcessor* processor = m_processors[processorIndex].get();
        
        // Enqueue the OCR task
        m_threadPool.enqueue([stream, processor, imageId, filename, imageData]() {
            std::string extractedText = processor->processImage(imageData, filename);
            
            ocr::OCRResult result;
            result.set_image_id(imageId);
            result.set_extracted_text(extractedText);
            result.set_success(!extractedText.empty());
            
            if (extractedText.empty()) {
                result.set_error_message("OCR failed to extract text");
            }
            
            // Send result back to client
            if (!stream->Write(result)) {
                std::cerr << "Failed to send result for image: " << imageId << std::endl;
            } else {
                std::cout << "Sent result for image: " << imageId << std::endl;
            }
        });
    }
    
    std::cout << "Client disconnected" << std::endl;
    return grpc::Status::OK;
}