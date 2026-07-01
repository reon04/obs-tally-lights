#pragma once

#include <Arduino.h>
#include <cc1101.h>
#include <cppQueue.h>

#define OTL_DEFAULT_CS_PIN 10
#define OTL_DEFAULT_GDO0_PIN 2 // must be an interrupt-capable pin
#define OTL_DEFAULT_GDO2_PIN 3 // must be an interrupt-capable pin

#define OTL_FREQ 433.8
#define OTL_DATA_RATE 9.6 // 9.6 kbaud default, 1.2 kbaud for long range
#define OTL_SENDER_SENDING_RETRIES 3
#define OTL_SENDER_SENDING_RETRY_AFTER_MS 50 // must be greater than the time to get a package acknowledged (~40ms @ 9.6 kbaud)
#define OTL_RECEIVER_SENDING_RETRIES 5
#define OTL_RECEIVER_SENDING_RETRY_AFTER_MS 60
#define OTL_REFRESH_INTERVAL_MS 1000 // should be greater than OTL_SENDER_SENDING_RETRIES * OTL_SENDER_SENDING_RETRY_AFTER_MS * OTL_NUM_RECEIVERS

#define OTL_DISABLED_BLINK_CYCLE_MS 1000
#define OTL_COLOR_DISABLED OTLhsv{54612, 255, 255}
#define OTL_COLOR_DISCONNECTED OTLhsv{43690, 255, 255}
#define OTL_COLOR_STANDBY OTLhsv{0, 0, 255}
#define OTL_COLOR_READY OTLhsv{21845, 255, 255}
#define OTL_COLOR_LIVE OTLhsv{0, 255, 255}

using namespace CC1101;

enum OTLmodule : uint8_t {
  OTL_MODULE_SENDER,
  OTL_MODULE_RECEIVER_1,
  OTL_MODULE_RECEIVER_2,
  OTL_MODULE_RECEIVER_3,
  OTL_MODULE_RECEIVER_4
};

constexpr OTLmodule OTLreceivers[] = {
  OTL_MODULE_RECEIVER_1,
  OTL_MODULE_RECEIVER_2,
  OTL_MODULE_RECEIVER_3,
  OTL_MODULE_RECEIVER_4
};

constexpr uint8_t OTL_NUM_RECEIVERS = sizeof(OTLreceivers);

constexpr bool isOTLreceiver(OTLmodule module) {
  return module >= OTL_MODULE_RECEIVER_1 && module <= OTL_MODULE_RECEIVER_1 + OTL_NUM_RECEIVERS;
}

constexpr uint8_t OTLreceiverToIdx(OTLmodule receiver) {
  return constrain(receiver, OTL_MODULE_RECEIVER_1, OTL_MODULE_RECEIVER_1 + OTL_NUM_RECEIVERS) - OTL_MODULE_RECEIVER_1;
}

enum OTLstate : uint8_t {
  OTL_STATE_DISCONNECTED,
  OTL_STATE_STANDBY,
  OTL_STATE_READY,
  OTL_STATE_LIVE,
  OTL_STATE_PARTY
};

enum OTLcmd : uint8_t  {
  OTL_CMD_ACK,
  OTL_CMD_STANDBY,
  OTL_CMD_READY,
  OTL_CMD_LIVE,
  OTL_CMD_PARTY,
  OTL_CMD_DISABLED,
  OTL_CMD_ENABLED
};

enum OTLackExtDisabled : uint8_t {
  OTL_ACK_EXT_DISABLED,
  OTL_ACK_EXT_ENABLED
};

enum OTLswitchState : uint8_t {
  OTL_STANDBY,
  OTL_READY,
  OTL_LIVE,
  OTL_PARTY
};

struct OTLpacket {
  uint8_t senderId;
  unsigned long seq;
  OTLcmd cmd;
  uint8_t ext;
};

struct OTLpacketQueueItem {
  OTLcmd cmd;
  OTLmodule receiver;
  int sendingRetries;
};

struct OTLhsv {
  uint16_t h;
  uint8_t s;
  uint8_t v;
};

class OTLradio {
  public:
    OTLradio(OTLmodule module, uint8_t cs_pin = OTL_DEFAULT_CS_PIN, uint8_t gdo0_pin = OTL_DEFAULT_GDO0_PIN, uint8_t gdo2_pin = OTL_DEFAULT_GDO2_PIN);
    bool begin();
    void update();

  protected:
    uint8_t brightness = 128;

    void queueCmd(OTLcmd cmd, OTLmodule receiver, int sendingRetries);
    OTLmodule getLastReceiver();
    OTLcmd getLastCmd();
    virtual void extendUpdate() = 0;
    virtual void sendResult(bool result, uint8_t ackExt) = 0;
    virtual uint8_t handlePacket(OTLpacket packet) = 0;

  private:
    static OTLradio* instance;
    Radio radio;
    cppQueue packetQueue;
    OTLmodule module;
    volatile bool packetSent = false;
    volatile bool packetReceived = false;
    bool sending = false;
    OTLpacket sendingPacket;
    OTLmodule sendingTo;
    int sendingRetriesLeft;
    unsigned long nextSendingTry;
    unsigned long seq = 0;

    static void onTransmit();
    static void onReceive();
};

class OTLsender : public OTLradio {
  public:
    OTLsender(uint8_t cs_pin = OTL_DEFAULT_CS_PIN, uint8_t gdo0_pin = OTL_DEFAULT_GDO0_PIN, uint8_t gdo2_pin = OTL_DEFAULT_GDO2_PIN);
    OTLstate getState(OTLmodule receiver);
    void switchState(OTLmodule receiver, OTLswitchState);
    bool getDisabled(OTLmodule receiver);
    uint8_t getBrightness();
    void setBrightness(uint8_t br);

  private:
    unsigned long lastRefresh = 0;
    OTLstate states[OTL_NUM_RECEIVERS];
    OTLcmd setCommands[OTL_NUM_RECEIVERS];
    bool disabled[OTL_NUM_RECEIVERS];

    void extendUpdate() override;
    void sendResult(bool result, uint8_t ackExt) override;
    uint8_t handlePacket(OTLpacket packet) override;
};

class OTLreceiver : public OTLradio {
  public:
    OTLreceiver(OTLmodule receiver = OTL_MODULE_RECEIVER_1, uint8_t cs_pin = OTL_DEFAULT_CS_PIN, uint8_t gdo0_pin = OTL_DEFAULT_GDO0_PIN, uint8_t gdo2_pin = OTL_DEFAULT_GDO2_PIN);
    OTLstate getState();
    bool getDisabled();
    void switchDisabled(bool disabled);
    uint8_t getBrightness();

  private:
    unsigned long lastConnection = 0;
    OTLstate state = OTL_STATE_DISCONNECTED;
    bool disabled = false;
    bool setDisabled = false;

    void extendUpdate() override;
    void sendResult(bool result, uint8_t ackExt) override;
    uint8_t handlePacket(OTLpacket packet) override;
};

class OTLled {
  public:
    OTLled(uint16_t hueOffset = 0);
    void update(OTLstate state, bool disabled);
    OTLhsv getHSV();

  private:
    uint16_t hueOffset;
    unsigned long partyStart = 0;
    unsigned long blinkCycleStart = 0;
    OTLstate state = OTL_STATE_DISCONNECTED;
    bool disabled = false;
};