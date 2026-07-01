#include "OTL.h"

using namespace CC1101;

OTLradio* OTLradio::instance = nullptr;

OTLradio::OTLradio(OTLmodule module, uint8_t cs_pin, uint8_t gdo0_pin, uint8_t gdo2_pin) : radio(cs_pin, gdo0_pin, gdo2_pin), packetQueue(sizeof(OTLpacketQueueItem), OTL_NUM_RECEIVERS * OTL_SENDER_SENDING_RETRIES) {
  instance = this;
  this->module = module;
}

void OTLradio::onTransmit() {
  if(instance) instance->packetSent = true;
}

void OTLradio::onReceive() {
  if(instance) instance->packetReceived = true;
}

bool OTLradio::begin() {
  if(radio.begin() != STATUS_OK) return false;

  radio.setModulation(MOD_GFSK);
  radio.setFrequency(OTL_FREQ);
  radio.setDataRate(OTL_DATA_RATE);
  radio.setOutputPower(10);

  radio.setPacketLengthMode(PKT_LEN_MODE_FIXED, sizeof(OTLpacket) + 1);
  radio.setAddressFilteringMode(ADDR_FILTER_MODE_CHECK);
  radio.setPreambleLength(64);
  radio.setSyncWord(0x5043);
  radio.setSyncMode(SYNC_MODE_30_32_CS);
  radio.setCrc(true);
  radio.setDataWhitening(true);
  radio.setManchester(false);
  radio.setFEC(false);

  if(radio.setTransmitAction(OTLradio::onTransmit, GDO0) != STATUS_OK) return false;
  if(radio.setReceiveAction(OTLradio::onReceive, GDO2) != STATUS_OK) return false;

  radio.startReceive(this->module);

  return true;
}

void OTLradio::update() {
  if(this->packetSent) {
    this->packetSent = false;

    if(this->sendingPacket.cmd == OTL_CMD_ACK) this->sending = false;
    
    radio.finishTransmit();
    radio.startReceive(this->module);
  }

  if(this->packetReceived) {
    this->packetReceived = false;

    OTLpacket packet;
    radio.readData(reinterpret_cast<uint8_t*>(&packet), sizeof(OTLpacket));

    if(packet.cmd == OTL_CMD_ACK) {
      if(this->sending && packet.senderId == this->sendingTo && packet.seq == this->seq) {
        this->sending = false;
        this->sendResult(true, packet.ext);
      }
      radio.startReceive(this->module);
    }
    else {
      uint8_t ackExt = this->handlePacket(packet);
      this->sending = true;
      this->sendingPacket = {this->module, packet.seq, OTL_CMD_ACK, ackExt};
      radio.startTransmit(reinterpret_cast<uint8_t*>(&this->sendingPacket), sizeof(OTLpacket), packet.senderId);
    }
  }

  if(this->sending && this->sendingPacket.cmd != OTL_CMD_ACK && millis() >= this->nextSendingTry) {
    if(this->sendingRetriesLeft <= 0) {
      this->sending = false;
      this->sendResult(false, 0);
    }
    else {
      radio.startTransmit(reinterpret_cast<uint8_t*>(&this->sendingPacket), sizeof(OTLpacket), this->sendingTo);
      this->nextSendingTry = millis() + (this->module == OTL_MODULE_SENDER ? OTL_SENDER_SENDING_RETRY_AFTER_MS : OTL_RECEIVER_SENDING_RETRY_AFTER_MS);
    }
    this->sendingRetriesLeft--;
  }

  if(!this->sending && !this->packetQueue.isEmpty()) {
    OTLpacketQueueItem item;
    this->packetQueue.pop(&item);
    this->sending = true;
    this->sendingPacket = {this->module, ++this->seq, item.cmd, this->brightness};
    this->sendingTo = item.receiver;
    this->sendingRetriesLeft = item.sendingRetries;
    this->nextSendingTry = millis();
  }

  this->extendUpdate();
}

void OTLradio::queueCmd(OTLcmd cmd, OTLmodule receiver, int sendingRetries) {
  OTLpacketQueueItem item = {cmd, receiver, sendingRetries};
  this->packetQueue.push(&item);
}

OTLmodule OTLradio::getLastReceiver() {
  return this->sendingTo;
}

OTLcmd OTLradio::getLastCmd() {
  return this->sendingPacket.cmd;
}

OTLsender::OTLsender(uint8_t cs_pin, uint8_t gdo0_pin, uint8_t gdo2_pin) : OTLradio(OTL_MODULE_SENDER, cs_pin, gdo0_pin, gdo2_pin) {
  for(OTLmodule receiver : OTLreceivers) states[OTLreceiverToIdx(receiver)] = OTL_STATE_DISCONNECTED;
  for(OTLmodule receiver : OTLreceivers) setCommands[OTLreceiverToIdx(receiver)] = OTL_CMD_STANDBY;
  for(OTLmodule receiver : OTLreceivers) disabled[OTLreceiverToIdx(receiver)] = false;
}

void OTLsender::extendUpdate() {
  if(millis() - this->lastRefresh >= OTL_REFRESH_INTERVAL_MS) {
    for (OTLmodule receiver : OTLreceivers) this->queueCmd(this->setCommands[OTLreceiverToIdx(receiver)], receiver, this->states[OTLreceiverToIdx(receiver)] != OTL_STATE_DISCONNECTED ? OTL_SENDER_SENDING_RETRIES : 1);
    this->lastRefresh = millis();
  }
}

void OTLsender::sendResult(bool result, uint8_t ackExt) {
  OTLmodule receiver = this->getLastReceiver(); 
  if(isOTLreceiver(receiver)) {
    if(result) {
      switch(this->getLastCmd()) {
        case OTL_CMD_STANDBY: this->states[OTLreceiverToIdx(receiver)] = OTL_STATE_STANDBY; break;
        case OTL_CMD_READY: this->states[OTLreceiverToIdx(receiver)] = OTL_STATE_READY; break;
        case OTL_CMD_LIVE: this->states[OTLreceiverToIdx(receiver)] = OTL_STATE_LIVE; break;
        case OTL_CMD_PARTY: this->states[OTLreceiverToIdx(receiver)] = OTL_STATE_PARTY; break;
      }
      this->disabled[OTLreceiverToIdx(receiver)] = ackExt == OTL_ACK_EXT_DISABLED ? true : false;
    }
    else this->states[OTLreceiverToIdx(receiver)] = OTL_STATE_DISCONNECTED;
  }
}

uint8_t OTLsender::handlePacket(OTLpacket packet) {
  if(isOTLreceiver(packet.senderId)) {
    switch(packet.cmd) {
      case OTL_CMD_DISABLED: this->disabled[OTLreceiverToIdx(packet.senderId)] = true; break;
      case OTL_CMD_ENABLED: this->disabled[OTLreceiverToIdx(packet.senderId)] = false; break;
    }
    return this->disabled[OTLreceiverToIdx(packet.senderId)] ? OTL_ACK_EXT_DISABLED : OTL_ACK_EXT_ENABLED;
  }
  return 0;
}

OTLstate OTLsender::getState(OTLmodule receiver) {
  if(isOTLreceiver(receiver)) {
    return this->states[OTLreceiverToIdx(receiver)];
  }
  return OTL_STATE_DISCONNECTED;
}

void OTLsender::switchState(OTLmodule receiver, OTLswitchState newState) {
  if(isOTLreceiver(receiver)) {
    OTLcmd cmd = OTL_CMD_STANDBY;
    switch(newState) {
      case OTL_STANDBY: cmd = OTL_CMD_STANDBY; break;
      case OTL_READY: cmd = OTL_CMD_READY; break;
      case OTL_LIVE: cmd = OTL_CMD_LIVE; break;
      case OTL_PARTY: cmd = OTL_CMD_PARTY; break;
    }
    this->setCommands[OTLreceiverToIdx(receiver)] = cmd;
    this->queueCmd(cmd, receiver, OTL_SENDER_SENDING_RETRIES);
  }
}

uint8_t OTLsender::getBrightness() {
  return this->brightness;
}

void OTLsender::setBrightness(uint8_t br) {
  this->brightness = br;
}

bool OTLsender::getDisabled(OTLmodule receiver) {
  if(isOTLreceiver(receiver)) {
    return this->disabled[OTLreceiverToIdx(receiver)];
  }
  return false;
}

OTLreceiver::OTLreceiver(OTLmodule receiver, uint8_t cs_pin, uint8_t gdo0_pin, uint8_t gdo2_pin) : OTLradio(isOTLreceiver(receiver) ? receiver : OTL_MODULE_RECEIVER_1, cs_pin, gdo0_pin, gdo2_pin) {}

void OTLreceiver::extendUpdate() {
  if(millis() - this->lastConnection > 2 * OTL_REFRESH_INTERVAL_MS) this->state = OTL_STATE_DISCONNECTED;
}

void OTLreceiver::sendResult(bool result, uint8_t ackExt) {
  if(result) {
    if(ackExt == OTL_ACK_EXT_DISABLED) this->disabled = true;
    if(ackExt == OTL_ACK_EXT_ENABLED) this->disabled = false;
  }
  else {
    this->state = OTL_STATE_DISCONNECTED;
    this->disabled = this->setDisabled;
  }
}

uint8_t OTLreceiver::handlePacket(OTLpacket packet) {
  this->lastConnection = millis();

  switch(packet.cmd) {
    case OTL_CMD_STANDBY: this->state = OTL_STATE_STANDBY; break;
    case OTL_CMD_READY: this->state = OTL_STATE_READY; break;
    case OTL_CMD_LIVE: this->state = OTL_STATE_LIVE; break;
    case OTL_CMD_PARTY: this->state = OTL_STATE_PARTY; break;
  }

  this->brightness = packet.ext;

  return this->disabled ? OTL_ACK_EXT_DISABLED : OTL_ACK_EXT_ENABLED;
}

OTLstate OTLreceiver::getState() {
  return this->state;
}

bool OTLreceiver::getDisabled() {
  return this->disabled;
}

void OTLreceiver::switchDisabled(bool disabled) {
  this->setDisabled = disabled;
  if(disabled) this->queueCmd(OTL_CMD_DISABLED, OTL_MODULE_SENDER, OTL_RECEIVER_SENDING_RETRIES);
  else this->queueCmd(OTL_CMD_ENABLED, OTL_MODULE_SENDER, OTL_RECEIVER_SENDING_RETRIES);
}

uint8_t OTLreceiver::getBrightness() {
  return this->brightness;
}

OTLled::OTLled(uint16_t hueOffset) {
  this->hueOffset = hueOffset;
}

void OTLled::update(OTLstate state, bool disabled) {
  if(disabled != this->disabled) {
    this->disabled = disabled;
    this->blinkCycleStart = millis();
  }
  if(state != this->state) {
    this->state = state;
    this->blinkCycleStart = millis() - OTL_DISABLED_BLINK_CYCLE_MS / 2;
    if(state == OTL_STATE_PARTY) this->partyStart = millis();
  }
}

OTLhsv OTLled::getHSV() {
  if(this->disabled) {
    bool blinkCycleFirstHalf = (millis() - this->blinkCycleStart) % OTL_DISABLED_BLINK_CYCLE_MS < OTL_DISABLED_BLINK_CYCLE_MS / 2;
    switch(this->state) {
      case OTL_STATE_DISCONNECTED: return blinkCycleFirstHalf ? OTL_COLOR_DISABLED : OTL_COLOR_DISCONNECTED;
      case OTL_STATE_STANDBY: return OTL_COLOR_DISABLED;
      case OTL_STATE_READY: return blinkCycleFirstHalf ? OTL_COLOR_DISABLED : OTL_COLOR_READY;
      case OTL_STATE_LIVE: return blinkCycleFirstHalf ? OTL_COLOR_DISABLED : OTL_COLOR_LIVE;
      case OTL_STATE_PARTY: return OTL_COLOR_DISABLED;
    }
  }
  else {
    switch(this->state) {
      case OTL_STATE_DISCONNECTED: return OTL_COLOR_DISCONNECTED;
      case OTL_STATE_STANDBY: return OTL_COLOR_STANDBY;
      case OTL_STATE_READY: return OTL_COLOR_READY;
      case OTL_STATE_LIVE: return OTL_COLOR_LIVE;
      case OTL_STATE_PARTY: return OTLhsv{((millis() - this->partyStart) << 6) + this->hueOffset, 255, 255};
    }
  }
}