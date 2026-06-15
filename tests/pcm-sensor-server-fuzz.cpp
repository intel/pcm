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
        DBG( 0, "Client: Error creating socket" );
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
    DBG( 0, "Client: Stopping HTTPServer" );
    httpServer->stop();
    DBG( 0, "Client: Cleaning up PMU:" );
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
            DBG( 0, "Client: Error in program() function" );
            exit(1);
        }
        debug::dyn_debug_level(1);
        #ifdef FUZZ_USE_SSL
        DBG( 0, "Client: Starting SSL enabled server on https://localhost:", port );
        auto httpsServer = new HTTPSServer( "", port );
        httpsServer->setPrivateKeyFile ( "/private.key" );
        httpsServer->setCertificateFile( "/certificate.crt" );
        httpsServer->initialiseSSL();
        httpServer = httpsServer;
        #else
        DBG( 0, "Client: Starting plain HTTP server on http://localhost:", port );
        httpServer = new HTTPServer( "", port );
        #endif
        // HEAD is GET without body, we will remove the body in execute()
        httpServer->registerCallback( HTTPRequestMethod::GET,  my_get_callback );
        httpServer->registerCallback( HTTPRequestMethod::HEAD, my_get_callback );
        httpServer->run();
    });
    int timeout = 60; // Timeout in seconds
    DBG( 0, "Client: Waiting for port ", port, " to be bound with timeout of ", timeout, " seconds..." );
    if (waitForPort(port, timeout)) {
            DBG( 0, "Client: Port ", port, " is now bound." );
    } else {
            DBG( 0, "Client: Port ", port, " is not bound after ", timeout, " seconds." );
            exit(1);
    }
    atexit(cleanup);
    return true;
}

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
        DBG( 0, "Client: Failed to resolve host. Error: ", strerror(errno) );
        throw std::runtime_error("Failed to resolve host: " + server);
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
#ifdef FUZZ_USE_SSL
        SSL_CTX_free(ctx);
#endif
        DBG( 0, "Client: Failed to create socket. Error: ", strerror(errno) );
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
        DBG( 0, "Failed to connect to server. Error: ", strerror(errno) );
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
    int con_ret = SSL_connect(ssl);
    DBG( 1, "Client: SSL_connect returned ", con_ret );
    if ( con_ret <= 0) {
        SSL_free(ssl);
        close(sock);
        SSL_CTX_free(ctx);
        throw std::runtime_error("Failed to establish SSL connection");
    }
#endif

#ifdef FUZZ_USE_SSL
    // "Client:" is used as a hint to indicate whether client or server wrote the debug messages inside socketstream and socketbuf
    // When using this socketstream, it takes ownership of the socket and ssl connection and is responsible for properly closing 
    // connections and freeing the allocated structures, this is why all of the frees and closes are commented out below
    DBG( 0, "Client: Opening an SSL socket stream" );
    socketstream mystream( sock, ssl, "Client: " );
#else
    DBG( 0, "Client: Opening a normal socket stream" );
    socketstream mystream( sock );
#endif
    DBG( 0, "Sending request: \n", request, "\n=====" );

    std::string response;
    int bytes_received = -1;
    // Send the request
    try {
        mystream << request.c_str();
        mystream.sync();
    } catch ( const std::exception& e ) {
        DBG( 0, "Writing caused an exception: ", e.what() );
        mystream.close();
#ifdef FUZZ_USE_SSL
        SSL_CTX_free(ctx);
#endif
        throw std::runtime_error(std::string("Client Failed to write the request: ") + e.what());
    }
    // Receive the response
    HTTPResponse resp;
    DBG( 0, "Client: Waiting for response:" );
    try {
        mystream >> resp;
    } catch ( const std::exception& e ) {
        mystream.close();
#ifdef FUZZ_USE_SSL
        SSL_CTX_free(ctx);
#endif
        DBG( 0, "Reading from the socket failed, reason: ", e.what() );
        return std::string("Not necessarily fatal: Client: Exception caught while reading a response from the server: ") + e.what();
    }

    // We've got a valid HTTPResponse otherwise we'd have caught an exception above
    HTTPHeader const h = resp.getHeader( "Content-Length" );
    size_t contentLength = h.headerValueAsNumber();

    // contentLength must be positive now otherwise the bad Content-Length should have thrown an exception
    bytes_received = contentLength;

    DBG( 0, "Client: received ", bytes_received, " bytes, copying them into response." );
    // Reducing verbosity, only print the first 1024 characters of the response
    response.append( resp.body() );
    if ( response.size() > 1024 )
        response.erase(1024, std::string::npos );

    // clean up
    mystream.close();
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
        DBG( 0, "Response:\n", response, "\n====" );
    } catch (const std::exception& e) {
        DBG( 0, "Client: LLVMFuzzerTestOneInput Exception: \"", e.what(), "\"" );
        exit(1);
    }
   return 0;
}
