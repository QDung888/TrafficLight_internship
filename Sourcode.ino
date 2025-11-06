#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <PCF8575.h>   // xreef
#include "SPIFFS.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>

// ===== Config WiFi SoftAP =====
const char* ssid     = "Traffic Control";
const char* password = "12345678";

WebServer server(80);

// ===== I2C pins =====
// const int SDA_PIN = 21;
// const int SCL_PIN = 22;
const int SDA_PIN = 3;
const int SCL_PIN = 2;

// ===== PCF8575 expanders =====
PCF8575 pcf1(0x20, SDA_PIN, SCL_PIN);
PCF8575 pcf2(0x23, SDA_PIN, SCL_PIN);
PCF8575 pcf3(0x27, SDA_PIN, SCL_PIN);
PCF8575* expanders[3] = { &pcf1, &pcf2, &pcf3 };

// ===== Security / Device =====
#define PRIVATE_KEY "my_secret_key"
#define DEVICE_ID   2
#define MAX_LAMPS   14

// Bắt buộc yêu cầu kèm MD5
#define REQUIRE_REQ_AUTH 1   // 1=bắt buộc; 0=không bắt buộc

// ===== Nhóm A ngược nhóm B =====
// Nhóm A: 0,3,4,7,8,11,12 | Nhóm B: 1,2,5,6,9,10,13
inline bool isGroupA(int lamp) {
  return (lamp==0 || lamp==3 || lamp==4 || lamp==7 || lamp==8 || lamp==11 || lamp==12);
}

// ===== Chế độ điều khiển =====
enum Mode : uint8_t { MODE_MANUAL = 0, MODE_AUTO = 1 };
Mode g_mode = MODE_MANUAL;

// ===== Thời gian AUTO (ms) =====
unsigned long durG = 40000; // 40s xanh
unsigned long durY = 5000;  // 5s vàng
// đỏ = xanh + vàng của hướng đối diện

// ===== AUTO state machine 4 pha =====
enum AutoPhase : uint8_t { PH_A_G = 0, PH_A_Y = 1, PH_B_G = 2, PH_B_Y = 3 };
struct AutoCtrl {
  AutoPhase phase;
  unsigned long phaseStart;
  unsigned long phaseDur;
} autoCtrl;

// ===== Tự động về AUTO sau khi MANUAL rỗi =====
const unsigned long MANUAL_IDLE_TO_AUTO_MS = 60000UL; // 1 phút
unsigned long lastManualCmdMs = 0;
inline void onManualActivity() { lastManualCmdMs = millis(); }

// ===== Khai báo trước =====
void setLight(int lamp, char color);
void setGroups(char colorA, char colorB);
void setAllGreen();
String getLampState(int lamp);
void enterPhase(AutoPhase ph);
const char* phaseToStr(AutoPhase ph);
void autoTick();
unsigned long phaseRemainingMs();

String calcMD5(int id_src, int id_des, int opcode, JsonVariantConst data, long timeVal, const char* key);
bool verifyRequestAuth(int id_src, int id_des, int opcode, JsonVariantConst data, long reqTime, const char* reqAuth);

inline void setModeAuto()   { g_mode = MODE_AUTO;   enterPhase(PH_A_G); saveConfig(); }
inline void setModeManual() { g_mode = MODE_MANUAL; saveConfig(); onManualActivity(); }

void handlePacket(String input);
void handleSetLight();
void handleFileRequest();
void handleStatus();
void handleMode();
void handleTiming();

// ===== Map lamp -> expander/pin =====
static inline int expIndexOf(int lamp) { return lamp / 5; }
static inline int basePinOf(int lamp)  { return (lamp * 3) % 15; }

// ===== Điều khiển 1 cột =====
// Giả định: HIGH = LED ON, LOW = LED OFF (PCF8575 -> ULN2803 -> LED)
void setLight(int lamp, char color) {
  if (lamp < 0 || lamp >= MAX_LAMPS) return;
  int expIndex = expIndexOf(lamp);
  int base     = basePinOf(lamp);
  // tắt cả 3
  for (int i = 0; i < 3; i++) expanders[expIndex]->digitalWrite(base + i, LOW);
  // bật theo màu
  int offset = (color=='G') ? 0 : (color=='Y') ? 1 : (color=='R') ? 2 : -1;
  if (offset >= 0) expanders[expIndex]->digitalWrite(base + offset, HIGH);
}

// ===== Đặt cả nhóm A/B =====
void setGroups(char colorA, char colorB) {
  for (int i = 0; i < MAX_LAMPS; ++i) {
    if (isGroupA(i)) setLight(i, colorA);
    else             setLight(i, colorB);
  }
}

// ===== Tất cả xanh (khi MANUAL khởi động) =====
void setAllGreen() { for (int i = 0; i < MAX_LAMPS; ++i) setLight(i, 'G'); }

// ===== Đọc trạng thái 1 cột =====
String getLampState(int lamp) {
  if (lamp < 0 || lamp >= MAX_LAMPS) return "OFF";
  int expIndex = expIndexOf(lamp);
  int base     = basePinOf(lamp);
  bool g = expanders[expIndex]->digitalRead(base + 0);
  bool y = expanders[expIndex]->digitalRead(base + 1);
  bool r = expanders[expIndex]->digitalRead(base + 2);
  if (g) return "G"; if (y) return "Y"; if (r) return "R"; return "OFF";
}

// ===== SPIFFS config lưu timing/mode =====
const char* CONFIG_PATH = "/config.json";
void saveConfig() {
  StaticJsonDocument<256> d;
  d["mode"] = (g_mode == MODE_AUTO) ? "auto" : "manual";
  d["g"] = durG / 1000;
  d["y"] = durY / 1000;
  d["r"] = (durG + durY) / 1000; // chỉ để hiển thị
  File f = SPIFFS.open(CONFIG_PATH, "w");
  if (f) { serializeJson(d, f); f.close(); }
}
void loadConfig() {
  if (!SPIFFS.exists(CONFIG_PATH)) return;
  File f = SPIFFS.open(CONFIG_PATH, "r");
  if (!f) return;
  StaticJsonDocument<256> d;
  if (deserializeJson(d, f) == DeserializationError::Ok) {
    String m = d["mode"] | "manual";
    g_mode = (m == "auto") ? MODE_AUTO : MODE_MANUAL;
    unsigned long sg = d["g"] | (durG / 1000);
    unsigned long sy = d["y"] | (durY / 1000);
    durG = sg * 1000UL;
    durY = sy * 1000UL;
  }
  f.close();
}

// ===== Phase helpers =====
const char* phaseToStr(AutoPhase ph) {
  switch (ph) {
    case PH_A_G: return "A_G";
    case PH_A_Y: return "A_Y";
    case PH_B_G: return "B_G";
    case PH_B_Y: return "B_Y";
  }
  return "?";
}

// ===== AUTO state machine =====
void enterPhase(AutoPhase ph) {
  autoCtrl.phase = ph;
  autoCtrl.phaseStart = millis();
  switch (ph) {
    case PH_A_G: autoCtrl.phaseDur = durG; setGroups('G', 'R'); break; // A xanh, B đỏ
    case PH_A_Y: autoCtrl.phaseDur = durY; setGroups('Y', 'R'); break; // A vàng, B đỏ
    case PH_B_G: autoCtrl.phaseDur = durG; setGroups('R', 'G'); break; // A đỏ, B xanh
    case PH_B_Y: autoCtrl.phaseDur = durY; setGroups('R', 'Y'); break; // A đỏ, B vàng
  }
}
void autoTick() {
  unsigned long now = millis();
  if ((now - autoCtrl.phaseStart) >= autoCtrl.phaseDur) {
    AutoPhase next = PH_A_G;
    switch (autoCtrl.phase) {
      case PH_A_G: next = PH_A_Y; break;
      case PH_A_Y: next = PH_B_G; break;
      case PH_B_G: next = PH_B_Y; break;
      case PH_B_Y: next = PH_A_G; break;
    }
    enterPhase(next);
  }
}
unsigned long phaseRemainingMs() {
  unsigned long now = millis();
  unsigned long elapsed = now - autoCtrl.phaseStart;
  return (elapsed >= autoCtrl.phaseDur) ? 0 : (autoCtrl.phaseDur - elapsed);
}

// ===== Serial buffer =====
String serialBuffer = "";

// ===== HTTP handlers =====
void handleSetLight() {
  if (!(server.hasArg("lamp") && server.hasArg("color"))) {
    server.send(400, "application/json", "{\"error\":\"Missing parameters\"}");
    return;
  }

  int lamp   = server.arg("lamp").toInt();
  char color = server.arg("color")[0];

  if (lamp < 0 || lamp >= MAX_LAMPS || !(color=='R'||color=='Y'||color=='G')) {
    server.send(400, "application/json", "{\"error\":\"invalid lamp or color\"}");
    return;
  }

  // (Nâng cấp) nếu đang AUTO, tự chuyển về MANUAL rồi thực hiện
  if (g_mode == MODE_AUTO) {
    setModeManual();
  }
  setLight(lamp, color);
  onManualActivity();

  server.send(200, "application/json", "{\"status\":0,\"result\":\"OK\"}");
}

void handleFileRequest() {
  String path = server.uri();
  if (path.endsWith("/")) path += "index.html";
  File file = SPIFFS.open(path, "r");
  if (!file) { server.send(404, "text/plain", "File not found"); return; }
  String contentType = "text/plain";
  if      (path.endsWith(".html")) contentType = "text/html";
  else if (path.endsWith(".css"))  contentType = "text/css";
  else if (path.endsWith(".js"))   contentType = "application/javascript";
  else if (path.endsWith(".png"))  contentType = "image/png";
  else if (path.endsWith(".ico"))  contentType = "image/x-icon";
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.streamFile(file, contentType);
  file.close();
}

void handleStatus() {
  StaticJsonDocument<1024> doc;
  doc["mode"] = (g_mode == MODE_AUTO) ? "auto" : "manual";
  if (g_mode == MODE_AUTO) {
    doc["phase"] = phaseToStr(autoCtrl.phase);
    doc["t_remain_ms"] = phaseRemainingMs();
    doc["t_phase_ms"]  = autoCtrl.phaseDur;
  }
  JsonObject timing = doc.createNestedObject("timing");
  timing["g"] = durG / 1000;
  timing["y"] = durY / 1000;
  timing["r"] = (durG + durY) / 1000;
  JsonArray arr = doc.createNestedArray("lamps");
  for (int i = 0; i < MAX_LAMPS; i++) {
    JsonObject obj = arr.createNestedObject();
    obj["lamp"]  = i;
    obj["color"] = getLampState(i);
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleMode() {
  if (server.hasArg("set")) {
    String m = server.arg("set");
    if (m == "auto") { setModeAuto(); }
    else if (m == "manual") { setModeManual(); }
    else { server.send(400, "application/json", "{\"error\":\"set must be auto|manual\"}"); return; }
  }
  StaticJsonDocument<64> d; d["mode"] = (g_mode == MODE_AUTO) ? "auto" : "manual";
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

void handleTiming() {
  bool changed = false;
  if (server.hasArg("g")) { durG = server.arg("g").toInt() * 1000UL; changed = true; }
  if (server.hasArg("y")) { durY = server.arg("y").toInt() * 1000UL; changed = true; }
  if (changed) {
    saveConfig();
    if (g_mode == MODE_AUTO) {
      autoCtrl.phaseDur = (autoCtrl.phase==PH_A_G || autoCtrl.phase==PH_B_G) ? durG : durY;
      if (millis() - autoCtrl.phaseStart >= autoCtrl.phaseDur) autoTick();
    }
  }
  StaticJsonDocument<96> d;
  d["g"] = durG / 1000; d["y"] = durY / 1000; d["r"] = (durG + durY) / 1000;
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

// ===== MD5 helpers =====
// MD5(id_src + id_des + opcode + serialize(data) + time + PRIVATE_KEY)
String calcMD5(int id_src, int id_des, int opcode, JsonVariantConst data, long timeVal, const char* key) {
  String raw = String(id_src) + String(id_des) + String(opcode);
  String dataStr; serializeJson(data, dataStr);
  raw += dataStr;
  raw += String(timeVal);
  raw += String(key);
  MD5Builder md5; md5.begin(); md5.add(raw); md5.calculate(); return md5.toString();
}

bool verifyRequestAuth(int id_src, int id_des, int opcode, JsonVariantConst data, long reqTime, const char* reqAuth) {
  if (!reqAuth || reqTime == 0) return false;
  String expect = calcMD5(id_src, id_des, opcode, data, reqTime, PRIVATE_KEY);
  return expect.equalsIgnoreCase(String(reqAuth));
}

// ===== Serial protocol =====
// opcode = 1: điều khiển 1 đèn (data.{lamp,color})
// opcode = 2: điều khiển nhiều đèn (data.commands = [{lamp,color},...])
// opcode = 3: đọc trạng thái tất cả đèn
// opcode = 4: đọc chế độ (auto/manual) + pha & timing
// opcode = 5: đặt chế độ (data.set="auto"|"manual")
void handlePacket(String input) {
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, input);
  if (error) { Serial.println("{\"status\":255,\"error\":\"invalid json\"}"); return; }

  int id_src  = doc["id_src"] | -1;
  int id_des  = doc["id_des"] | -1;
  int opcode  = doc["opcode"] | -1;
  JsonVariant data = doc["data"];

  // Lấy time/auth từ yêu cầu
  long reqTime = doc["time"] | 0;
  const char* reqAuth = doc["auth"] | (const char*)nullptr;

  // Kiểm đích
  if (id_des != DEVICE_ID) {
    Serial.println("{\"status\":254,\"error\":\"wrong destination\"}");
    return;
  }

  // Bắt buộc kiểm MD5 của yêu cầu
  if (REQUIRE_REQ_AUTH) {
    if (reqTime == 0 || !reqAuth) {
      StaticJsonDocument<256> resp;
      resp["id_src"] = DEVICE_ID;
      resp["id_des"] = id_src;
      resp["opcode"] = (opcode < 0 ? 0 : (opcode | 0x80));
      long nowTime = millis() / 1000;
      resp["time"]  = nowTime;
      resp["auth"]  = calcMD5(id_src, id_des, opcode, data.as<JsonVariantConst>(), nowTime, PRIVATE_KEY);
      resp["status"] = 9;
      resp["error"]  = "missing time/auth in request";
      serializeJson(resp, Serial); Serial.println();
      return;
    }
    if (!verifyRequestAuth(id_src, id_des, opcode, data.as<JsonVariantConst>(), reqTime, reqAuth)) {
      StaticJsonDocument<256> resp;
      resp["id_src"] = DEVICE_ID;
      resp["id_des"] = id_src;
      resp["opcode"] = (opcode < 0 ? 0 : (opcode | 0x80));
      long nowTime = millis() / 1000;
      resp["time"]  = nowTime;
      resp["auth"]  = calcMD5(id_src, id_des, opcode, data.as<JsonVariantConst>(), nowTime, PRIVATE_KEY);
      resp["status"] = 8;
      resp["error"]  = "bad auth";
      serializeJson(resp, Serial); Serial.println();
      return;
    }
  }

  // Chuẩn bị phản hồi
  long nowTime = millis() / 1000;
  String auth = calcMD5(id_src, id_des, opcode, data.as<JsonVariantConst>(), nowTime, PRIVATE_KEY);

  StaticJsonDocument<1024> resp;
  resp["id_src"] = DEVICE_ID;
  resp["id_des"] = id_src;
  resp["opcode"] = (opcode < 0 ? 0 : (opcode | 0x80));
  resp["time"]   = nowTime;
  resp["auth"]   = auth;

  // ===== Xử lý opcode =====

  // ===== OPCODE 1: một đèn (nâng cấp: nếu AUTO -> tự chuyển MANUAL rồi thực thi) =====
  if (opcode == 1) {
    if (data.is<JsonObject>() && data.as<JsonObject>().containsKey("lamp") && data.as<JsonObject>().containsKey("color")) {
      int lamp = data["lamp"] | -1;
      const char* colorStr = data["color"] | "";
      char color = (colorStr && colorStr[0]) ? colorStr[0] : '\0';

      if (lamp >= 0 && lamp < MAX_LAMPS && (color=='R'||color=='Y'||color=='G')) {
        if (g_mode == MODE_AUTO) {            // tự chuyển AUTO -> MANUAL
          setModeManual();
          resp["switched"] = "auto->manual";
        }
        setLight(lamp, color);
        resp["status"]=0; resp["result"]="Light updated"; resp["lamp"]=lamp; resp["color"]=String(color);
        onManualActivity();
      } else { resp["status"]=4; resp["error"]="invalid lamp or color"; }
    } else { resp["status"]=4; resp["error"]="missing lamp/color"; }

  // ===== OPCODE 2: nhiều đèn (nâng cấp: nếu AUTO -> tự chuyển MANUAL khi gặp lệnh hợp lệ đầu tiên) =====
  } else if (opcode == 2) {
    if (data["commands"].is<JsonArray>()) {
      JsonArray cmds = data["commands"].as<JsonArray>();
      JsonArray applied = resp.createNestedArray("applied");
      int okCount=0, errCount=0;
      bool switched = false;

      for (JsonObject cmd : cmds) {
        int lamp = cmd["lamp"] | -1;
        const char* colorStr = cmd["color"] | "";
        char color = (colorStr && colorStr[0]) ? colorStr[0] : '\0';

        JsonObject r = applied.createNestedObject();
        r["lamp"]=lamp; r["color"]=colorStr;

        if (lamp >= 0 && lamp < MAX_LAMPS && (color=='R'||color=='Y'||color=='G')) {
          if (g_mode == MODE_AUTO && !switched) {
            setModeManual();
            resp["switched"] = "auto->manual";
            switched = true;
          }
          setLight(lamp, color);
          r["ok"]=true; okCount++;
        } else {
          r["ok"]=false; errCount++;
        }
      }

      if (okCount == 0) {
        resp["status"]=4; resp["error"]="no valid commands";
      } else if (errCount == 0) {
        resp["status"]=0; resp["result"]="Batch processed";
        onManualActivity();
      } else {
        resp["status"]=6; resp["result"]="Batch processed with errors";
        resp["ok"]=okCount; resp["errors"]=errCount;
        onManualActivity();
      }
    } else { resp["status"]=4; resp["error"]="missing commands[]"; }

  // ===== OPCODE 3: đọc trạng thái tất cả đèn =====
  } else if (opcode == 3) {
    resp["status"]=0; resp["mode"]=(g_mode==MODE_AUTO)?"auto":"manual";
    if (g_mode==MODE_AUTO) { resp["phase"]=phaseToStr(autoCtrl.phase); resp["t_remain_ms"]=phaseRemainingMs(); resp["t_phase_ms"]=autoCtrl.phaseDur; }
    JsonArray arr = resp.createNestedArray("lamps");
    for (int i=0;i<MAX_LAMPS;i++){ JsonObject o=arr.createNestedObject(); o["lamp"]=i; o["color"]=getLampState(i); }

  // ===== OPCODE 4: đọc chế độ + timing =====
  } else if (opcode == 4) {
    resp["status"] = 0;
    resp["mode"]   = (g_mode == MODE_AUTO) ? "auto" : "manual";
    if (g_mode == MODE_AUTO) {
      resp["phase"]        = phaseToStr(autoCtrl.phase);
      resp["t_remain_ms"]  = phaseRemainingMs();
      resp["t_phase_ms"]   = autoCtrl.phaseDur;
    }
    JsonObject timing = resp.createNestedObject("timing");
    timing["g"] = durG / 1000;
    timing["y"] = durY / 1000;
    timing["r"] = (durG + durY) / 1000;

  // ===== OPCODE 5: đặt chế độ =====
  } else if (opcode == 5) {
    const char* set = data["set"] | "";
    if (!strcmp(set, "auto")) {
      setModeAuto();  resp["status"]=0; resp["mode"]="auto";
    } else if (!strcmp(set, "manual")) {
      setModeManual(); resp["status"]=0; resp["mode"]="manual";
    } else {
      resp["status"]=4; resp["error"]="set must be auto|manual";
    }

  } else {
    resp["status"]=3; resp["error"]="unknown opcode";
  }

  serializeJson(resp, Serial);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Traffic-Light Controller (AUTO two directions) Ready!");

  if (!SPIFFS.begin(true)) Serial.println("SPIFFS mount failed!");
  loadConfig();

  Wire.begin(SDA_PIN, SCL_PIN);
  for (int i = 0; i < 3; i++) {
    expanders[i]->begin();
    for (int p = 0; p < 16; p++) {
      expanders[i]->pinMode(p, OUTPUT);
      expanders[i]->digitalWrite(p, LOW);
    }
  }

  if (g_mode == MODE_AUTO) enterPhase(PH_A_G);
  else setAllGreen();

  // Mốc idle khi MANUAL
  lastManualCmdMs = millis();

  WiFi.softAP(ssid, password);
  Serial.print("AP IP address: "); Serial.println(WiFi.softAPIP());

  server.on("/set",     handleSetLight);
  server.on("/status",  handleStatus);
  server.on("/mode",    handleMode);
  server.on("/timing",  handleTiming);
  server.onNotFound(handleFileRequest);
  server.begin();
}

void loop() {
  server.handleClient();

  if (g_mode == MODE_AUTO) {
    autoTick();
  } else {
    // MANUAL -> nếu quá 1 phút không có lệnh manual -> tự về AUTO
    unsigned long now = millis();
    if ((now - lastManualCmdMs) >= MANUAL_IDLE_TO_AUTO_MS) {
      setModeAuto();
    }
  }

  // Nhận JSON qua Serial (kết thúc bằng '\n')
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialBuffer.trim();
      if (serialBuffer.length() > 0) handlePacket(serialBuffer);
      serialBuffer = "";
    } else serialBuffer += c;
  }
}
