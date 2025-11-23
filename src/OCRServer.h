#ifndef OCRSERVER_H
#define OCRSERVER_H

#include <string>
#include <memory>
#include <grpcpp/grpcpp.h>  
class OCRServer {
public:
    OCRServer(const std::string& address = "0.0.0.0:50051", size_t numThreads = 4);
    ~OCRServer();
    
    void run();
    void shutdown();

private:
    std::string m_address;
    size_t m_numThreads;
    std::unique_ptr<grpc::Server> m_server;
};

#endif // OCRSERVER_H