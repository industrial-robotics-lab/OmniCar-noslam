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

#define SERVER_IP "192.168.0.119"
// #define SERVER_IP "127.0.0.1"
#define TCP_SERVER_PORT 10001
#define UDP_SERVER_PORT 10002
#define BUFFER_SIZE 1024
#define TIMEOUT 10

using namespace std;
using namespace cv;

std::mutex mutexControl;
uint8_t controlVec[3] = {127, 127, 127}; // means zero velocity

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
    serverAddress.sin_port = htons(UDP_SERVER_PORT);
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
    Mat img;
    while (true)
    {
        cap >> img;
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
    serverAddress.sin_port = htons(TCP_SERVER_PORT);
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

    // While loop: accept and echo message back to client
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

        if (bytesReceived == 12)
        {
            unique_lock<mutex> lock(mutexControl);
            memcpy(controlVec, buffer, 12);
            printf("Got [%i, %i, %i] from TCP\n", controlVec[0], controlVec[1], controlVec[2]);
        }
    }

    // Close client socket
    close(clientSocket);
}

void serial_talk()
{
    const char *portName = "/dev/ttyACM0";
    float vec[3] = {0, 0, 0};
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
            readChecksum = buffer[0];
            memcpy(vec, buffer + 1, 12);
            calcChecksum = crc8((uint8_t *)vec, 12);
            bool isPassed = readChecksum == calcChecksum;
            if (isPassed)
            {
                // printf("Received accurate: [%f; %f, %f];\n\n", vec[0], vec[1], vec[2]);
            }
        }
    }
}

int main()
{
    thread tcp_thread(tcp_rx);
    thread udp_thread(udp_tx);
    thread serial_thread(serial_talk);
    cout << "TCP, UDP and Serial threads started" << endl;
    tcp_thread.join();
    tcp_thread.join();
    return 0;
}