#include "OCRProcessor.h"
#include <iostream>
#include <vector>

OCRProcessor::OCRProcessor() : m_initialized(false) {
}

OCRProcessor::~OCRProcessor() {
    if (m_tesseract) {
        m_tesseract->End();
    }
}

bool OCRProcessor::initialize() {
    m_tesseract = std::make_unique<tesseract::TessBaseAPI>();
    
    if (m_tesseract->Init(NULL, "eng")) {
        std::cerr << "Could not initialize Tesseract" << std::endl;
        return false;
    }
    
    // Configure Tesseract for better accuracy (from your working code)
    m_tesseract->SetPageSegMode(tesseract::PSM_SINGLE_WORD);
    m_tesseract->SetVariable("tessedit_char_whitelist", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    m_tesseract->SetVariable("load_system_dawg", "0");
    m_tesseract->SetVariable("load_freq_dawg", "0");
    m_tesseract->SetVariable("textord_min_linesize", "2.0");
    m_tesseract->SetVariable("tessedit_ocr_engine_mode", "1");
    
    m_initialized = true;
    return true;
}

std::string OCRProcessor::processImage(const std::string& imageData, const std::string& filename) {
    if (!m_initialized) {
        return "";
    }
    
    // Check if image data is valid
    if (imageData.empty()) {
        std::cerr << "Empty image data for: " << filename << std::endl;
        return "";
    }
    
    try {
        // Convert string data to Pix image
        Pix* cleanedImage = cleanImage(
            reinterpret_cast<const unsigned char*>(imageData.data()), 
            imageData.size()
        );
        
        if (!cleanedImage) {
            std::cerr << "Failed to preprocess image (possibly corrupt): " << filename << std::endl;
            return "";
        }
        
        // Perform OCR
        m_tesseract->SetImage(cleanedImage);
        char* outText = m_tesseract->GetUTF8Text();
        std::string extractedText = outText ? outText : "";
        
        // Clean up
        delete[] outText;
        pixDestroy(&cleanedImage);
        
        // Post-process text
        return postProcessText(extractedText);
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing image " << filename << ": " << e.what() << std::endl;
        return "";
    }
}

Pix* OCRProcessor::cleanImage(const unsigned char* imageData, size_t dataSize) {
    // Read image from memory
    Pix* pix = pixReadMem(imageData, dataSize);
    if (!pix) {
        return nullptr;
    }
    
    Pix* processed = pix;
    
    // Convert to grayscale if needed
    if (pixGetDepth(processed) != 8) {
        Pix* temp = pixConvertTo8(processed, 0);
        if (temp) {
            pixDestroy(&processed);
            processed = temp;
        }
    }
    
    // Simple noise reduction
    Pix* denoised = pixMedianFilter(processed, 1, 1);
    if (denoised) {
        pixDestroy(&processed);
        processed = denoised;
    }
    
    // Simple threshold
    Pix* binary = pixThresholdToBinary(processed, 128);
    if (binary) {
        pixDestroy(&processed);
        processed = binary;
    }
    
    return processed;
}

std::string OCRProcessor::postProcessText(const std::string& text) {
    if (text.empty()) return "";
    
    std::string result = text;
    
    // Remove leading/trailing whitespace
    result.erase(0, result.find_first_not_of(" \t\n\r\f\v"));
    result.erase(result.find_last_not_of(" \t\n\r\f\v") + 1);
    
    if (result.empty()) return "";
    
    // Common OCR error patterns to fix
    std::vector<std::pair<std::string, std::string>> replacements = {
        {"|", "l"}, {"[", "l"}, {"]", "l"}, {"`", "'"}, {"''", "\""},
        {" - ", "-"}, {" ,", ","}, {" .", "."}, {"\\", "l"}, {"//", "l"},
        {"0", "o"}, {"1", "l"}, {"5", "s"}
    };
    
    // Apply replacements
    for (const auto& replacement : replacements) {
        size_t pos = 0;
        while ((pos = result.find(replacement.first, pos)) != std::string::npos) {
            result.replace(pos, replacement.first.length(), replacement.second);
            pos += replacement.second.length();
        }
    }
    
    // Remove isolated punctuation at start/end
    if (!result.empty()) {
        std::string punctuation = ".,!?*-|`'\"";
        while (!result.empty() && punctuation.find(result[0]) != std::string::npos) {
            result.erase(0, 1);
        }
        while (!result.empty() && punctuation.find(result.back()) != std::string::npos) {
            result.pop_back();
        }
    }
    
    // Remove multiple consecutive spaces
    size_t pos = 0;
    while ((pos = result.find("  ", pos)) != std::string::npos) {
        result.replace(pos, 2, " ");
    }
    
    return result;
}