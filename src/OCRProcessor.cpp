#include "OCRProcessor.h"
#include <iostream>
#include <vector>

OCRProcessor::OCRProcessor() : m_initialized(false) {
}

OCRProcessor::~OCRProcessor() {
    if (m_tesseract) {
        // More aggressive cleanup to prevent Tesseract memory leaks
        m_tesseract->Clear();
        m_tesseract->ClearPersistentCache();
        m_tesseract->End();
    }
}

bool OCRProcessor::initialize() {
    m_tesseract = std::make_unique<tesseract::TessBaseAPI>();
    
    if (m_tesseract->Init(NULL, "eng")) {
        std::cerr << "Could not initialize Tesseract" << std::endl;
        return false;
    }
    
    // Configure Tesseract - simplified configuration to reduce memory issues
    m_tesseract->SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
    
    // Disable adaptive recognition and dictionaries to reduce memory usage
    m_tesseract->SetVariable("load_system_dawg", "0");
    m_tesseract->SetVariable("load_freq_dawg", "0"); 
    m_tesseract->SetVariable("load_unambig_dawg", "0");
    m_tesseract->SetVariable("load_punc_dawg", "0");
    m_tesseract->SetVariable("load_number_dawg", "0");
    m_tesseract->SetVariable("load_fixed_length_dawgs", "0");
    m_tesseract->SetVariable("load_bigram_dawg", "0");
    
    m_initialized = true;
    return true;
}

std::string OCRProcessor::processImage(const std::string& imageData, const std::string& filename) {
    if (!m_initialized) {
        std::cerr << "OCRProcessor not initialized for: " << filename << std::endl;
        return "";
    }
    
    if (imageData.empty()) {
        std::cerr << "Empty image data for: " << filename << std::endl;
        return "";
    }
    
    Pix* cleanedImage = nullptr;
    
    try {
        // Convert string data to Pix image
        cleanedImage = cleanImage(
            reinterpret_cast<const unsigned char*>(imageData.data()), 
            imageData.size()
        );
        
        if (!cleanedImage) {
            std::cerr << "Failed to preprocess image: " << filename << std::endl;
            return "";
        }
        
        // Clear Tesseract state before processing new image
        m_tesseract->Clear();
        
        // Perform OCR
        m_tesseract->SetImage(cleanedImage);
        char* outText = m_tesseract->GetUTF8Text();
        std::string extractedText = outText ? outText : "";
        
        // Clean up
        delete[] outText;
        pixDestroy(&cleanedImage);
        
        // Clear adaptive classifier to prevent memory buildup
        m_tesseract->ClearAdaptiveClassifier();
        
        return postProcessText(extractedText);
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing image " << filename << ": " << e.what() << std::endl;
        if (cleanedImage) {
            pixDestroy(&cleanedImage);
        }
        return "";
    }
}

Pix* OCRProcessor::cleanImage(const unsigned char* imageData, size_t dataSize) {
    Pix* pix = pixReadMem(imageData, dataSize);
    if (!pix) {
        return nullptr;
    }
    
    Pix* current = pix;
    Pix* next = nullptr;
    
    // Convert to grayscale if needed
    if (pixGetDepth(current) != 8) {
        next = pixConvertTo8(current, 0);
        if (!next) {
            pixDestroy(&current);
            return nullptr;
        }
        pixDestroy(&current);
        current = next;
    }
    
    // Only apply processing to reasonably sized images
    l_int32 width, height;
    pixGetDimensions(current, &width, &height, NULL);
    
    if (width > 100 && height > 100) {
        // Apply noise reduction
        next = pixMedianFilter(current, 1, 1);
        if (next) {
            pixDestroy(&current);
            current = next;
        }
        
        // Apply thresholding
        next = pixThresholdToBinary(current, 128);
        if (next) {
            pixDestroy(&current);
            current = next;
        }
    }
    
    return current;
}

std::string OCRProcessor::postProcessText(const std::string& text) {
    if (text.empty()) return "";
    
    std::string result = text;
    
    // Remove leading/trailing whitespace
    size_t start = result.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) return "";
    
    size_t end = result.find_last_not_of(" \t\n\r\f\v");
    result = result.substr(start, end - start + 1);
    
    // Remove multiple consecutive spaces
    size_t pos = 0;
    while ((pos = result.find("  ", pos)) != std::string::npos) {
        result.replace(pos, 2, " ");
        pos += 1;
    }
    
    return result;
}