#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include "opencv2/opencv.hpp"
#include "serial_utils.h"
#include <mutex>

// #define SERVER_IP "192.168.0.119"
#define SERVER_IP "127.0.0.1"
#define TCP_CONTROL_SERVER_PORT 10001
#define UDP_VIDEO_SERVER_PORT 10002
#define TCP_MAP_SERVER_PORT 10003
#define BUFFER_SIZE 1024
#define TIMEOUT 10

using namespace std;
using namespace cv;

std::mutex mutexControl;
uint8_t controlVec[3] = {127, 127, 127}; // means zero velocity

std::mutex mutexMap;
float mapPoint[3] = {0, 0, 0};

void udp_tx()
{

    // Create a socket
    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket == -1)
    {
        cerr << "Error: Can't create a socket" << endl;
    }

    // Bind the ip address and port to a socket
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(UDP_VIDEO_SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddress.sin_addr);

    bind(serverSocket, (sockaddr *)&serverAddress, sizeof(serverAddress));

    // Get init message from client
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    sockaddr_in clientAddress;
    socklen_t clientSize = sizeof(clientAddress);
    int bytesReceived = recvfrom(serverSocket, buffer, BUFFER_SIZE, 0, (sockaddr *)&clientAddress, &clientSize);
    if (bytesReceived == -1)
    {
        cerr << "Error in recvfrom()" << endl;
    }
    cout << "Received init message -> starting video transmission" << endl;

    VideoCapture cap;
    if (!cap.open(0))
    {
        cerr << "Error: Can't open camera" << endl;
    }
    Mat imgFlipped;
    Mat img;
    while (true)
    {
        cap >> imgFlipped;
        flip(imgFlipped, img, -1);
        if (img.empty())
            break;

        vector<uchar> imgBuffer;
        std::vector<int> param(2);
        param[0] = cv::IMWRITE_JPEG_QUALITY;
        param[1] = 50; //default(95) 0-100
        imencode(".jpg", img, imgBuffer, param);
        int imgBufferSize = imgBuffer.size();
        // cout << "Encoded image size: " << imgBufferSize << endl;

        int bytesSent = sendto(serverSocket, &imgBuffer[0], imgBufferSize, 0, (sockaddr *)&clientAddress, clientSize);
        if (bytesSent == -1)
        {
            cout << "Error sending image" << endl;
        }
        // imshow("TRANSMITTING", img);
        if (waitKey(TIMEOUT) >= 0)
            break;
    }

    // Close the socket
    close(serverSocket);
}

void tcp_rx()
{
    // Create a socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        cerr << "Error: Can't create a socket" << endl;
    }

    // Bind the ip address and port to a socket
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(TCP_CONTROL_SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddress.sin_addr);

    bind(serverSocket, (sockaddr *)&serverAddress, sizeof(serverAddress));

    // Tell the socket is for listening
    listen(serverSocket, SOMAXCONN);

    // Wait for a connection
    sockaddr_in clientAddress;
    socklen_t clientSize = sizeof(clientAddress);
    int clientSocket = accept(serverSocket, (sockaddr *)&clientAddress, &clientSize);

    char hostName[NI_MAXHOST];   // Client's remote name
    char clientPort[NI_MAXSERV]; // Service (i.e. port) the client is connect on

    memset(hostName, 0, NI_MAXHOST);
    memset(clientPort, 0, NI_MAXSERV);

    if (getnameinfo((sockaddr *)&clientAddress, sizeof(clientAddress), hostName, NI_MAXHOST, clientPort, NI_MAXSERV, 0) == 0)
    {
        cout << hostName << " connected on port " << clientPort << endl;
    }
    else
    {
        inet_ntop(AF_INET, &clientAddress.sin_addr, hostName, NI_MAXHOST);
        cout << hostName << " connected on port " << ntohs(clientAddress.sin_port) << endl;
    }

    // Close listening (server) socket
    close(serverSocket);

    char buffer[BUFFER_SIZE];
    while (true)
    {
        // Wait for client to send data
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
        if (bytesReceived == -1)
        {
            cerr << "Error in recv()" << endl;
            break;
        }

        if (bytesReceived == 0)
        {
            cout << "Client disconnected " << endl;
            break;
        }
        // cout << "Received " << bytesReceived << "bytes" << endl;

        if (bytesReceived == 3)
        {
            unique_lock<mutex> lock(mutexControl);
            memcpy(controlVec, buffer, 3);
            // printf("Got [%i, %i, %i] from TCP\n", controlVec[0], controlVec[1], controlVec[2]);
        }
    }

    // Close client socket
    close(clientSocket);
    unique_lock<mutex> lock(mutexControl);
    controlVec[0] = 127;
    controlVec[1] = 127;
    controlVec[2] = 127;
}

void tcp_tx()
{
    // Create a socket
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        cerr << "Error: Can't create a socket" << endl;
    }

    // Bind the ip address and port to a socket
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(TCP_MAP_SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddress.sin_addr);

    bind(serverSocket, (sockaddr *)&serverAddress, sizeof(serverAddress));

    // Tell the socket is for listening
    listen(serverSocket, SOMAXCONN);

    // Wait for a connection
    sockaddr_in clientAddress;
    socklen_t clientSize = sizeof(clientAddress);
    int clientSocket = accept(serverSocket, (sockaddr *)&clientAddress, &clientSize);

    char hostName[NI_MAXHOST];   // Client's remote name
    char clientPort[NI_MAXSERV]; // Service (i.e. port) the client is connect on

    memset(hostName, 0, NI_MAXHOST);
    memset(clientPort, 0, NI_MAXSERV);

    if (getnameinfo((sockaddr *)&clientAddress, sizeof(clientAddress), hostName, NI_MAXHOST, clientPort, NI_MAXSERV, 0) == 0)
    {
        cout << hostName << " connected on port " << clientPort << endl;
    }
    else
    {
        inet_ntop(AF_INET, &clientAddress.sin_addr, hostName, NI_MAXHOST);
        cout << hostName << " connected on port " << ntohs(clientAddress.sin_port) << endl;
    }

    // Close listening (server) socket
    close(serverSocket);

    char buffer[BUFFER_SIZE];

    // tx data
    while (true)
    {
        memset(buffer, 0, BUFFER_SIZE);

        unique_lock<mutex> lock(mutexMap);
        send(clientSocket, mapPoint, 12, 0);
    }

    // Close client socket
    close(clientSocket);
}

void serial_talk()
{
    const char *portName = "/dev/ttyACM0";
    float feedbackPos[3] = {0, 0, 0};
    char n = '\n';
    int bytesSent = 0;
    int bytesRead = 0;

    const int bufferSize = 14;
    char buffer[bufferSize] = {0};
    uint8_t readChecksum;
    uint8_t calcChecksum;

    int fd = openPort(portName);
    configPort(fd);
    srand(static_cast<unsigned>(time(0)));

    while (1)
    {
        //tx
        {
            unique_lock<mutex> lock(mutexControl);
            calcChecksum = crc8((uint8_t *)controlVec, 3);
            bytesSent = 0;
            bytesSent += write(fd, controlVec, 3);
            bytesSent += write(fd, &calcChecksum, 1);
            bytesSent += write(fd, &n, 1);
            // printf("Just sent %i bytes to Arduino\n", bytesSent);
        }

        //rx
        for (int i = 0; i < 14; i++)
        {
            bytesRead = read(fd, buffer + i, 1);
        }

        if (buffer[13] == '\n')
        {
            memcpy(feedbackPos, buffer, 12);
            readChecksum = buffer[12];
            calcChecksum = crc8((uint8_t *)feedbackPos, 12);
            bool isPassed = readChecksum == calcChecksum;
            if (isPassed)
            {
                unique_lock<mutex> lock(mutexMap);
                if (mapPoint[0] != feedbackPos[0] || mapPoint[1] != feedbackPos[1] || mapPoint[2] != feedbackPos[2])
                {
                    memcpy(mapPoint, feedbackPos, 12);
                }
            }
        }
    }
}

int main()
{
    thread tcp_rx_thread(tcp_rx);
    thread udp_tx_thread(udp_tx);
    thread tcp_tx_thread(tcp_tx);
    thread serial_thread(serial_talk);
    cout << "TCP, UDP and Serial threads started" << endl;
    tcp_rx_thread.join();
    udp_tx_thread.join();
    tcp_tx_thread.join();
    serial_thread.join();
    return 0;
}