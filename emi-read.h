#include "light-modbus/light-modbus-rtu.h"

typedef struct __attribute__ ((__packed__)) {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t hundredthOfSecond;
    uint16_t deviation;
    uint8_t clockStatus;
} emi_clock_t;

char* getOctetString(modbus_t* ctx, uint16_t registerAddress, uint8_t nb);
double getDoubleFromUInt16(modbus_t* ctx, uint16_t registerAddress, signed char scaller);
double getDoubleFromUInt32(modbus_t* ctx, uint16_t registerAddress, signed char scaler);
emi_clock_t* getTime(modbus_t* ctx);
int _MQTTClient_publish(MQTTClient handle, const char* topicName, const void* payload, int payloadlen);
int _MQTTClient_publishInt(MQTTClient handle, const char* topicName, int n);
int _MQTTClient_publishDouble(MQTTClient handle, const char* topicName, double n, uint8_t decimals);