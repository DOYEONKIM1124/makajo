#include <ESP8266WiFi.h>
#include <Servo.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

// ========== WiFi 설정 ==========
const char* ssid = "Makajo";
const char* password = "abcdefgh";

// ========== 타이밍 ==========
const unsigned long SCAN_INTERVAL = 3000;  // 연결 상태 확인 주기 (3초)
const unsigned long GRACE_TIME    = 20000; // ★ 유예 시간 20초 ★
const int MOVE_DELAY = 700;                // 서보 도달 대기(ms)

// ========== 서보 각도 ==========
const int ANGLE_CLOSED = 100; // 닫힘(렌즈 가림)
const int ANGLE_OPEN   = 0;   // 열림(렌즈 노출)
// 🚨 방향이 반대면 두 값을 서로 바꾸세요

// ========== 핀 배치 ==========
const int servoTopPin = D1;  // GPIO5  서보 위
const int servoBotPin = D2;  // GPIO4  서보 아래
const int btnPin      = D3;  // GPIO0  버튼
const int ledRedPin   = D7;  // GPIO13 빨강 LED : 켜짐=열림 / 꺼짐=닫힘
// LCD 소프트웨어 I2C : SDA=D6(GPIO12), SCL=D5(GPIO14)
const int LCD_SDA = D6;
const int LCD_SCL = D5;

hd44780_I2Cexp lcd;
Servo servoTop;
Servo servoBot;

enum State { SCANNING, HOME, GRACE_PERIOD, AWAY };
enum LidMode { LID_CLOSED, LID_OPEN };

State   currentState = SCANNING;
LidMode currentLid   = LID_OPEN;   // 부팅 강제닫힘을 위해 일부러 OPEN으로 시작

unsigned long lastScanTime = 0;
unsigned long graceStartTime = 0;
int lastShownSec = -1;

bool lastBtnState = HIGH;
unsigned long btnPressTime = 0;

// ---------- 유틸 ----------
String pad16(String s) {
  while (s.length() < 16) s += ' ';
  if (s.length() > 16) s = s.substring(0, 16);
  return s;
}
void lcdShow(String l0, String l1) {
  lcd.setCursor(0, 0); lcd.print(pad16(l0));
  lcd.setCursor(0, 1); lcd.print(pad16(l1));
}
void moveServoTo(int angle) {
  servoTop.attach(servoTopPin);
  servoBot.attach(servoBotPin);
  servoTop.write(angle);
  servoBot.write(angle);
  delay(MOVE_DELAY);
  servoTop.detach();   // 지터/발열 방지
  servoBot.detach();
}

// ★ 핵심: 핫스팟(Makajo)에 붙어 있으면 재실(있음)로 판단 ★
bool phonePresent() {
  return WiFi.status() == WL_CONNECTED;
}

// 눈꺼풀 + LED 동시 제어
//  열림  -> 빨강 LED 켜짐
//  닫힘  -> 빨강 LED 꺼짐
void setLid(LidMode mode) {
  digitalWrite(ledRedPin, (mode == LID_OPEN) ? HIGH : LOW);
  if (mode != currentLid) {
    moveServoTo(mode == LID_CLOSED ? ANGLE_CLOSED : ANGLE_OPEN);
  }
  currentLid = mode;
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  pinMode(btnPin, INPUT_PULLUP);
  pinMode(ledRedPin, OUTPUT);
  digitalWrite(ledRedPin, LOW);

  // 소프트웨어 I2C 핀 지정 + LCD 초기화
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.begin(16, 2);
  lcd.backlight();
  lcdShow("MAKAJO", "Booting...");

  // ★ 부팅 = 무조건 닫힘 초기화 ★
  Serial.println("\n[부팅] 닫힘 상태로 강제 초기화");
  setLid(LID_CLOSED);
  lcdShow("Init: CLOSED", "Privacy ON");
  delay(800);

  // WiFi 시작 (연결될 때까지 백그라운드로 계속 시도)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("WiFi 연결 시도...");
  lcdShow("WiFi connect..", ssid);

  currentState = SCANNING;
  lastScanTime = 0;
}

// ========== LOOP ==========
void loop() {
  handleButton();

  switch (currentState) {
    case SCANNING:
      if (lastScanTime == 0 || millis() - lastScanTime >= SCAN_INTERVAL) {
        lastScanTime = millis();
        if (phonePresent()) {
          setLid(LID_CLOSED); currentState = HOME;
          lcdShow("Status: HOME", "Lid: CLOSED");
        } else {
          // 아직 연결 전 -> 프라이버시 우선(닫힘) 유지하며 계속 시도
          lcdShow("Waiting WiFi..", ssid);
        }
      }
      break;

    case HOME:
      if (millis() - lastScanTime >= SCAN_INTERVAL) {
        lastScanTime = millis();
        if (!phonePresent()) {
          graceStartTime = millis();
          lastShownSec   = -1;
          currentState   = GRACE_PERIOD;   // 닫힘 유지한 채 카운트다운
        }
      }
      break;

    case GRACE_PERIOD: {
      long remain = (long)GRACE_TIME - (long)(millis() - graceStartTime);
      if (remain < 0) remain = 0;
      int sec = (int)((remain + 999) / 1000);
      if (sec != lastShownSec) {
        lastShownSec = sec;
        lcdShow("Leaving? wait", "Open in: " + String(sec) + "s");
      }
      if (phonePresent()) {         // 다시 붙으면 즉시 복귀
        setLid(LID_CLOSED); currentState = HOME;
        lcdShow("Status: HOME", "Lid: CLOSED");
        break;
      }
      if (millis() - graceStartTime >= GRACE_TIME) {
        setLid(LID_OPEN); currentState = AWAY;
        lcdShow("Status: AWAY", "Lid: OPEN");
      }
      break;
    }

    case AWAY:
      if (millis() - lastScanTime >= SCAN_INTERVAL) {
        lastScanTime = millis();
        if (phonePresent()) {
          setLid(LID_CLOSED); currentState = HOME;
          lcdShow("Status: HOME", "Lid: CLOSED");
        }
      }
      break;
  }
  delay(50);
}

// ========== 버튼 ==========
void handleButton() {
  bool s = digitalRead(btnPin);
  if (s == LOW && lastBtnState == HIGH) {
    btnPressTime = millis(); delay(50);
  } else if (s == HIGH && lastBtnState == LOW) {
    unsigned long dur = millis() - btnPressTime;
    if (dur >= 3000) {
      Serial.println("수동 재부팅"); ESP.restart();
    } else if (dur > 50) {
      if (currentLid == LID_OPEN) { setLid(LID_CLOSED); lcdShow("MANUAL", "Lid: CLOSED"); }
      else                        { setLid(LID_OPEN);   lcdShow("MANUAL", "Lid: OPEN");   }
    }
    delay(50);
  }
  lastBtnState = s;
}
