#include "Transceiver.h"

Transceiver::Transceiver(const char *portName, const char *ip, uint16_t portTcpRx, uint16_t portUdpTx, uint16_t portTcpTx)
    : portName(portName), ip(ip), portTcpRx(portTcpRx), portUdpTx(portUdpTx), portTcpTx(portTcpTx)
{
    controlVec[0] = controlVec[1] = controlVec[2] = 127;
    mapPoint[0] = mapPoint[1] = mapPoint[2] = 0;
}

void Transceiver::run()
{
    thread mtRxTcp(&Transceiver::rxControlTcp, this);
    thread mtTxUdp(&Transceiver::txVideoUdp, this);
    thread mtTxTcp(&Transceiver::txMapTcp, this);
    thread mtTalkSerial(&Transceiver::talkSerial, this);
    cout << "Started 4 communication threads" << endl;
    mtRxTcp.join();
    mtTxUdp.join();
    mtTxTcp.join();
    mtTalkSerial.join();
}

void Transceiver::rxControlTcp()
{
    int clientSocket = startTcp(ip, portTcpRx);
    char buffer[BUFFER_SIZE];
    while (true)
    {
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
    close(clientSocket);
    unique_lock<mutex> lock(mutexControl);
    controlVec[0] = controlVec[1] = controlVec[2] = 127;
}

void Transceiver::txVideoUdp()
{
    int serverSocket;
    sockaddr_in clientAddress;
    socklen_t clientSize;
    tie(serverSocket, clientAddress, clientSize) = startUdp(ip, portUdpTx);

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
        param[1] = 80; //default(95) 0-100
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
    close(serverSocket);
}

void Transceiver::txMapTcp()
{
    int clientSocket = startTcp(ip, portTcpTx);
    char buffer[BUFFER_SIZE];
    float prevMapPoint[3] = {100, 100, 100}; // some unrealistic value for first iteration check failure
    while (true)
    {
        memset(buffer, 0, BUFFER_SIZE);

        unique_lock<mutex> lock(mutexMap);
        if (mapPoint[0] != prevMapPoint[0] || mapPoint[1] != prevMapPoint[1] || mapPoint[2] != prevMapPoint[2])
        {
            send(clientSocket, mapPoint, 12, 0);
            // printf("Sent map point [%f, %f, %f] to UI\n", mapPoint[0], mapPoint[1], mapPoint[2]);
            memcpy(prevMapPoint, mapPoint, 12);
            // this_thread::sleep_for(50ms);
        }
    }
    close(clientSocket);
}

void Transceiver::talkSerial()
{

    float feedbackPos[3] = {0, 0, 0};
    char n = '\n';
    int bytesSent = 0;
    int bytesRead = 0;

    const int bufferSize = 14;
    char buffer[bufferSize] = {0};
    uint8_t readChecksum;
    uint8_t calcChecksum;

    int fd = openSerialPort(portName);
    configSerialPort(fd);
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
            // printf("Sent control [%i, %i, %i] to Arduino\n", controlVec[0], controlVec[1], controlVec[2]);
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
                    // printf("Got map point [%f, %f, %f] from Arduino\n", mapPoint[0], mapPoint[1], mapPoint[2]);
                }
            }
        }
    }
}

int Transceiver::startTcp(const char *ip, uint16_t port)
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
    serverAddress.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serverAddress.sin_addr);

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
    return clientSocket;
}

tuple<int, sockaddr_in, socklen_t>
Transceiver::startUdp(const char *ip, uint16_t port)
{
    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket == -1)
    {
        cerr << "Error: Can't create a socket" << endl;
    }

    // Bind the ip address and port to a socket
    sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serverAddress.sin_addr);

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
    return make_tuple(serverSocket, clientAddress, clientSize);
}

int Transceiver::openSerialPort(const char *port)
{
    int fd;                                        // file descriptor for the port
    fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY); // Open for reading and writing, not as controlling terminal, no delay (no sleep - keep awake) (see Source 1)
    if (fd == -1)
    {
        perror("\nError: could not open specified port\n");
    }
    else
    {
        fcntl(fd, F_SETFL, 0); // Manipulates the file descriptor ands sets status FLAGS to 0 (see Source 4)
                               // Block (wait) until characters come in or interval timer expires
    }
    return (fd);
}

void Transceiver::configSerialPort(int fd)
{
    struct termios options;
    tcgetattr(fd, &options); // Fills the termios structure with the current serial port configuration

    // Change the current settings to new values

    // Set baud rate
    cfsetispeed(&options, BAUDRATE); // Input speed (rate)  -- Most systems don't support different input and output speeds so keep these the same for portability
    cfsetospeed(&options, BAUDRATE); // Output speed (rate)

    // Enable the receiver and set as local mode - CLOCAL & CREAD should always be enabled.
    // CLOCAL so that program does not become owner of the port
    // CREAD so that the serial interface will read incoming data
    options.c_cflag |= (CLOCAL | CREAD);

    // Set other options to match settings on Windows side of comm (No parity, 1 stop bit) - See Source 1
    options.c_cflag &= ~PARENB;                         // PARITY NOT ENABLED
    options.c_cflag &= ~CSTOPB;                         // CSTOPB = 2 stop bits (1 otherwise). Therefore ~CSTOPB means 1 stop bit
    options.c_cflag &= ~CSIZE;                          // Mask the character size to be in bits
    options.c_cflag |= CS8;                             // Use 8 bit data (per character)
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // Set the port for RAW input with no echo or other input signals
    options.c_oflag &= ~OPOST;                          // Use RAW output mode
    options.c_cc[VMIN] = 0;                             // Min # of chars to read. When 0 VTIME specifies the wait time for every character
    options.c_cc[VTIME] = 1;                            // Time in 1/10ths of a second to wait for every character before timing out.
                                                        // If VTIME is set to 0 then reads will block forever (port will only read)
                                                        // 24 Second timeout
    // Apply the new options to the port
    tcsetattr(fd, TCSANOW, &options); // TCSANOW makes changes without waiting for data to complete
}

uint8_t Transceiver::crc8(uint8_t *p, uint8_t len)
{
    uint16_t i;
    uint16_t crc = 0x0;

    while (len--)
    {
        i = (crc ^ *p++) & 0xFF;
        crc = (crc_table[i] ^ (crc << 8)) & 0xFF;
    }

    return crc & 0xFF;
}