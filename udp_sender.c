#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib") 

    typedef SOCKET MySocket;
    #define BAD_SOCKET INVALID_SOCKET
    
    void init_network_lib() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    
    void close_network_lib() {
        WSACleanup();
    }
    
    void close_socket(MySocket s) {
        closesocket(s);
    }

#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netinet/in.h>

    typedef int MySocket;
    #define BAD_SOCKET -1

    void init_network_lib() {}
    void close_network_lib() {}
    void close_socket(MySocket s) { close(s); }
#endif

struct sockaddr_in g_server_addr;

MySocket create_udp_socket(const char* ip, int port) {
    MySocket s = socket(AF_INET, SOCK_DGRAM, 0);
    
    if (s == BAD_SOCKET) {
        printf("Can't create socket\n");
        return BAD_SOCKET;
    }

    memset(&g_server_addr, 0, sizeof(g_server_addr));
    g_server_addr.sin_family = AF_INET;
    g_server_addr.sin_port = htons(port);
    
#ifdef _WIN32
    g_server_addr.sin_addr.s_addr = inet_addr(ip);
#else
    inet_pton(AF_INET, ip, &g_server_addr.sin_addr);
#endif

    return s;
}

void send_udp_message(MySocket s, const char* msg) {
    int sent_bytes = sendto(s, msg, strlen(msg), 0, 
                           (struct sockaddr*)&g_server_addr, sizeof(g_server_addr));
                           
    if (sent_bytes < 0) {
        printf("Failed to send UDP packet\n");
    } else {
        printf("Sent to server: %s\n", msg);
    }
}

#ifdef _WIN32
    #include <windows.h>

    typedef HANDLE MyPort;
    #define BAD_PORT INVALID_HANDLE_VALUE
    #define PAUSE(ms) Sleep(ms)

    MyPort connect_port(const char* name) {
        HANDLE h = CreateFileA(name,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            0,
            NULL);

        if(h == BAD_PORT) {
            return BAD_PORT;
        }
        
        DCB cfg = {0};
        cfg.DCBlength = sizeof(cfg);
        GetCommState(h, &cfg);
        cfg.BaudRate = CBR_9600;
        cfg.ByteSize = 8;
        cfg.StopBits = ONESTOPBIT;
        cfg.Parity = NOPARITY;
        SetCommState(h, &cfg);

        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        SetCommTimeouts(h, &timeouts);

        return h;
    }

    int read_data(MyPort p, char* buffer, int size) {
        unsigned long read;
        
        if(!ReadFile(p, buffer, size, &read, NULL)) {
            return 0;
        }
        return (int)read;
    }

    void disconnect(MyPort p) {
        CloseHandle(p);
    }

#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <termios.h>

    typedef int MyPort;
    #define BAD_PORT -1
    #define PAUSE(ms) usleep((ms) * 1000)

    MyPort connect_port(const char* name) {
        int id = open(name, O_RDWR | O_NOCTTY | O_NDELAY);

        if(id == -1) {
            return BAD_PORT;
        }

        struct termios cfg;
        
        tcgetattr(id, &cfg);
        cfsetispeed(&cfg, B9600);
        cfsetospeed(&cfg, B9600);

        cfg.c_cflag &= ~PARENB;
        cfg.c_cflag &= ~CSTOPB;
        cfg.c_cflag &= ~CSIZE;
        cfg.c_cflag |= CS8;
        cfg.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

        tcsetattr(id, TCSANOW, &cfg);
        return id;
    }

    int read_data(MyPort p, char* buffer, int size) {
        return read(p, buffer, size); 
    }

    void disconnect(MyPort p) {
        close(p);
    }

#endif

int main(int argc, char* argv[]) {
    char* com_name = argv[1];
    char* srv_ip = argv[2];
    int srv_port = atoi(argv[3]);

    init_network_lib();
    MySocket sock = create_udp_socket(srv_ip, srv_port);
    if (sock == BAD_SOCKET) return 1;

    printf("Connecting to %s...\n", com_name);
    MyPort port = connect_port(com_name);
    if (port == BAD_PORT) {
        printf("Can't open COM port\n");
        return 1;
    }

    printf("Started. Forwarding data to %s:%d\n", srv_ip, srv_port);

    char str_buf[64];
    int pos = 0;

    while (1) {
        char chunk[1];
        if (read_data(port, chunk, 1) > 0) {
            
            if (chunk[0] != '\n' && pos < 63) {
                str_buf[pos++] = chunk[0];
                continue;
            }

            str_buf[pos] = '\0';
            pos = 0;

            send_udp_message(sock, str_buf);

        } else {
            PAUSE(10);
        }
    }

    disconnect(port);
    close_socket(sock);
    close_network_lib();
    return 0;
}
