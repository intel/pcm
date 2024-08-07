#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <netinet/tcp.h>

#define UNIT_TEST 1

#include "../src/pcm-sensor-server.cpp"

#undef UNIT_TEST

int port = 0;

bool waitForPort(int port, int timeoutSeconds) {
    int sockfd;
    struct sockaddr_in address;
    bool isBound = false;
    time_t startTime = time(nullptr);

    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Error creating socket" << std::endl;
        return false;
    }

    // Set up the address structure
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(port);

    // Loop until the port is bound or the timeout is reached
    while (!isBound && (time(nullptr) - startTime) < timeoutSeconds) {
        // Attempt to connect to the port
        if (connect(sockfd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            // Connection failed, wait a bit before retrying
            sleep(1);
        } else {
            // Connection succeeded, the port is bound
            isBound = true;
        }
    }

    // Clean up the socket
    close(sockfd);
    return isBound;
}

HTTPServer * httpServer;
std::thread * serverThread;

void cleanup()
{
    std::cerr << "Stopping HTTPServer\n";
    httpServer->stop();
    std::cerr << "Cleaning up PMU:\n";
    PCM::getInstance()->cleanup();
}

bool init()
{
    port = (rand() % 100) + 10000; // to be able to restart the fuzzer quickly without waiting for the port to be released
    serverThread = new std::thread([]() {
        PCM::ErrorCode status;
        PCM * pcmInstance = PCM::getInstance();
        assert(pcmInstance);
        pcmInstance->resetPMU();
        status = pcmInstance->program();
        if (status != PCM::Success) {
            std::cerr << "Error in program() function" << std::endl;
            exit(1);
        }
        debug::dyn_debug_level(0);
        #ifdef FUZZ_USE_SSL
        std::cerr << "Starting SSL enabled server on https://localhost:" << port << "/\n";
        auto httpsServer = new HTTPSServer( "", port );
        httpsServer->setPrivateKeyFile ( "/private.key" );
        httpsServer->setCertificateFile( "/certificate.crt" );
        httpsServer->initialiseSSL();
        httpServer = httpsServer;
        #else
        std::cerr << "Starting plain HTTP server on http://localhost:" << port << "/\n";
        httpServer = new HTTPServer( "", port );
        #endif
        // HEAD is GET without body, we will remove the body in execute()
        httpServer->registerCallback( HTTPRequestMethod::GET,  my_get_callback );
        httpServer->registerCallback( HTTPRequestMethod::HEAD, my_get_callback );
        httpServer->run();
    });
    int timeout = 60; // Timeout in seconds
    std::cout << "Waiting for port " << port << " to be bound with timeout of " << timeout << " seconds..." << std::endl;
    std::cout.flush();
    if (waitForPort(port, timeout)) {
            std::cout << "Port " << port << " is now bound." << std::endl;
    } else {
            std::cout << "Port " << port << " is not bound after " << timeout << " seconds." << std::endl;
            exit(1);
    }
    atexit(cleanup);
    return true;
}


std::vector<char> buffer(1024*1024*16);

std::string make_request(const std::string& request) {
#ifdef FUZZ_USE_SSL
    const SSL_METHOD* method = TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);
    if (!ctx) {
        throw std::runtime_error("Unable to create SSL context");
    }
#endif
    std::string server = "127.0.0.1";
    // Resolve the host
    struct hostent* host = gethostbyname(server.c_str());
    if (!host) {
#ifdef FUZZ_USE_SSL
        SSL_CTX_free(ctx);
#endif
        std::cerr << "Failed to resolve host. Error: " << strerror(errno) << std::endl;
        throw std::runtime_error("Failed to resolve host: " + server);
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
#ifdef FUZZ_USE_SSL
        SSL_CTX_free(ctx);
#endif
        std::cerr << "Failed to create socket. Error: " << strerror(errno) << std::endl;
        throw std::runtime_error("Failed to create socket");
    }

    // Create server address structure
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    std::memcpy(&server_addr.sin_addr, host->h_addr, host->h_length);

    // Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Failed to connect to server. Error: " << strerror(errno) << std::endl;
        close(sock);
#ifdef FUZZ_USE_SSL
        SSL_CTX_free(ctx);
#endif
        throw std::runtime_error("Failed to connect to server");
    }

#ifdef FUZZ_USE_SSL
    // Create SSL structure
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) <= 0) {
        SSL_free(ssl);
        close(sock);
        SSL_CTX_free(ctx);
        throw std::runtime_error("Failed to establish SSL connection");
    }
#endif

    std::cout << "Sending request: " << request << "\n=====\n";

    std::string response;
    int bytes_received = -1;
#ifdef FUZZ_USE_SSL
    // Send the request
    if (SSL_write(ssl, request.c_str(), request.length()) <= 0) {
        SSL_free(ssl);
        close(sock);
        SSL_CTX_free(ctx);
        return "Failed to send request, no response";
    }

    // Receive the response
    bytes_received = SSL_read(ssl, &(buffer[0]), buffer.size());
#else
    // Send the request
    if (send(sock, request.c_str(), request.length(), 0) < 0) {
        std::cerr << "Failed to send request. Error: " << strerror(errno) << std::endl;
        close(sock);
        return "Failed to send request, no response"; // not sure why it happens relatively often
        // throw std::runtime_error("Failed to send request");
    }

    // Receive the response
    bytes_received = recv(sock, &(buffer[0]), buffer.size(), 0);
#endif
    if (bytes_received > 0)
    {
        response.append(&(buffer[0]), bytes_received);
    }

    if (bytes_received < 0) {
#ifdef FUZZ_USE_SSL
        SSL_free(ssl);
#endif
        std::cerr << "Failed to receive response. Error: " << strerror(errno) << std::endl;
        close(sock);
#ifdef FUZZ_USE_SSL
        SSL_CTX_free(ctx);
#endif
        // throw std::runtime_error("Failed to receive response");
        return "Failed to receive response"; // expected to happen sometimes
    }

    // clean up
#ifdef FUZZ_USE_SSL
    SSL_shutdown(ssl);
    SSL_free(ssl);
#endif
    close(sock);
#ifdef FUZZ_USE_SSL
    SSL_CTX_free(ctx);
#endif

    return response;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static bool initialized = false;
    if (!initialized) {
        initialized = init();
    }
    try {
        std::string request = std::string((const char*)data, size);
        std::string response = make_request(request);
        std::cout << response << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "LLVMFuzzerTestOneInput Exception: \"" << e.what() << "\"" << std::endl;
        exit(1);
    }
   return 0;
}
