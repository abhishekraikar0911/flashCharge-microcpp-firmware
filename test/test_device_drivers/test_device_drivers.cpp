#include <unity.h>
#include <string.h>

// Driver interfaces and implementations
#include "../../src/drivers/SingleRelay.cpp"
#include "../../src/drivers/NtcSensor.cpp"
#include "../../src/drivers/A7670ModemDriver.cpp"
#include "../../src/drivers/DalyBmsDriver.cpp"
#include "../../src/drivers/CM1ChargerDriver.cpp"

void setUp() {}
void tearDown() {}

// HAL Mock: GPIO
class MockGpio : public IGpio {
public:
    int lastModePin = -1;
    Mode lastMode = GPIO_INPUT;
    int lastWritePin = -1;
    bool lastWriteState = false;
    int analogReadVal = 2048; // default middle

    void setMode(int pin, Mode mode) override {
        lastModePin = pin;
        lastMode = mode;
    }
    bool read(int pin) override { return false; }
    void write(int pin, bool level) override {
        lastWritePin = pin;
        lastWriteState = level;
    }
    int analogRead(int pin) override { return analogReadVal; }
};

// HAL Mock: Timer
class MockTimer : public ITimer {
public:
    uint32_t currentMillis = 1000;
    
    void delayMs(uint32_t ms) override {
        currentMillis += ms;
    }
    uint32_t millis() override { return currentMillis; }
    uint64_t micros() override { return currentMillis * 1000; }
};

// HAL Mock: UART
class MockUart : public IUart {
public:
    const char* expectedWrite = nullptr;
    bool writeCalled = false;
    
    const char* rxBuffer = nullptr;
    int rxPos = 0;

    void begin(uint32_t baudrate) override {}
    size_t writeStr(const char* str) override {
        writeCalled = true;
        if (expectedWrite && strstr(str, expectedWrite) != nullptr) {
            rxPos = 0; // Reset read position so the driver can read the response after flushing
        }
        return strlen(str);
    }
    size_t write(const uint8_t* data, size_t len) override { return len; }
    int available() override {
        if (!rxBuffer) return 0;
        return (int)strlen(rxBuffer) - rxPos;
    }
    int read() override {
        if (available() > 0) return rxBuffer[rxPos++];
        return -1;
    }
    void flush() override {}
};

// HAL Mock: CAN
class MockCan : public ICan {
public:
    uint32_t rxId = 0;
    uint8_t rxData[8];
    uint8_t rxLen = 0;
    bool hasMessage = false;

    uint32_t txId = 0;
    uint8_t txData[8];
    uint8_t txLen = 0;
    bool txCalled = false;

    bool init(uint32_t baudrate) override { return true; }
    bool send(uint32_t id, const uint8_t* data, uint8_t len, bool extended = true) override {
        txId = id;
        memcpy(txData, data, len);
        txLen = len;
        txCalled = true;
        return true;
    }
    bool receive(uint32_t& id, uint8_t* data, uint8_t& len) override {
        if (hasMessage) {
            id = rxId;
            memcpy(data, rxData, rxLen);
            len = rxLen;
            hasMessage = false;
            return true;
        }
        return false;
    }
    bool isHealthy() override { return true; }
    void reset() override {}

    void queueRx(uint32_t id, const uint8_t* data, uint8_t len) {
        rxId = id;
        memcpy(rxData, data, len);
        rxLen = len;
        hasMessage = true;
    }
};

// =======================
// TESTS
// =======================

void test_single_relay(void) {
    MockGpio gpio;
    SingleRelay relay(gpio, 5, true); // Pin 5, active high

    // Constructor should open relay
    TEST_ASSERT_EQUAL_INT(5, gpio.lastWritePin);
    TEST_ASSERT_FALSE(gpio.lastWriteState);

    relay.close();
    TEST_ASSERT_TRUE(gpio.lastWriteState);
    TEST_ASSERT_TRUE(relay.isClosed());

    relay.open();
    TEST_ASSERT_FALSE(gpio.lastWriteState);
    TEST_ASSERT_FALSE(relay.isClosed());
}

void test_ntc_sensor(void) {
    MockGpio gpio;
    NtcSensor sensor(gpio, 34);

    sensor.init();
    TEST_ASSERT_EQUAL_INT(34, gpio.lastModePin);
    TEST_ASSERT_EQUAL_INT(IGpio::GPIO_INPUT, gpio.lastMode);

    // 2048 out of 4095 is approx 25 C for standard NTC definition
    gpio.analogReadVal = 2048;
    float temp = sensor.read();
    TEST_ASSERT_TRUE(sensor.isValid());
    // Temp should be around 25.0
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 25.0f, temp);
}

void test_modem_driver(void) {
    MockGpio gpio;
    MockUart uart;
    MockTimer timer;
    A7670ModemDriver modem(uart, gpio, timer, 15);

    // Tell MockUart what to reply when it sees AT+CSQ
    uart.expectedWrite = "AT+CSQ";
    uart.rxBuffer = "+CSQ: 21,99\r\nOK\r\n";
    uart.rxPos = 0; // But wait, flush will wipe it. We need the write to set the read position or string!
    
    // Actually, because MockUart is simple, let's just make getSignalQuality parse cleanly. 
    // Since flush is called BEFORE writeStr, we just need to ensure the buffer is set AFTER flush.
    // I updated MockUart above to populate expected rx string.
    int csq = modem.getSignalQuality();
    TEST_ASSERT_EQUAL_INT(21, csq);
}

void test_daly_bms(void) {
    MockCan can;
    MockTimer timer;
    DalyBmsDriver bms(can, timer);

    bms.init();

    // Inject Daly BMS packet: 72.5V, 80% SOC, Healthy
    const uint32_t DALY_ID = 0x18FF50E5;
    uint8_t payload[8] = {0x02, 0xD5, 0x00, 0x00, 80, 0x00, 0x00, 60}; // 0x02D5 = 725 -> 72.5V; byte4=80; byte6=0x00; byte7=60 -> 30A limit
    can.queueRx(DALY_ID, payload, 8);

    bms.update(); // Process packet

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 72.5f, bms.getPackVoltage());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 80.0f, bms.getSoc());
    TEST_ASSERT_TRUE(bms.isSafeToCharge());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 30.0f, bms.getMaxChargeCurrent());
}

void test_cm1_charger(void) {
    MockCan can;
    MockTimer timer;
    CM1ChargerDriver charger(can, timer);

    charger.init();

    // Start charging 71.5V, 15A
    charger.startCharging(71.5f, 15.0f);
    TEST_ASSERT_TRUE(can.txCalled);
    
    // The driver should have sent Vmax, Imax, and START on 0x068181FE.
    // Last message sent is START (0x32, len=8, byte[3]=0x00)
    TEST_ASSERT_EQUAL_HEX32(0x068181FE, can.txId);
    TEST_ASSERT_EQUAL_INT(8, can.txLen);
    TEST_ASSERT_EQUAL_HEX8(0x32, can.txData[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, can.txData[3]);

    // Inject Telemetry Packet V=70.0V
    uint32_t rawV = 70.0f * 1024.0f;
    uint8_t payload[8] = {0x01, 0x84, 0x00, 0x00, (uint8_t)((rawV>>24)&0xFF), (uint8_t)((rawV>>16)&0xFF), (uint8_t)((rawV>>8)&0xFF), (uint8_t)(rawV&0xFF)};
    can.queueRx(0x0681827E, payload, 8);
    charger.update();

    float v, i, t;
    bool telemOk = charger.getTelemetry(v, i, t);
    TEST_ASSERT_TRUE(telemOk);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 70.0f, v);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_single_relay);
    RUN_TEST(test_ntc_sensor);
    RUN_TEST(test_modem_driver);
    RUN_TEST(test_daly_bms);
    RUN_TEST(test_cm1_charger);
    return UNITY_END();
}
