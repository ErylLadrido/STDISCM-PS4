#include "OCRServer.h"
#include "OCRService.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <csignal>
#include <atomic>
#include <cstdlib>
#include <thread>
#include <chrono>

std::atomic<bool> shutdownServer(false);

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", initiating shutdown..." << std::endl;
    shutdownServer = true;
}

void segmentationHandler(int signal) {
    std::cerr << "Segmentation fault occurred! Attempting graceful shutdown..." << std::endl;
    std::cerr << "Segmentation fault signal: " << signal << std::endl;
    shutdownServer = true;
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::exit(1);
}

OCRServer::OCRServer(const std::string& address, size_t numThreads)
    : m_address(address)
    , m_numThreads(numThreads) {
}

OCRServer::~OCRServer() {
    shutdown();
}

void OCRServer::run() {
    int restartCount = 0;
    const int MAX_RESTARTS = 3;
    
    while (restartCount < MAX_RESTARTS) {
        try {
            std::cout << "Starting OCR Server (attempt " << (restartCount + 1) << ")..." << std::endl;
            
            OCRServiceImpl service(m_numThreads);
            
            grpc::ServerBuilder builder;
            builder.AddListeningPort(m_address, grpc::InsecureServerCredentials());
            builder.RegisterService(&service);
            
            // Set max message size to handle large images
            builder.SetMaxMessageSize(100 * 1024 * 1024); // 100MB
            builder.SetMaxReceiveMessageSize(100 * 1024 * 1024); // 100MB
            
            m_server = builder.BuildAndStart();
            if (!m_server) {
                throw std::runtime_error("Failed to build and start server");
            }
            
            std::cout << "OCR Server listening on " << m_address << std::endl;
            std::cout << "Using " << m_numThreads << " worker threads" << std::endl;
            std::cout << "Press Ctrl+C to stop the server..." << std::endl;
            
            // Set up signal handling
            std::signal(SIGINT, signalHandler);
            std::signal(SIGTERM, signalHandler);
            std::signal(SIGSEGV, segmentationHandler);
            
            // Reset restart count on successful start
            restartCount = 0;
            
            // Wait for shutdown signal
            while (!shutdownServer) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            
            shutdown();
            break;
            
        } catch (const std::exception& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
            restartCount++;
            
            if (restartCount < MAX_RESTARTS) {
                std::cerr << "Restarting server in 5 seconds..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(5));
                shutdownServer = false;
            } else {
                std::cerr << "Maximum restart attempts reached. Server will not restart." << std::endl;
                break;
            }
        }
    }
}

void OCRServer::shutdown() {
    if (m_server) {
        std::cout << "Shutting down server gracefully..." << std::endl;
        auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(10);
        m_server->Shutdown(deadline);
        std::cout << "Server shutdown complete" << std::endl;
    }
}

// MAIN FUNCTION MUST BE PRESENT
int main(int argc, char* argv[]) {
    std::string address = "0.0.0.0:50051";
    size_t numThreads = 4;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--address" && i + 1 < argc) {
            std::string ip = argv[++i];
            address = ip + ":50051";
        } else if (arg == "--port" && i + 1 < argc) {
            std::string port = argv[++i];
            size_t colonPos = address.find(':');
            if (colonPos != std::string::npos) {
                address = address.substr(0, colonPos) + ":" + port;
            } else {
                address = "0.0.0.0:" + port;
            }
        } else if (arg == "--threads" && i + 1 < argc) {
            numThreads = std::stoul(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [--address IP] [--port PORT] [--threads NUM_THREADS]" << std::endl;
            std::cout << "Examples:" << std::endl;
            std::cout << "  " << argv[0] << " --address 192.168.1.100 --port 50051" << std::endl;
            std::cout << "  " << argv[0] << " --port 8080 --threads 8" << std::endl;
            return 0;
        }
    }
    
    OCRServer server(address, numThreads);
    server.run();
    
    return 0;
}