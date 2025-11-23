#include "OCRServer.h"
#include "OCRService.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <csignal>
#include <atomic>

std::atomic<bool> shutdownServer(false);

void signalHandler(int signal) {
    std::cout << "\nReceived shutdown signal..." << std::endl;
    shutdownServer = true;
}

OCRServer::OCRServer(const std::string& address, size_t numThreads)
    : m_address(address)
    , m_numThreads(numThreads) {
}

OCRServer::~OCRServer() {
    shutdown();
}

void OCRServer::run() {
    try {
        OCRServiceImpl service(m_numThreads);
        
        grpc::ServerBuilder builder;
        builder.AddListeningPort(m_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);
        
        m_server = builder.BuildAndStart();
        std::cout << "OCR Server listening on " << m_address << std::endl;
        std::cout << "Using " << m_numThreads << " worker threads" << std::endl;
        std::cout << "Press Ctrl+C to stop the server..." << std::endl;
        
        // Set up signal handling for graceful shutdown
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
        
        // Wait for shutdown signal
        while (!shutdownServer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
    }
}

void OCRServer::shutdown() {
    if (m_server) {
        std::cout << "Shutting down server..." << std::endl;
        m_server->Shutdown();
        m_server->Wait();
        std::cout << "Server shutdown complete" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    std::string address = "0.0.0.0:50051";  // Default to all interfaces
    size_t numThreads = 4;
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--address" && i + 1 < argc) {
            std::string ip = argv[++i];
            address = ip + ":50051";  // Use specified IP with default port
        } else if (arg == "--port" && i + 1 < argc) {
            std::string port = argv[++i];
            // Extract IP part if address was already set, otherwise use 0.0.0.0
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