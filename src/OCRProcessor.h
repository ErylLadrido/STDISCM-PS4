#ifndef OCRPROCESSOR_H
#define OCRPROCESSOR_H

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <string>
#include <memory>

class OCRProcessor {
public:
    OCRProcessor();
    ~OCRProcessor();
    
    bool initialize();
    std::string processImage(const std::string& imageData, const std::string& filename);
    
private:
    std::string postProcessText(const std::string& text);
    Pix* cleanImage(const unsigned char* imageData, size_t dataSize);
    
    std::unique_ptr<tesseract::TessBaseAPI> m_tesseract;
    bool m_initialized;
};

#endif // OCRPROCESSOR_H