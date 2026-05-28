// ble.ino - BLE 通信和核心板数据管理

// ---------- 核心板数据管理 ----------
void clearCoreData() {
  coreOsVersion  = "";
  coreMac        = "";
  coreWifiStatus = "";
  coreWifiSsid   = "";
  coreWifiIp     = "";
}

bool isCoreHeartbeat(const String& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  return String((const char*)doc["type"]) == "heartbeat";
}

void parseCoreHeartbeat(const String& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  coreOsVersion  = String((const char*)doc["os_version"]);
  coreMac        = String((const char*)doc["mac_address"]);
  coreWifiStatus = String((const char*)doc["wifi_status"]);
  coreWifiSsid   = String((const char*)doc["wifi_ssid"]);
  coreWifiIp     = String((const char*)doc["wifi_ip"]);
  lastCoreHeartbeatMs = millis();

  Serial.println("[核心板心跳] MAC=" + coreMac + " IP=" + coreWifiIp);
}

// 构建发给手机的 BLE 心跳数据包
String buildHeartbeat() {
  bool coreOnline = (lastCoreHeartbeatMs > 0) &&
                    (millis() - lastCoreHeartbeatMs < CORE_HEARTBEAT_TIMEOUT_MS);

  JsonDocument doc;
  doc["type"]        = "heartbeat";
  doc["light_ver"]   = LIGHT_VER;
  doc["light_mac"]   = BLEDevice::getAddress().toString().c_str();
  doc["core_status"] = coreOnline ? "ok" : "error";
  doc["core_ver"]    = coreOsVersion;
  doc["core_mac"]    = coreMac;
  doc["wifi_status"] = coreWifiStatus;
  doc["wifi_ssid"]   = coreWifiSsid;
  doc["wifi_ip"]     = coreWifiIp;
  doc["uptime_s"]    = millis() / 1000;

  String out;
  serializeJson(doc, out);
  return out;
}

// ---------- BLE 回调 ----------
class CharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String data = pChar->getValue().c_str();
    if (data.length() > 0) {
      Serial.println("[BLE→命令] " + data);
      processLedCommand(data.c_str());
      Serial2.println(data);  // 同步透传给核心板
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pSrv) override {
    deviceConnected = true;
    Serial.println("客户端已连接");
  }
  void onDisconnect(BLEServer* pSrv) override {
    deviceConnected = false;
    Serial.println("客户端已断开，重新广播...");
    BLEDevice::startAdvertising();
  }
};

// BLE 初始化（在 setup 中调用）
void setupBLE() {
  Serial.println("正在初始化 BLE...");
  BLEDevice::init(DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ  |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new CharacteristicCallbacks());
  pCharacteristic->setValue("{}");

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE 广播已启动，设备名: " DEVICE_NAME);
  Serial.println("灯头 MAC: " + String(BLEDevice::getAddress().toString().c_str()));
}

// BLE 心跳循环（在 loop 中调用）
void loopBLE() {
  // 核心板超时检测
  if (lastCoreHeartbeatMs > 0 &&
      millis() - lastCoreHeartbeatMs >= CORE_HEARTBEAT_TIMEOUT_MS) {
    clearCoreData();
    lastCoreHeartbeatMs = 0;
    Serial.println("[警告] 核心板心跳超时，数据已清空");
  }

  // 每 2 秒向手机发送一次心跳包
  if (deviceConnected) {
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 2000) {
      lastHeartbeat = millis();
      String packet = buildHeartbeat();
      pCharacteristic->setValue(packet.c_str());
      pCharacteristic->notify();
      Serial.println("[BLE发送] " + packet);
    }
  }
}
