#include <byteswap.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "MQTTClient.h"
#include "emi-read.h"
#include <systemd/sd-daemon.h>

#define SERVER_ID 0x01

double instVoltageL1, instCurrentL1, instActivePower, activeEnergyImport;
double instFrequency, instPowerFactor, currentApparentPowerThreshold;
double rate1ActiveEnergy, rate2ActiveEnergy, rate3ActiveEnergy, totalRateActiveEnergy;
modbus_t *ctx = NULL;
int rc, mqttrc;
MQTTClient client;
emi_clock_t *emiClock;

int main(int argc, char *argv[])
{
    // UPDATE THE DEVICE NAME AS NECESSARY
    ctx = modbus_new_rtu("/dev/ttyUSB0", 9600, 'N', 8, 2);
    if (ctx == NULL)
    {
        fprintf(stderr, "Could not connect to MODBUS: %s\n", modbus_strerror(errno));
        return -1;
    }

    rc = modbus_set_slave(ctx, SERVER_ID);
    if (rc == -1)
    {
        fprintf(stderr, "server_id=%d Invalid slave ID: %s\n", SERVER_ID, modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    mqttrc = MQTTClient_create(&client, argv[1], "emi-reader", MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (mqttrc != 0)
    {
        fprintf(stderr, "invalid mqtt server name or not provided\n");
        return -1;
    }

    //mqtt_connect(&client, argv);

    modbus_set_debug(ctx, FALSE);

    modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

    /* Define a new timeout of 50ms */
    modbus_set_response_timeout(ctx, 0, 200 * 1000);

    int con = modbus_connect(ctx);
    if (con == -1)
    {
        fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        return -1;
    }

    sd_notify(FALSE, "READY=1");

    // fire runHourly just once before entering the loop.
    runHourly();
    unsigned char hourlyLastRanAt = getCurrentHour();

    while (TRUE)
    {
        mqtt_connect(client, argv);
        runContinuously();

        if (getCurrentHour() != hourlyLastRanAt)
        {
            runHourly();
            hourlyLastRanAt = getCurrentHour();
        }
        mqtt_disconnect(client);
        usleep(5000 * 1000);
    }

    /* Close the connection */
    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}

void runContinuously()
{
    int localRc = 0;
    localRc += getDoubleFromUInt16(ctx, 0x006c, -1, &instVoltageL1);
    localRc += getDoubleFromUInt16(ctx, 0x006d, -1, &instCurrentL1);
    localRc += getDoubleFromUInt32(ctx, 0x0079, 0, &instActivePower);
    localRc += getDoubleFromUInt32(ctx, 0x0016, 0, &activeEnergyImport);
    localRc += getDoubleFromUInt16(ctx, 0x007F, -1, &instFrequency);
    localRc += getDoubleFromUInt16(ctx, 0x007B, -3, &instPowerFactor);
    localRc += getDoubleFromUInt32(ctx, 0x0026, 0, &rate1ActiveEnergy);
    localRc += getDoubleFromUInt32(ctx, 0x0027, 0, &rate2ActiveEnergy);
    localRc += getDoubleFromUInt32(ctx, 0x0028, 0, &rate3ActiveEnergy);
    localRc += getDoubleFromUInt32(ctx, 0x002C, 0, &totalRateActiveEnergy);

    if (localRc != 10)
    {
        // we should re-read;
        printf("read bad values. Expected 10, but got only %d successful reads.\n", localRc);
        return;
    }

    mqttrc = _MQTTClient_publishDouble(client, "emi/L1/voltage", instVoltageL1, 1);
    mqttrc = _MQTTClient_publishDouble(client, "emi/L1/activeEnergyImport", activeEnergyImport, 0);
    mqttrc = _MQTTClient_publishDouble(client, "emi/L1/current", instCurrentL1, 1);
    mqttrc = _MQTTClient_publishDouble(client, "emi/L1/activePower", instActivePower, 0);
    mqttrc = _MQTTClient_publishDouble(client, "emi/L1/frequency", instFrequency, 1);
    mqttrc = _MQTTClient_publishDouble(client, "emi/L1/powerFactor", instPowerFactor, 3);
    mqttrc = _MQTTClient_publishDouble(client, "emi/tariff/rate1ActiveEnergy", rate1ActiveEnergy, 0);
    mqttrc = _MQTTClient_publishDouble(client, "emi/tariff/rate2ActiveEnergy", rate2ActiveEnergy, 0);
    mqttrc = _MQTTClient_publishDouble(client, "emi/tariff/rate3ActiveEnergy", rate3ActiveEnergy, 0);
    mqttrc = _MQTTClient_publishDouble(client, "emi/tariff/totalRateActiveEnergy", totalRateActiveEnergy, 0);

    emiClock = getTime(ctx);
    char clockTime[64];
    sprintf(clockTime, "%02d-%02d-%02dT%02d:%02d:%02dZ\n", emiClock->year, emiClock->month, emiClock->day, emiClock->hour, emiClock->minute, emiClock->second);
    mqttrc = _MQTTClient_publishString(client, "emi/clockTime", clockTime);
    free(emiClock);
}

void runHourly()
{
    int localRc = 0;

    double currentlyActiveTariff;
    localRc += getDoubleFromUInt16(ctx, 0x000b, 0, &currentlyActiveTariff);
    char *activityCalendarActiveName = getOctetString(ctx, 0x0006, 6);
    char *deviceId2 = getOctetString(ctx, 0x0003, 6);
    char *deviceId1 = getOctetString(ctx, 0x0002, 10);
    char *activeCoreFirmwareId = getOctetString(ctx, 0x0004, 5);
    char *activeAppFirmwareId = getOctetString(ctx, 0x0005, 5);
    char *activeComFirmwareId = getOctetString(ctx, 0x0006, 5);

    localRc += getDoubleFromUInt32(ctx, 0x0012, -3, &currentApparentPowerThreshold);

    mqttrc = _MQTTClient_publishDouble(client, "emi/tariff/currentApparentPowerThreshold", currentApparentPowerThreshold, 2);
    mqttrc = _MQTTClient_publishDouble(client, "emi/currentlyActiveTariff", currentlyActiveTariff, 1);
    mqttrc = _MQTTClient_publishString(client, "emi/activityCalendarActiveName", activityCalendarActiveName);
    mqttrc = _MQTTClient_publishString(client, "emi/serialNumber", deviceId1);

    free(deviceId1);
    free(deviceId2);
    free(activeCoreFirmwareId);
    free(activeAppFirmwareId);
    free(activeComFirmwareId);
    free(activityCalendarActiveName);
}

char *getOctetString(modbus_t *ctx, uint16_t registerAddress, uint8_t nb)
{
    char *string = malloc(nb + 1 * sizeof(char));
    string[nb] = 0; // set string terminator
    int rc = modbus_read_input_registers(ctx, registerAddress, 1, nb, string);

    if (rc != 1)
    {
        string[0] = '\0';
    }

    return string;
}

double scaleInt(int num, int scaler)
{
    if (scaler == 0)
    {
        // No effect
        return num;
    }
    else
    {
        return num * pow(10, scaler);
    }
}

int getDoubleFromUInt16(modbus_t *ctx, uint16_t registerAddress, signed char scaler, double *res)
{
    uint16_t buffer;
    int rc = modbus_read_input_registers(ctx, registerAddress, 1, 2, &buffer);
    *res = scaleInt(__bswap_16(buffer), scaler);
    return rc;
}

int getDoubleFromUInt32(modbus_t *ctx, uint16_t registerAddress, signed char scaler, double *res)
{
    uint32_t buffer;
    int rc = modbus_read_input_registers(ctx, registerAddress, 1, 4, &buffer);
    *res = scaleInt(__bswap_32(buffer), scaler);
    return rc;
}

emi_clock_t *getTime(modbus_t *ctx)
{
    emi_clock_t *emiClock = malloc(1 * sizeof(emi_clock_t));
    int rc = modbus_read_input_registers(ctx, 0x0001, 1, sizeof(emi_clock_t), emiClock);
    if (rc != 0)
    {
    }
    emiClock->year = __bswap_16(emiClock->year);
    emiClock->deviation = __bswap_16(emiClock->deviation);
    return emiClock;
}

int _MQTTClient_publishInt(MQTTClient handle, const char *topicName, int n)
{
    char *str = malloc(32);
    sprintf(str, "%d", n);
    int rc = MQTTClient_publish(handle, topicName, strlen(str), str, 1, 0, NULL);
    free(str);
    return rc;
}

int _MQTTClient_publishDouble(MQTTClient handle, const char *topicName, double n, uint8_t decimals)
{
    char *str = malloc(32);
    sprintf(str, "%.*f", decimals, n);
    int rc = MQTTClient_publish(handle, topicName, strlen(str), str, 1, 0, NULL);
    free(str);
    return rc;
}

int _MQTTClient_publishString(MQTTClient handle, const char *topicName, char *str)
{
    return MQTTClient_publish(handle, topicName, strlen(str), str, 1, 0, NULL);
}

unsigned char getCurrentHour()
{
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    return timeinfo->tm_hour;
}

void mqtt_connect(MQTTClient client, char **argv)
{

    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    conn_opts.username = argv[2];
    conn_opts.password = argv[3];
    
    do
    {
        mqttrc = MQTTClient_connect(client, &conn_opts);
        printf("mqtt connect returned %i, %s\n", mqttrc, MQTTClient_strerror(mqttrc));
        if (mqttrc != MQTTCLIENT_SUCCESS)
            sleep(10);
    } while (mqttrc != MQTTCLIENT_SUCCESS);
    printf("Connected to mqtt\n");
}

void mqtt_disconnect(MQTTClient client)
{
    MQTTClient_disconnect(client, 1000);
    printf("Disconnected from mqtt\n");
}
