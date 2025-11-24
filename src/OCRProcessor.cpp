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
    
    // Try different page segmentation modes
    m_tesseract->SetPageSegMode(tesseract::PSM_AUTO_OSD);
    
    // Improved configuration for better accuracy
    m_tesseract->SetVariable("tessedit_char_blacklist", "|[]\\");
    m_tesseract->SetVariable("textord_min_linesize", "2.5");
    m_tesseract->SetVariable("textord_heavy_nr", "1");
    m_tesseract->SetVariable("edges_max_children_per_outline", "40");
    
    // Keep some dictionaries for better word recognition
    m_tesseract->SetVariable("load_system_dawg", "1");
    m_tesseract->SetVariable("load_freq_dawg", "1");
    m_tesseract->SetVariable("load_unambig_dawg", "1");
    
    // Disable the ones that cause memory issues
    m_tesseract->SetVariable("load_punc_dawg", "0");
    m_tesseract->SetVariable("load_number_dawg", "0");
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
    
    // Simple thresholding - most reliable approach
    next = pixThresholdToBinary(current, 128);
    if (next) {
        pixDestroy(&current);
        current = next;
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
    
    if (result.empty()) return "";
    
    // Common OCR error corrections with context awareness
    std::vector<std::pair<std::string, std::string>> replacements = {
        // Common character confusions
        {"|", "l"}, {"[", "l"}, {"]", "l"}, {"\\", "l"}, {"//", "l"},
        {"``", "\""}, {"''", "\""}, {"`", "'"}, {"´", "'"}, {"‘", "'"}, {"’", "'"},
        {"“", "\""}, {"”", "\""}, {"„", "\""},
        
        // Number/letter confusions (context-dependent)
        {"0", "O"},  // Zero to capital O
        {"1", "l"},  // One to lowercase L
        {"5", "S"},  // Five to capital S
        {"8", "B"},  // Eight to capital B
        {"6", "G"},  // Six to capital G
        {"9", "g"},  // Nine to lowercase G
        
        // Space and punctuation fixes
        {" ,", ","}, {" .", "."}, {" ;", ";"}, {" :", ":"},
        {"( ", "("}, {" )", ")"}, {"[ ", "["}, {" ]", "]"},
        {"{ ", "{"}, {" }", "}"}, {" /", "/"}, {"\\ ", "\\"}
    };
    
    // Apply basic replacements
    for (const auto& replacement : replacements) {
        size_t pos = 0;
        while ((pos = result.find(replacement.first, pos)) != std::string::npos) {
            result.replace(pos, replacement.first.length(), replacement.second);
            pos += replacement.second.length();
        }
    }
    
    // Advanced context-aware replacements
    result = applyContextualReplacements(result);
    
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
        pos += 1;
    }
    
    // Final validation - if result looks like garbage, return empty
    if (isLikelyGarbage(result)) {
        return "";
    }
    
    return result;
}

// New helper function for context-aware replacements
std::string OCRProcessor::applyContextualReplacements(const std::string& text) {
    if (text.length() <= 1) return text;
    
    std::string result = text;
    
    // Context-aware number/letter replacements
    for (size_t i = 0; i < result.length(); ++i) {
        char c = result[i];
        
        // Zero to O - usually at start of words or in all-caps contexts
        if (c == '0' && (i == 0 || !std::isalnum(result[i-1]))) {
            result[i] = 'O';
        }
        // One to l - usually in middle of words
        else if (c == '1' && i > 0 && i < result.length()-1 && 
                 std::isalpha(result[i-1]) && std::isalpha(result[i+1])) {
            result[i] = 'l';
        }
        // Five to S - usually at start or middle of words
        else if (c == '5' && i < result.length()-1 && std::isalpha(result[i+1])) {
            result[i] = 'S';
        }
    }
    
    // Fix common word patterns
    std::vector<std::pair<std::string, std::string>> wordReplacements = {
        {"lhe", "the"}, {"lhat", "that"}, {"lhis", "this"}, {"lhere", "there"},
        {"wi1h", "with"}, {"1he", "the"}, {"0r", "Or"}, {"5tart", "Start"},
        {"8ack", "Back"}, {"9ood", "good"}, {"6reat", "Great"}
    };
    
    for (const auto& replacement : wordReplacements) {
        size_t pos = 0;
        while ((pos = result.find(replacement.first, pos)) != std::string::npos) {
            // Only replace if it's a whole word or at word boundaries
            if ((pos == 0 || !std::isalnum(result[pos-1])) &&
                (pos + replacement.first.length() >= result.length() || 
                 !std::isalnum(result[pos + replacement.first.length()]))) {
                result.replace(pos, replacement.first.length(), replacement.second);
                pos += replacement.second.length();
            } else {
                pos += replacement.first.length();
            }
        }
    }
    
    return result;
}

// New function to detect likely garbage text
bool OCRProcessor::isLikelyGarbage(const std::string& text) {
    if (text.empty() || text.length() > 100) return false; // Too long might be real text
    
    int letterCount = 0;
    int digitCount = 0;
    int symbolCount = 0;
    int consecutiveSymbols = 0;
    int maxConsecutiveSymbols = 0;
    
    for (char c : text) {
        if (std::isalpha(c)) {
            letterCount++;
            consecutiveSymbols = 0;
        } else if (std::isdigit(c)) {
            digitCount++;
            consecutiveSymbols = 0;
        } else if (!std::isspace(c)) {
            symbolCount++;
            consecutiveSymbols++;
            maxConsecutiveSymbols = std::max(maxConsecutiveSymbols, consecutiveSymbols);
        } else {
            consecutiveSymbols = 0;
        }
    }
    
    // If mostly symbols or too many consecutive symbols, likely garbage
    if (symbolCount > letterCount + digitCount) return true;
    if (maxConsecutiveSymbols >= 3) return true;
    
    // If very short but has multiple different symbols, likely garbage
    if (text.length() < 5 && symbolCount >= 2) return true;
    
    return false;
}