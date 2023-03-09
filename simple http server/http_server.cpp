/**
 * @file http_server.cpp
 * @author Ana Mendes
 * @brief multithreaded http server with logger
 * @version 0.1
 * @date 2023-01-29
 * @see "Seattle University, cpsc5042, Winter 2023"
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include <fstream>
#include <netinet/in.h>
#include <string.h>  
#include <sys/wait.h>
#include <sys/socket.h>
#include "utils.h"

#define PORT 8080
#define MAX_CLIENTS 5

void runServer(pthread_t logger_pid);

void *runHandler(void *params);

void *runLogger(void *params);

int createSocket(struct sockaddr_in *server_addr, socklen_t socklen);

http_response_t handleHTTPrequest(std::string request, handlerParam param);

void parseHTTPrequest(std::string request, http_request_t &http_request);

void handleGETrequest(std::string uri, int server_sock, http_response_t *http_response);

std::string cleanHttpVersion(std::string httpVersion);

int main() {
    int result = pipe(pipefds);

    if(result < 0) {
        perror("Pipe failed");
        return 1;
    }
    // create logger
    pthread_t logger_pid;
    pthread_create(&logger_pid, 0, runLogger, NULL);

    // create the server
    runServer(logger_pid);

    pthread_mutex_destroy(&ioMutex);
    pthread_mutex_destroy(&pipeMutex);

    return 0;
}

void runServer(pthread_t logger_pid) {
    struct sockaddr_in server_addr{};
    socklen_t socklen = sizeof(server_addr);
    int addrlen = sizeof(server_addr);

    int server_sock = createSocket(&server_addr, socklen);
    writeToPipe("Listening on port " + std::to_string(PORT) + "\n");

    while(true) {
        int client_sock = accept(server_sock, (struct sockaddr *)&server_addr, (socklen_t*)&addrlen);
        if(client_sock < 0) {
            // send exit msg to logger and wait for it
            writeToPipe(EXIT_MSG);
            break;
        }
        writeToPipe("Accepted connection on socket " + std::to_string(client_sock) + "\n");
        handlerParam param = { server_sock, client_sock };
        pthread_t handlerId;
        pthread_create(&handlerId, 0, runHandler, (void *)&param);
    }
    // wait for logger to finish
    pthread_join(logger_pid, NULL);
}

void *runHandler(void *params) {
    handlerParam *param = (handlerParam *)params;
    int client_sock = param->client_sock;
    char buffer[BUF_SIZE] = {};

    // receive message
    ssize_t bytes_read = read(client_sock, buffer, BUF_SIZE);
    if(bytes_read <= 0) {
        writeToPipe("Error reading from connection " + std::to_string(client_sock));
        pthread_exit(nullptr);
    }

    std::string request(buffer, bytes_read);
    http_response_t response = handleHTTPrequest(request, *param);
    std::string HTTPresponse = response.to_string();
    writeToPipe(HTTPresponse);

    send(client_sock, HTTPresponse.c_str(), HTTPresponse.length(), 0);

    std::string body = response.body;
    if(!body.empty()) {
        send(client_sock, body.c_str(), body.length(), 0);
    }
    // close socket
    close(client_sock);

    pthread_exit(0);
}

void *runLogger(void *params) {
    printHelper("Logger pid: " + std::to_string(getpid()) 
        + " tid: " + std::to_string(get_tid_xplat()));
    bool stopReading = false;
    while(!stopReading) {
        stopReading = readFromPipe();
    }
    close(pipefds[0]);
    close(pipefds[1]);
    pthread_exit(0);
}

int createSocket(struct sockaddr_in *server_addr, socklen_t socklen) {
    int server_sock;
    // create server socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(server_sock == 0) {
        perror("Failed to create server socket");
        exit(EXIT_FAILURE);
    }

    server_addr->sin_family = AF_INET; // domain, assume over the internet using IPV4
    server_addr->sin_addr.s_addr = INADDR_ANY; // IP address, in this case is localhost
    server_addr->sin_port = htons(PORT); // socket port number
    // need to use htons to make sure the marshalling is in the correct byte order

    // we need to bind the address to the server socket
    int bind_result = bind(server_sock, (struct sockaddr *)server_addr, socklen);
    if(bind_result < 0) {
        perror("Failed to bind the server to address");
        exit(EXIT_FAILURE);
    }

    int listen_result = listen(server_sock, MAX_CLIENTS);
    if(listen_result < 0) {
        perror("Failed to limit listen to " + MAX_CLIENTS);
        exit(EXIT_FAILURE);
    }

    return server_sock;
}

http_response_t handleHTTPrequest(std::string request, handlerParam param) {
    http_request_t http_request;
    http_response_t http_response;
    parseHTTPrequest(request, http_request);
    writeToPipe("Read " + std::to_string(request.length()) + " bytes: " + http_request.to_string() + "\n");

    // handle based on the HTTP verb
    switch(http_request.verb) {
        case HttpRequestType::GET:
            handleGETrequest(http_request.uri, param.server_sock, &http_response);
            break;
        default:
            std::string errorMessage = "Unknown HTTP request " + stringToHttpRequest[http_request.verb] + "\n";
            writeToPipe(errorMessage);
            buildFailureHTTPresponse(&http_response, errorMessage);
            break;
    }

    return http_response;
}

void parseHTTPrequest(std::string request, http_request_t &http_request) {
    printHelper("Request: " + request);
    std::vector<std::string> tokens = split(request, '\n');

    std::vector<std::string> requestLine = split(tokens[0], ' ');
    if(requestLine.size() == 3) {
        http_request.uri = requestLine[1];
    } else {
        http_request.uri = "/";
    }
    http_request.verb    = httpRequestToString[requestLine[0]];
    http_request.version = cleanHttpVersion(requestLine[2]);
    // handle headers
     for(int i = 1; i < tokens.size(); i++) {
         std::vector<std::string> kvp = split(trim(tokens[i]), ':', false);
         if(kvp.size() > 1) {
             http_header_t header;
             header.key = trim(kvp[0]);
             header.value = trim(kvp[1]);
             http_request.headers.push_back(header);
         }
     }
}

void handleGETrequest(std::string uri, int server_sock, http_response_t *http_response) {
    // we'll render something even if the request uri is empty
    if(uri.empty() || uri == "/") {
        uri = DEFAULT_URI;
    }
    std::ifstream file("." + uri, std::ifstream::binary);
    bool failure = file.fail();
    if(failure) {
        buildFailureHTTPresponse(http_response, "File not found");
        http_response->body = "GET " + uri + " failed";
        http_header_t content_type = {
                "Content-Type",
                getURIContentType(".txt")
        };
        http_header_t content_length = {
                "Content-Length",
                std::to_string(http_response->body.length())
        };
        http_response->headers.push_back(content_type);
        http_response->headers.push_back(content_length);

        if (uri == QUIT_SERVER) {
            shutdown(server_sock, SHUT_RDWR);
        }
    } else {
        // get file size
        file.seekg(0, file.end);
        int file_size = file.tellg();
        file.seekg(0, file.beg);
        // build response header
        http_header_t content_type = {
                "Content-Type",
                getURIContentType(uri)
        };
        http_header_t content_length = {
                "Content-Length",
                std::to_string(file_size)
        };
        http_response->headers.push_back(content_type);
        http_response->headers.push_back(content_length);

        char* buffer = new char[file_size];
        if(!file.read(buffer, file_size)) {
            std::cout << "Error reading. Only read " << file.gcount() << std::endl;
        } else {
            std::string temp(buffer, file_size);
            http_response->body = temp;
        }
        delete[] buffer;

        // successfully handled the request
        buildSuccessfulHTTPresponse(http_response);
    }
    file.close();
}

std::string cleanHttpVersion(std::string httpVersion) {
    httpVersion = trim(httpVersion);
    int versionLen = 3;

    std::string temp(httpVersion.end() - versionLen, httpVersion.end());
    return temp;
}