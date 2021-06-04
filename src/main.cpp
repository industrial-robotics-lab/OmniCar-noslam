#include "Transceiver.h"

int main()
{
    Transceiver txrx(
        "/dev/ttyACM0",
        // "192.168.0.119", // Raspberry Pi
        "127.0.0.1", // localhost
        10001,
        10002,
        10003);
    txrx.run();
    return 0;
}