/// This software is distributed under the terms of the MIT License.
/// Copyright (c) 2020 Hugo A. Garcia
/// Author: Hugo A. Garcia <hugo.a.garcia@gmail.com>
/// Based on code by:
///     Pavel Kirienko <pavel.kirienko@zubax.com>
///     joan2937 <joan@abyz.me.uk>

#include <canard.h>
#include <canard_dsdl.h>
#include <pigpio.h>
#include <socketcan.h>
#include <stdio.h>
#include <string.h>

#include <time.h>

/* Ultrasound GPIO pins */

#define TRIGGER_PIN 18
#define ECHO_PIN 24

/* Message Subject ID's 
 * where ID = [0, 24575]
 * for mandatory Heartbeat Message;
 * for Ultrasound Message,
 * ref. Unregulated identifiers (both fixed and non-fixed).
 *      SpecificationRevision v1.0-alpha, sec. 5.1.1
 */
static const uint16_t HeartbeatSubjectID = 32085;
static const uint16_t UltrasoundMessageSubjectID = 1610;

// Memory management.
static void *canardAllocate(CanardInstance *const ins, const size_t amount)
{
    (void)ins;
    return malloc(amount);
}

static void canardFree(CanardInstance *const ins, void *const pointer)
{
    (void)ins;
    free(pointer);
}

/* Node heartbeat
 * ref. SpecificationRevision v1.0-alpha, sec. 5.3.2
 */
static void publishHeartbeat(CanardInstance *const canard, const uint32_t uptime)
{
    static CanardTransferID transfer_id;
    const uint8_t payload[7] = {
        (uint8_t)(uptime >> 0U),
        (uint8_t)(uptime >> 8U),
        (uint8_t)(uptime >> 16U),
        (uint8_t)(uptime >> 24U),
        0,
        0,
        0,
    };
    const CanardTransfer transfer = {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = HeartbeatSubjectID,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = transfer_id,
        .payload_size = sizeof(payload),
        .payload = &payload[0],
    };
    ++transfer_id;
    (void)canardTxPush(canard, &transfer);
}

/* Ultrasound node functions using the pigpio library
 * ref. http://abyz.me.uk/rpi/pigpio/index.html
 * ref. http://abyz.me.uk/rpi/pigpio/ex_sonar_ranger.html
 */
static void publishUltrasoundDistance(CanardInstance *const canard, const float distance)
{
    static CanardTransferID transfer_id;
    uint8_t payload[4] = {0, 0, 0, 0};

    //Serilize the distance
    canardDSDLSetF32(payload, 0, distance);

    const CanardTransfer transfer = {
        .priority = CanardPriorityNominal,
        .transfer_kind = CanardTransferKindMessage,
        .port_id = UltrasoundMessageSubjectID,
        .remote_node_id = CANARD_NODE_ID_UNSET,
        .transfer_id = transfer_id,
        .payload_size = 4, // sizeof(payload),
        .payload = &payload[0],
    };
    ++transfer_id;
    (void)canardTxPush(canard, &transfer);
}

void ultrasoundTrigger(void)
{
    gpioWrite(TRIGGER_PIN, PI_ON);
    gpioDelay(10);
    gpioWrite(TRIGGER_PIN, PI_OFF);
}

void ultrasoundEcho(int gpio, int level, uint32_t tick, void *canard_ins)
{
    static uint32_t startTick, firstTick = 0;

    int diffTick;
    double distanceCm;

    if (!firstTick)
        firstTick = tick;

    if (level == PI_ON)
    {
        startTick = tick;
    }
    else if (level == PI_OFF)
    {
        diffTick = tick - startTick;
        distanceCm = (diffTick / 2) * 0.0343;

        publishUltrasoundDistance(canard_ins, distanceCm);

        //Debugging
        //printf("%u %u\ ", tick - firstTick, diffTick);
        printf("%f \n", distanceCm);
    }
}

int initializaUltrasoundSensor(CanardInstance *const ins)
{
    if (gpioInitialise() < 0)
        return -1;

    gpioSetMode(TRIGGER_PIN, PI_OUTPUT);
    gpioWrite(TRIGGER_PIN, PI_OFF);
    gpioSetMode(ECHO_PIN, PI_INPUT);

    // update ultrasound at 20 Hz
    // timer #0, every 50ms, interrupt function
    gpioSetTimerFunc(0, 50, ultrasoundTrigger);

    // monitor ultrasound echos
    // pin, callback function, Canard instance
    gpioSetAlertFuncEx(ECHO_PIN, ultrasoundEcho, ins);
    return 0;
}

/*
 * MAIN 
 */
int main(const int argc, const char *const argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage:   %s <iface-name> <node-id>\n", argv[0]);
        fprintf(stderr, "Example: %s vcan0 42\n", argv[0]);
        return 1;
    }

    // Initialize the node with a static node-ID as specified in the command-line arguments.
    CanardInstance canard = canardInit(&canardAllocate, &canardFree);
    canard.mtu_bytes = CANARD_MTU_CAN_CLASSIC; // Do not use CAN FD to enhance compatibility.
    canard.node_id = (CanardNodeID)atoi(argv[2]);

    // Initialize a SocketCAN socket. Do not use CAN FD to enhance compatibility.
    const SocketCANFD sock = socketcanOpen(argv[1], false);
    if (sock < 0)
    {
        fprintf(stderr, "Could not initialize the SocketCAN interface: errno %d %s\n", -sock, strerror(-sock));
        return 1;
    }

    // Initialize ultrasound
    if (initializaUltrasoundSensor(&canard) < 0)
    {
        fprintf(stderr, "Could not initialize GPIO.");
        return 1;
    };

    // The main loop: publish messages and process service requests.
    const time_t boot_ts = time(NULL);
    time_t next_1hz_at = boot_ts;
    while (true)
    {
        if (next_1hz_at < time(NULL))
        {
            next_1hz_at++;
            publishHeartbeat(&canard, time(NULL) - boot_ts);
        }

        // Transmit pending frames.
        const CanardFrame *txf = canardTxPeek(&canard);
        while (txf != NULL)
        {
            (void)socketcanPush(sock, txf, 0); // Error handling not implemented
            canardTxPop(&canard);
            free((void *)txf);
            txf = canardTxPeek(&canard);
        }
    }
}
