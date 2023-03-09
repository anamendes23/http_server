/**
 * @file utils.h
 * @author Ana Mendes
 * @brief utility functions, structs, global variables and definitions
 * @version 0.1
 * @date 2023-01-29
 * @see "Seattle University, cpsc5042, Winter 2023"
 * 
 * @copyright Copyright (c) 2023
 * 
 */

#include <cstdio>
#include <iostream>
#include <map>
#include <pthread.h>
#include <stdio.h>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

#define DEFAULT_URI "/index.html"
#define QUIT_SERVER "/quit"
#define EXIT_MSG "Shutting down.\n"
#define EXIT_MSG_SZ 16
#define BUF_SIZE 1024
#define gettid() syscall(SYS_gettid)

const char* ws = " \t\n\r\f\v";
int pipefds[2]; // index 0 is read, index 1 is write
pthread_mutex_t pipeMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ioMutex = PTHREAD_MUTEX_INITIALIZER;

enum HttpRequestType {
    GET,
    PUT,
    DELETE,
    POST
};

std::map<std::string, HttpRequestType> httpRequestToString = {
    { "GET", HttpRequestType::GET },
    { "PUT", HttpRequestType::PUT },
    { "DELETE", HttpRequestType::DELETE },
    { "POST", HttpRequestType::POST }
};

std::map<HttpRequestType, std::string> stringToHttpRequest = {
    { HttpRequestType::GET, "GET" },
    { HttpRequestType::PUT, "PUT" },
    { HttpRequestType::DELETE, "DELETE" },
    { HttpRequestType::POST, "POST" }
};

std::map<std::string, int> httpErrorCode = {
    { "SUCCESS", 200},
    { "FAILURE", 404}
};

std::map<std::string, std::string> contentType = {
        {"png", "image/png"},
        {"css", "text/css"},
        {"js", "text/javascript"},
        {"html", "text/html"},
        {"txt", "text/plaintext"}
};

typedef struct {
    int server_sock;
    int client_sock;
    std::string to_string() const {
        return std::to_string(client_sock);
    }
} handlerParam;

typedef struct {
    std::string key;
    std::string value;
    std::string to_string() const {
        return key + ": " + value;
    }
} http_header_t;

typedef struct {
    HttpRequestType verb;
    std::string version;
    std::string uri;
    std::vector<http_header_t> headers;
    std::string to_string() const {
        std::ostringstream sso;
        sso << stringToHttpRequest[verb] << " " << uri << " version " << version;
        for(http_header_t header : headers) {
            sso << " " << header.to_string();
        }
        return sso.str();
    }
} http_request_t;

typedef struct {
    std::string version = "1.0";
    int status_code;
    std::string status_txt;
    std::string body;
    std::vector<http_header_t> headers;
    std::string to_string() const {
        std::ostringstream sso;
        sso << "HTTP/" << version << " " << status_code << " " << status_txt << "\r\n";
        for(http_header_t header : headers) {
            sso << header.to_string() << "\r\n";
        }
        sso << "\r\n";
        return sso.str();
    }
} http_response_t;

bool foundEndMessage(char buffer[BUF_SIZE], int bytes_read) {
    int buffer_index = 0;

    while((bytes_read - buffer_index) >= EXIT_MSG_SZ) {
        if(buffer[buffer_index] == EXIT_MSG[0]) {
            for(int i = 1; i < EXIT_MSG_SZ; i++) {
                buffer_index++;
                if(buffer[buffer_index] != EXIT_MSG[i]) {
                    break;
                }
                else if(i == EXIT_MSG_SZ - 1) {
                    return true;
                }
            }
        }
        buffer_index++;
    }

    return false;
}

void printHelper(std::string message) {
    pthread_mutex_lock(&ioMutex);
    std::cout << message << std::endl;
    pthread_mutex_unlock(&ioMutex);
}

void printBuffer(char buffer[BUF_SIZE], int bytes_read) {
    int index = 0;
    pthread_mutex_lock(&ioMutex);
    while(index < bytes_read) {
        std::cout << buffer[index];
        index++;
    }
    pthread_mutex_unlock(&ioMutex);
}

bool readFromPipe() {
    char buffer[BUF_SIZE];
    ssize_t bytes_read = read(pipefds[0], buffer, sizeof(buffer));
    printBuffer(buffer, bytes_read);
    return foundEndMessage(buffer, bytes_read);
}

void writeToPipe(std::string message) {
    int size = message.length();
    pthread_mutex_lock(&pipeMutex);
    write(pipefds[1], message.c_str(), size + 1);
    pthread_mutex_unlock(&pipeMutex);
}

long get_tid_xplat() {
#ifdef __APPLE__
    long   tid;
    pthread_t self = pthread_self();
    int res = pthread_threadid_np(self, reinterpret_cast<__uint64_t *>(&tid));
    return tid;
#else
    pid_t tid = gettid();
    return (long) tid;
#endif
}

std::vector<std::string> split(std::string& src, char delim, bool recurse = true) {
    std::istringstream ss(src);
    std::vector<std::string> res;

    std::string piece;

    while(getline(ss, piece, delim)) {
        res.push_back(piece);
        if(!recurse) {
            delim = '\0';
        }
    }

    return res;
}

std::string getURIContentType(std::string uri) {
    std::vector<std::string> tokens = split(uri, '.');
    std::string fileFormat = tokens[tokens.size() - 1];
    return contentType[fileFormat];
}

void buildSuccessfulHTTPresponse(http_response_t *http_response) {
    http_response->status_code = httpErrorCode["SUCCESS"];
    http_response->status_txt = "OK";
}

void buildFailureHTTPresponse(http_response_t *http_response, std::string message) {
    http_response->status_code = httpErrorCode["FAILURE"];
    http_response->status_txt = message;
}

/**
 * Functions below from Stackoverflow
 * https://stackoverflow.com/questions/216823/how-to-trim-an-stdstring
 */
// trim from end of string (right)
std::string& rtrim(std::string& s, const char* t = ws)
{
    s.erase(s.find_last_not_of(t) + 1);
    return s;
}

// trim from beginning of string (left)
std::string& ltrim(std::string& s, const char* t = ws)
{
    s.erase(0, s.find_first_not_of(t));
    return s;
}

// trim from both ends of string (right then left)
std::string& trim(std::string& s, const char* t = ws)
{
    return ltrim(rtrim(s, t), t);
}
