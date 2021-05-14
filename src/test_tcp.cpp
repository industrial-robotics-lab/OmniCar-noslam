#include <iostream>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>

#define SERVER_IP "127.0.0.1"
#define TCP_SERVER_PORT 10001
#define BUFFER_SIZE 1024
#define TIMEOUT 10

using namespace std;

uint8_t controlVec[3] = {127, 127, 127};

int main()
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
            memcpy(controlVec, buffer, 12);
            printf("Got [%i, %i, %i] from TCP\n", controlVec[0], controlVec[1], controlVec[2]);
        }
    }

    // Close client socket
    close(clientSocket);
}