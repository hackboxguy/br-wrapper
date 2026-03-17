#include "CanReader.h"

#include <QDebug>
#include <cstring>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

// OBD2 CAN IDs
static const uint32_t OBD2_REQUEST_ID  = 0x7DF;
static const uint32_t OBD2_RESPONSE_ID = 0x7E8;
static const uint32_t TELLTALE_ID      = 0x420;

// OBD2 PIDs we poll
static const uint8_t PID_RPM          = 0x0C;
static const uint8_t PID_SPEED        = 0x0D;
static const uint8_t PID_COOLANT_TEMP = 0x05;
static const uint8_t PID_FUEL_LEVEL   = 0x2F;
static const uint8_t PID_BATTERY_VOLT = 0x42;

CanReader::CanReader(const QString &interface, QObject *parent)
    : QObject(parent)
    , m_interface(interface)
    , m_socket(-1)
    , m_thread(nullptr)
    , m_running(false)
{
}

CanReader::~CanReader()
{
    stop();
}

bool CanReader::open()
{
    m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        qWarning() << "Failed to create CAN socket:" << strerror(errno);
        return false;
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, m_interface.toLocal8Bit().constData(),
                 sizeof(ifr.ifr_name) - 1);

    if (ioctl(m_socket, SIOCGIFINDEX, &ifr) < 0) {
        qWarning() << "CAN interface" << m_interface << "not found:" << strerror(errno);
        ::close(m_socket);
        m_socket = -1;
        return false;
    }

    struct sockaddr_can addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(m_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        qWarning() << "Failed to bind CAN socket:" << strerror(errno);
        ::close(m_socket);
        m_socket = -1;
        return false;
    }

    // Set receive timeout to 100ms
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return true;
}

void CanReader::start()
{
    if (m_socket < 0)
        return;

    m_running = true;
    m_thread = new QThread();

    // Move to thread and run poll loop
    this->moveToThread(m_thread);
    connect(m_thread, &QThread::started, this, &CanReader::pollLoop);
    connect(m_thread, &QThread::finished, m_thread, &QThread::deleteLater);
    m_thread->start();
}

void CanReader::stop()
{
    m_running = false;
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(2000);
        m_thread = nullptr;
    }
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
}

void CanReader::sendObd2Request(uint8_t pid)
{
    struct can_frame frame;
    std::memset(&frame, 0, sizeof(frame));
    frame.can_id = OBD2_REQUEST_ID;
    frame.can_dlc = 8;
    frame.data[0] = 0x02;  // Length: 2 bytes follow
    frame.data[1] = 0x01;  // Mode 01: current data
    frame.data[2] = pid;

    if (write(m_socket, &frame, sizeof(frame)) < 0) {
        qWarning() << "CAN write failed:" << strerror(errno);
    }
}

bool CanReader::readFrame(struct can_frame *frame, int timeoutMs)
{
    Q_UNUSED(timeoutMs);  // Using SO_RCVTIMEO set in open()
    ssize_t nbytes = read(m_socket, frame, sizeof(struct can_frame));
    return (nbytes == sizeof(struct can_frame));
}

void CanReader::decodeObd2Response(const uint8_t *data)
{
    // data[0] = length, data[1] = 0x41 (response), data[2] = PID
    if (data[1] != 0x41)
        return;

    uint8_t pid = data[2];
    switch (pid) {
    case PID_RPM:
        emit rpmChanged(((int)data[3] << 8 | data[4]) / 4);
        break;
    case PID_SPEED:
        emit speedChanged(data[3]);
        break;
    case PID_COOLANT_TEMP:
        emit coolantTempChanged((int)data[3] - 40);
        break;
    case PID_FUEL_LEVEL:
        emit fuelLevelChanged(data[3] * 100 / 255);
        break;
    case PID_BATTERY_VOLT:
        emit batteryVoltageChanged(((int)data[3] << 8 | data[4]) / 1000.0);
        break;
    }
}

void CanReader::decodeTelltales(const uint8_t *data)
{
    quint16 bits = (quint16)data[0] | ((quint16)data[1] << 8);
    emit telltalesChanged(bits);
}

void CanReader::pollLoop()
{
    // PIDs to poll each cycle (speed + RPM every cycle)
    static const uint8_t fastPids[] = { PID_RPM, PID_SPEED };
    static const uint8_t slowPids[] = { PID_COOLANT_TEMP };
    static const uint8_t rarePids[] = { PID_FUEL_LEVEL, PID_BATTERY_VOLT };

    int cycle = 0;
    int noResponseCount = 0;

    while (m_running) {
        // Build list of PIDs to poll this cycle
        uint8_t pids[8];
        int pidCount = 0;

        // Fast PIDs every cycle
        for (size_t i = 0; i < sizeof(fastPids); i++)
            pids[pidCount++] = fastPids[i];

        // Slow PIDs every 2nd cycle
        if (cycle % 2 == 0) {
            for (size_t i = 0; i < sizeof(slowPids); i++)
                pids[pidCount++] = slowPids[i];
        }

        // Rare PIDs every 5th cycle
        if (cycle % 5 == 0) {
            for (size_t i = 0; i < sizeof(rarePids); i++)
                pids[pidCount++] = rarePids[i];
        }

        bool gotResponse = false;

        for (int i = 0; i < pidCount && m_running; i++) {
            sendObd2Request(pids[i]);

            // Read responses (may also get telltale frames)
            struct can_frame frame;
            // Try to read a few frames (response + any pending telltales)
            for (int attempt = 0; attempt < 3; attempt++) {
                if (!readFrame(&frame, 100))
                    break;

                if (frame.can_id == OBD2_RESPONSE_ID) {
                    decodeObd2Response(frame.data);
                    gotResponse = true;
                    break;  // Got the response for this PID
                } else if (frame.can_id == TELLTALE_ID) {
                    decodeTelltales(frame.data);
                    // Keep reading for the OBD2 response
                }
            }
        }

        if (gotResponse) {
            noResponseCount = 0;
        } else {
            noResponseCount++;
            if (noResponseCount >= 3) {
                emit canTimeout();
                noResponseCount = 0;
            }
        }

        cycle++;
        if (!m_running) break;
        QThread::msleep(50);
    }
}
