#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <ctime>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

#if defined (WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET MySocket;
    #define BAD_SOCKET INVALID_SOCKET
    #define CLOSE_SOCK(s) closesocket(s)
    #define POLL_FUNC WSAPoll
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <poll.h>
    #include <signal.h>
    typedef int MySocket;
    #define BAD_SOCKET -1
    #define CLOSE_SOCK(s) close(s)
    #define POLL_FUNC poll
#endif

class DB {
public:
    sqlite3* db;

    DB() : db(nullptr) {}

    ~DB() {
        if (db) sqlite3_close(db);
    }

    bool Open(const char* filename) {
        if (sqlite3_open(filename, &db) != SQLITE_OK) {
            std::cout << "DB Error: Can't open database" << std::endl;
            return false;
        }


        const char* sql = "CREATE TABLE IF NOT EXISTS log (time INTEGER, temp REAL);";
        
        char* errMsg = 0;

        if (sqlite3_exec(db, sql, 0, 0, &errMsg) != SQLITE_OK) {
            std::cout << "DB Error: " << errMsg << std::endl;
            sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    void Insert(float temp) {
        char sql[256];
        sprintf(sql, "INSERT INTO log VALUES (%ld, %.2f);", (long)time(NULL), temp);

        char* errMsg = 0;
        if (sqlite3_exec(db, sql, 0, 0, &errMsg) != SQLITE_OK) {
            std::cout << "Insert Error: " << errMsg << std::endl;
            sqlite3_free(errMsg);
        } else {
            std::cout << "Saved to DB: " << temp << std::endl;
        }
    }

    std::string GetLastRecord() {
        sqlite3_stmt* stmt;
        const char* sql = "SELECT time, temp FROM log ORDER BY time DESC LIMIT 1;";
        
        std::string result = "No data yet";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                time_t t = (time_t)sqlite3_column_int64(stmt, 0);
                double temp = sqlite3_column_double(stmt, 1);
                
                char buf[100];
                struct tm* tm_info = localtime(&t);
                strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);
                
                std::stringstream ss;
                ss << "Time: " << buf << "- Temp: " << temp;
                result = ss.str();
            }
        }
        sqlite3_finalize(stmt);
        return result;
    }
    
    std::string GetHistoryHTML() {
        sqlite3_stmt* stmt;
        const char* sql = "SELECT time, temp FROM log ORDER BY time DESC LIMIT 10;";
        std::stringstream html;
        
        html << "<table border='1'><tr><th>Time</th><th>Temp</th></tr>";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                time_t t = (time_t)sqlite3_column_int64(stmt, 0);
                double temp = sqlite3_column_double(stmt, 1);
                
                char buf[100];
                struct tm* tm_info = localtime(&t);
                strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);

                html << "<tr><td>" << buf << "</td><td>" << temp << "</td></tr>";
            }
        }
        sqlite3_finalize(stmt);
        html << "</table>";
        return html.str();
    }
};

DB g_db;

class UdpListener {
public:
    MySocket sock;

    UdpListener() : sock(BAD_SOCKET) {}
    ~UdpListener() { if(sock != BAD_SOCKET) CLOSE_SOCK(sock); }

    bool Start(int port) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == BAD_SOCKET) return false;

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            std::cout << "UDP Bind failed" << std::endl;
            return false;
        }
        return true;
    }

    void Read() {
        char buf[256];
        int len = recv(sock, buf, sizeof(buf) - 1, 0);
        if (len > 0) {
            buf[len] = '\0';
            float temp = (float)atof(buf);
            g_db.Insert(temp);
        }
    }
};

class HttpServer {
public:
    MySocket sock;

    HttpServer() : sock(BAD_SOCKET) {}
    ~HttpServer() { if(sock != BAD_SOCKET) CLOSE_SOCK(sock); }

    bool Start(int port) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == BAD_SOCKET) return false;

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) return false;
        if (listen(sock, 5) != 0) return false;
        return true;
    }

    void ProcessClient() {
        MySocket client = accept(sock, NULL, NULL);
        if (client == BAD_SOCKET) return;

        char buf[1024];
        recv(client, buf, sizeof(buf), 0);

        std::stringstream body;
        body << "<html><head>"
             << "<meta http-equiv='refresh' content='1'>"
             << "<style>"
             << "body { font-family: sans-serif; text-align: center; background-color: #f4f4f9; }"
             << "h1 { color: #333; }"
             << "table { margin: 0 auto; border-collapse: collapse; width: 50%; box-shadow: 0 0 20px rgba(0,0,0,0.1); }"
             << "th, td { padding: 10px; text-align: center; border-bottom: 1px solid #ddd; }"
             << "th { background-color: #009879; color: white; }"
             << "tr:nth-child(even) { background-color: #f2f2f2; }"
             << "h2 { background-color: #fff; display: inline-block; padding: 10px; border-radius: 5px; }"
             << "</style>"
             << "</head>"
             << "<body>"
             << "<h1> Termometer Monitor</h1>"
             << "<h2>Current: " << g_db.GetLastRecord() << "</h2>"
             << "<h3>History Log</h3>"
             << g_db.GetHistoryHTML()
             << "</body></html>";

        std::stringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: text/html\r\n"
                 << "Content-Length: " << body.str().length() << "\r\n\r\n"
                 << body.str();

        send(client, response.str().c_str(), response.str().length(), 0);
        
        CLOSE_SOCK(client);
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: server <UDP_PORT> <HTTP_PORT>" << std::endl;
        return 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    if (!g_db.Open("data.db")) return 1;

    UdpListener udp;
    if (!udp.Start(atoi(argv[1]))) {
        std::cout << "Failed to start UDP" << std::endl;
        return 1;
    }

    HttpServer http;
    if (!http.Start(atoi(argv[2]))) {
        std::cout << "Failed to start HTTP" << std::endl;
        return 1;
    }

    std::cout << "UDP port: " << argv[1] << " HTTP port: " << argv[2] << std::endl;

    struct pollfd fds[2];
    
    fds[0].fd = udp.sock;
    fds[0].events = POLLIN;

    fds[1].fd = http.sock;
    fds[1].events = POLLIN;

    while (true) {
        int ret = POLL_FUNC(fds, 2, 1000);

        if (ret > 0) {
            if (fds[0].revents & POLLIN) {
                udp.Read();
            }
            if (fds[1].revents & POLLIN) {
                http.ProcessClient();
            }
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}