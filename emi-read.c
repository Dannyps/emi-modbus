#include <byteswap.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "MQTTClient.h"
#include "emi-read.h"

#define SERVER_ID 0x01

int main(int argc, char* argv[])
{
    modbus_t* ctx = NULL;
    int rc, mqttrc;
    int server_id = SERVER_ID;

    // UPDATE THE DEVICE NAME AS NECESSARY
    ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 2);
    if (ctx == NULL) {
        fprintf(stderr, "Could not connect to MODBUS: %s\n", modbus_strerror(errno));
        return -1;
    }

    rc = modbus_set_slave(ctx, server_id);
    if (rc == -1) {
        fprintf(stderr, "server_id=%d Invalid slave ID: %s\n", server_id, modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    MQTTClient client;
    mqttrc = MQTTClient_create(&client, argv[1], "emi-reader", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (mqttrc != 0) {
        fprintf(stderr, "invalid mqtt server name or not provided\n");
        return -1;
    }

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.keepAliveInterval = 10;
    conn_opts.cleansession = 1;
    conn_opts.username = argv[2];
    conn_opts.password = argv[3];
    mqttrc = MQTTClient_connect(client, &conn_opts);

    modbus_set_debug(ctx, FALSE);

    modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

    /* Define a new timeout of 50ms */
    modbus_set_response_timeout(ctx, 0, 80000);

    int con = modbus_connect(ctx);
    if (con == -1) {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    char* deviceId2 = getOctetString(ctx, 0x0003, 6);
    char* deviceId1 = getOctetString(ctx, 0x0002, 10);
    char* activeCoreFirmwareId = getOctetString(ctx, 0x0004, 5);
    char* activeAppFirmwareId = getOctetString(ctx, 0x0005, 5);
    char* activeComFirmwareId = getOctetString(ctx, 0x0006, 5);

    printf("Device ID 1 - Device Serial Number is %s.\n", deviceId1);
    // printf("Device ID 2 - Manufacturer Model Codes and Year is %s.\n", deviceId2);
    printf("EMI Active Core Firmware Id is %s.\n", activeCoreFirmwareId);
    printf("EMI Active App Firmware Id is %s.\n", activeAppFirmwareId);
    printf("EMI Active Com Firmware Id is %s.\n", activeComFirmwareId);

    emi_clock_t* clock = getTime(ctx);
    printf("EMI clock time is %2d/%2d/%2d - %2d:%2d:%2d\n", clock->day, clock->month, clock->year, clock->hour, clock->minute, clock->second);

    mqttrc = _MQTTClient_publish(client, "emi/serialNumber", deviceId1, 10);

    char* activityCalendarActiveName = getOctetString(ctx, 0x0006, 6);
    printf("Activity Calendar - Active Name is %s\n", activityCalendarActiveName);

    double currentlyActiveTariff = getDoubleFromUInt16(ctx, 0x000b, 0);
    printf("Currently Active Tariff is %f\n", currentlyActiveTariff);

    while (TRUE) {
        double instVoltageL1 = getDoubleFromUInt16(ctx, 0x006c, -1);
        double instCurrentL1 = getDoubleFromUInt16(ctx, 0x006d, -1);
        double instActivePowerSum = getDoubleFromUInt32(ctx, 0x0079, 0);

        mqttrc = _MQTTClient_publishDouble(client, "emi/L1/voltage", instVoltageL1, 1);
        mqttrc = _MQTTClient_publishDouble(client, "emi/L1/current", instCurrentL1, 1);
        mqttrc = _MQTTClient_publishDouble(client, "emi/L1/activePower", instActivePowerSum, 0);

        printf("Wrote 3 messages to mqtt.\n");

        usleep(200000);
    }

    /* Close the connection */
    modbus_close(ctx);
    modbus_free(ctx);

    free(deviceId1);
    free(deviceId2);
    free(activeCoreFirmwareId);
    free(activeAppFirmwareId);
    free(activeComFirmwareId);
    free(activityCalendarActiveName);
    free(clock);

    return 0;
}

char* getOctetString(modbus_t* ctx, uint16_t registerAddress, uint8_t nb)
{
    char* string = malloc(nb + 1 * sizeof(char));
    string[nb] = 0; // set string terminator
    modbus_read_input_registers(ctx, registerAddress, 1, nb, string);
    return string;
}

double scaleInt(int num, int scaler)
{
    if (scaler == 0) {
        // No effect
        return num;
    } else {
        return num * pow(10, scaler);
    }
}

double getDoubleFromUInt16(modbus_t* ctx, uint16_t registerAddress, signed char scaler)
{
    uint16_t res;
    modbus_read_input_registers(ctx, registerAddress, 1, 2, &res);
    return scaleInt(__bswap_16(res), scaler);
}

double getDoubleFromUInt32(modbus_t* ctx, uint16_t registerAddress, signed char scaler)
{
    uint32_t res;
    modbus_read_input_registers(ctx, registerAddress, 1, 4, &res);
    return scaleInt(__bswap_32(res), scaler);
}

emi_clock_t* getTime(modbus_t* ctx)
{
    emi_clock_t* clock = malloc(1 * sizeof(emi_clock_t));
    modbus_read_input_registers(ctx, 0x0001, 1, sizeof(emi_clock_t), clock);
    clock->year = __bswap_16(clock->year);
    clock->deviation = __bswap_16(clock->deviation);
    return clock;
}

int _MQTTClient_publish(MQTTClient handle, const char* topicName, const void* payload, int payloadlen)
{
    return MQTTClient_publish(handle, topicName, payloadlen, payload, 1, 0, NULL);
}

int _MQTTClient_publishInt(MQTTClient handle, const char* topicName, int n)
{
    char* str = malloc(32);
    sprintf(str, "%d", n);
    int rc = MQTTClient_publish(handle, topicName, strlen(str), str, 1, 0, NULL);
    free(str);
    return rc;
}

int _MQTTClient_publishDouble(MQTTClient handle, const char* topicName, double n, uint8_t decimals)
{
    char* str = malloc(32);
    sprintf(str, "%.*f", decimals, n);
    int rc = MQTTClient_publish(handle, topicName, strlen(str), str, 1, 0, NULL);
    free(str);
    return rc;
}
