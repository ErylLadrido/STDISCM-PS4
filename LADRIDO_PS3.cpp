#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <semaphore>
#include <atomic>
#include <vector>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace fs = std::filesystem;

// Structure to hold OCR result
struct OCRResult {
    int id;
    std::string filename;
    std::string extractedText;
    long long processingTimeMs;
};

// Thread-safe queue for image paths
template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> shutdown{false};

public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(value));
        condition.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this]() { return shutdown || !queue.empty(); });
        
        if (shutdown && queue.empty()) {
            return false;
        }
        
        value = std::move(queue.front());
        queue.pop();
        return true;
    }

    void stop() {
        shutdown = true;
        condition.notify_all();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }
};

// Thread-safe results storage
class ResultsManager {
private:
    std::vector<OCRResult> results;
    std::mutex mutex;
    std::atomic<int> nextId{1};

public:
    void addResult(const std::string& filename, const std::string& text, long long timeMs) {
        std::lock_guard<std::mutex> lock(mutex);
        OCRResult result;
        result.id = nextId++;
        result.filename = filename;
        result.extractedText = text;
        result.processingTimeMs = timeMs;
        results.push_back(result);
    }

    void saveToCSV(const std::string& outputPath) {
        std::lock_guard<std::mutex> lock(mutex);
        std::ofstream file(outputPath);
        
        if (!file.is_open()) {
            std::cerr << "Error: Cannot create output file: " << outputPath << std::endl;
            return;
        }

        // Write CSV header
        file << "ID,Filename,Extracted Text,Processing Time (ms)\n";

        // Write results
        for (const auto& result : results) {
            file << result.id << ",";
            file << "\"" << result.filename << "\",";
            
            // Escape quotes in text and wrap in quotes
            std::string escapedText = result.extractedText;
            size_t pos = 0;
            while ((pos = escapedText.find("\"", pos)) != std::string::npos) {
                escapedText.replace(pos, 1, "\"\"");
                pos += 2;
            }
            // Remove newlines for CSV compatibility
            pos = 0;
            while ((pos = escapedText.find("\n", pos)) != std::string::npos) {
                escapedText.replace(pos, 1, " ");
                pos += 1;
            }
            
            file << "\"" << escapedText << "\",";
            file << result.processingTimeMs << "\n";
        }

        file.close();
        std::cout << "\nResults saved to: " << outputPath << std::endl;
        std::cout << "Total images processed: " << results.size() << std::endl;
    }
};

// Image preprocessing class 
class OCRImageCleaner {
public:
    Pix* cleanImage(const std::string& inputPath) {
        std::cout << "  Processing: " << fs::path(inputPath).filename() << std::endl;
        
        // Read image
        Pix* pix = pixRead(inputPath.c_str());
        if (!pix) {
            std::cerr << "  Error: Cannot read image" << std::endl;
            return nullptr;
        }

        l_int32 width, height, depth;
        pixGetDimensions(pix, &width, &height, &depth);
        std::cout << "  Original: " << width << "x" << height << ", depth: " << depth << std::endl;

        // Convert to grayscale if needed
        if (depth != 8) {
            Pix* temp = pixConvertTo8(pix, 0);
            if (temp) {
                pixDestroy(&pix);
                pix = temp;
            }
        }

        // Simple noise reduction
        Pix* denoised = pixMedianFilter(pix, 1, 1);
        if (denoised) {
            pixDestroy(&pix);
            pix = denoised;
        }

        // Simple threshold
        Pix* binary = pixThresholdToBinary(pix, 128);
        if (binary) {
            pixDestroy(&pix);
            pix = binary;
        }

        pixGetDimensions(pix, &width, &height, &depth);
        std::cout << "  Final: " << width << "x" << height << ", depth: " << depth << std::endl;

        return pix;
    }
};

std::string postProcessText(const std::string& text) {
    if (text.empty()) return "";
    
    std::string result = text;
    
    // Remove leading/trailing whitespace
    result.erase(0, result.find_first_not_of(" \t\n\r\f\v"));
    result.erase(result.find_last_not_of(" \t\n\r\f\v") + 1);
    
    if (result.empty()) return "";
    
    // Common OCR error patterns to fix
    std::vector<std::pair<std::string, std::string>> replacements = {
        {"|", "l"},        // Common OCR error: | -> l
        {"[", "l"},        // [ -> l  
        {"]", "l"},        // ] -> l
        {"`", "'"},        // ` -> '
        {"''", "\""},      // '' -> "
        {" - ", "-"},      // Remove spaces around hyphens
        {" ,", ","},       // Fix space before comma
        {" .", "."},       // Fix space before period
        {"\\", "l"},       // \ -> l
        {"//", "l"},       // // -> l
        {"0", "o"},        // 0 -> o (common misrecognition)
        {"1", "l"},        // 1 -> l
        {"5", "s"},        // 5 -> s
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

// Producer thread function - loads image paths into queue
void producerThread(const std::string& directoryPath, 
                   ThreadSafeQueue<std::string>& imageQueue,
                   std::counting_semaphore<>& semaphore,
                   std::atomic<bool>& producerDone) {
    try {
        std::cout << "Producer: Scanning directory: " << directoryPath << std::endl;
        
        int imageCount = 0;
        for (const auto& entry : fs::directory_iterator(directoryPath)) {
            if (entry.is_regular_file()) {
                std::string extension = entry.path().extension().string();
                // Convert to lowercase for comparison
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                
                if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || 
                    extension == ".tiff" || extension == ".bmp" || extension == ".tif") {
                    
                    imageQueue.push(entry.path().string());
                    semaphore.release(); // Signal that an item is available
                    imageCount++;
                    std::cout << "Producer: Added " << entry.path().filename() << " to queue" << std::endl;
                }
            }
        }
        
        std::cout << "Producer: Finished loading " << imageCount << " images" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Producer error: " << e.what() << std::endl;
    }
    
    producerDone = true;
    imageQueue.stop();
}

// Worker thread function - processes images with OCR
void workerThread(int workerId,
                 ThreadSafeQueue<std::string>& imageQueue,
                 std::counting_semaphore<>& semaphore,
                 std::atomic<bool>& producerDone,
                 ResultsManager& resultsManager) {
    
    // Initialize Tesseract OCR engine with better configuration
    tesseract::TessBaseAPI* ocr = new tesseract::TessBaseAPI();
    
    // Configure Tesseract for better accuracy
    if (ocr->Init(NULL, "eng")) {
        std::cerr << "Worker " << workerId << ": Could not initialize Tesseract" << std::endl;
        delete ocr;
        return;
    }
    
    // Set Tesseract parameters for better text recognition
    ocr->SetPageSegMode(tesseract::PSM_SINGLE_WORD); // Treat image as a single word
    ocr->SetVariable("tessedit_char_whitelist", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"); // Limit to alphanumeric
    ocr->SetVariable("load_system_dawg", "0"); // Don't load system dictionary
    ocr->SetVariable("load_freq_dawg", "0");   // Don't load frequent words dictionary
    ocr->SetVariable("textord_min_linesize", "2.0"); // Minimum line size
    ocr->SetVariable("tessedit_ocr_engine_mode", "1"); // Neural nets only
    
    OCRImageCleaner cleaner;
    int processedCount = 0;
    
    std::cout << "Worker " << workerId << ": Started" << std::endl;
    
    while (true) {
        // Wait for semaphore signal (item available in queue)
        if (!semaphore.try_acquire_for(std::chrono::milliseconds(100))) {
            // Check if producer is done and queue is empty
            if (producerDone && imageQueue.empty()) {
                break;
            }
            continue;
        }
        
        std::string imagePath;
        if (!imageQueue.pop(imagePath)) {
            break; // Queue is shutdown
        }
        
        if (imagePath.empty()) continue;
        
        try {
            auto startTime = std::chrono::high_resolution_clock::now();
            
            std::string filename = fs::path(imagePath).filename().string();
            std::cout << "Worker " << workerId << ": Processing " << filename << std::endl;
            
            // Preprocess the image
            Pix* cleanedImage = cleaner.cleanImage(imagePath);
            
            if (!cleanedImage) {
                std::cerr << "Worker " << workerId << ": Failed to preprocess " << filename << std::endl;
                continue;
            }
            
            // Perform OCR inference
            ocr->SetImage(cleanedImage);
            char* outText = ocr->GetUTF8Text();
            std::string extractedText = outText ? outText : "";
            extractedText = postProcessText(extractedText);  
            
            // Clean up
            delete[] outText;
            pixDestroy(&cleanedImage);
            
            // Post-process the extracted text
            extractedText = postProcessText(extractedText);
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            // Store result
            resultsManager.addResult(filename, extractedText, duration.count());
            
            processedCount++;
            std::cout << "Worker " << workerId << ": Completed " << filename 
                     << " (" << duration.count() << "ms)" << std::endl;
            std::cout << "Worker " << workerId << ": Extracted: '" << extractedText << "'" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "Worker " << workerId << ": Error processing " << imagePath 
                     << " - " << e.what() << std::endl;
        }
    }
    
    std::cout << "Worker " << workerId << ": Finished processing " << processedCount << " images" << std::endl;
    
    // Cleanup Tesseract
    ocr->End();
    delete ocr;
}

int main(int argc, char* argv[]) {
    std::string inputDir;
    int numWorkers = 2; // Default to 2 worker threads
    
    // Prompt user for input directory
    if (argc >= 2) {
        inputDir = argv[1];
        if (argc >= 3) {
            numWorkers = std::stoi(argv[2]);
        }
    } else {
        std::cout << "Enter the directory path containing images to process: ";
        std::getline(std::cin, inputDir);
        
        std::cout << "Enter number of worker threads (default 2): ";
        std::string numWorkersStr;
        std::getline(std::cin, numWorkersStr);
        if (!numWorkersStr.empty()) {
            numWorkers = std::stoi(numWorkersStr);
        }
    }
    
    // Validate input directory
    if (!fs::exists(inputDir) || !fs::is_directory(inputDir)) {
        std::cerr << "Error: Input directory '" << inputDir << "' does not exist or is not a directory" << std::endl;
        return 1;
    }
    
    if (numWorkers < 2) {
        std::cerr << "Error: Must have at least 2 worker threads" << std::endl;
        return 1;
    }
    
    std::cout << "\n=== Starting Multithreaded OCR Pipeline ===" << std::endl;
    std::cout << "Input directory: " << inputDir << std::endl;
    std::cout << "Number of worker threads: " << numWorkers << std::endl;
    std::cout << "=========================================\n" << std::endl;
    
    // Initialize shared resources
    ThreadSafeQueue<std::string> imageQueue;
    std::counting_semaphore<> semaphore(0); // Start with 0, producer will release
    std::atomic<bool> producerDone{false};
    ResultsManager resultsManager;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Start producer thread
    std::thread producer(producerThread, inputDir, std::ref(imageQueue), 
                        std::ref(semaphore), std::ref(producerDone));
    
    // Start worker threads
    std::vector<std::thread> workers;
    for (int i = 0; i < numWorkers; i++) {
        workers.emplace_back(workerThread, i + 1, std::ref(imageQueue), 
                           std::ref(semaphore), std::ref(producerDone), 
                           std::ref(resultsManager));
    }
    
    // Wait for all threads to complete
    producer.join();
    for (auto& worker : workers) {
        worker.join();
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // Save results to CSV
    resultsManager.saveToCSV("result1.csv");
    
    std::cout << "\n=== Pipeline Completed ===" << std::endl;
    std::cout << "Total processing time: " << duration.count() << "ms" << std::endl;
    std::cout << "========================\n" << std::endl;
    
    return 0;
}