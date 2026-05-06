// Project 5 - PROJECT-USERTOS
// EEPROM Data Frames via Counting Semaphore
// Author: Shaun Lamb, CMPE 311 Spring 2026
// Date: May 4, 2026
//
// EEPROM frame demo is triggered by typing 'e' in the serial monitor.
// This avoids adding a 5th task which overflows the ATmega328P's 2KB RAM.

#include <Arduino_FreeRTOS.h>
#include <semphr.h>
#include <EEPROM.h>

// ─── Pin Definitions ─────────────────────────────────────────────────────────
#define LED1_PIN    8
#define LED2_PIN    7
#define FAN_PIN     3
#define BTN_PIN     2

// ─── EEPROM Frame Configuration ──────────────────────────────────────────────
#define FRAME_SIZE  256
#define NUM_FRAMES  3

// Byte 0 of each frame = sentinel: 0xFF = free, 0x00 = in-use
// Payload = bytes 1 through FRAME_SIZE-1
const int frameAddr[NUM_FRAMES] = {0, 256, 512};

// ─── Shared State ─────────────────────────────────────────────────────────────
volatile unsigned long interval1 = 0;
volatile unsigned long interval2 = 0;

const int   fanSpeeds[] = {0, 26, 38, 64, 38, 26};
const char *fanLabels[] = {
  "Fan off", "Fan on low", "Fan on medium",
  "Fan on high", "Fan on medium", "Fan on low"
};
volatile int  fanStep          = 0;
volatile int  selectedLED      = 0;
volatile bool awaitingInterval = false;

char    inputBuf[16];
uint8_t inputLen = 0;

// ─── Semaphores ───────────────────────────────────────────────────────────────
SemaphoreHandle_t xSerialMutex;
SemaphoreHandle_t xFrameSema;

// ─── Task Prototypes ──────────────────────────────────────────────────────────
void Task_Serial(void *pvParameters);
void Task_LED1  (void *pvParameters);
void Task_LED2  (void *pvParameters);
void Task_Fan   (void *pvParameters);

// ─── EEPROM Frame API ─────────────────────────────────────────────────────────

void initFrames() {
  for (int i = 0; i < NUM_FRAMES; i++) {
    EEPROM.write(frameAddr[i], 0xFF);
  }
}

int acquireFrame() {
  xSemaphoreTake(xFrameSema, portMAX_DELAY);
  for (int i = 0; i < NUM_FRAMES; i++) {
    if (EEPROM.read(frameAddr[i]) == 0xFF) {
      EEPROM.write(frameAddr[i], 0x00);
      return i;
    }
  }
  xSemaphoreGive(xFrameSema);
  return -1;
}

void releaseFrame(int idx) {
  EEPROM.write(frameAddr[idx], 0xFF);
  xSemaphoreGive(xFrameSema);
}

void writeFrame(int idx, const uint8_t *data, uint8_t len) {
  int base = frameAddr[idx] + 1;
  for (uint8_t i = 0; i < len && i < FRAME_SIZE - 1; i++) {
    EEPROM.write(base + i, data[i]);
  }
}

void readFrame(int idx, uint8_t *buf, uint8_t len) {
  int base = frameAddr[idx] + 1;
  for (uint8_t i = 0; i < len && i < FRAME_SIZE - 1; i++) {
    buf[i] = EEPROM.read(base + i);
  }
}

// Runs one acquire→write→read→release cycle and prints result.
// Called from Task_Serial so no extra task stack needed.
void runEepromDemo() {
  int frame = acquireFrame();
  if (frame < 0) {
    Serial.println("EEPROM: no frame available");
    return;
  }

  static const uint8_t payload[8] = {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04};
  static uint8_t readback[8];

  writeFrame(frame, payload, 8);
  readFrame(frame, readback, 8);
  releaseFrame(frame);

  Serial.print("EEPROM frame ");
  Serial.print(frame);
  Serial.print(" readback: ");
  for (int i = 0; i < 8; i++) {
    if (readback[i] < 0x10) Serial.print("0");
    Serial.print(readback[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(FAN_PIN,  OUTPUT);
  pinMode(BTN_PIN,  INPUT);
  analogWrite(FAN_PIN, 0);

  Serial.begin(9600);
  initFrames();

  // The Uno resets when the serial monitor opens, so we wait for the
  // user to press Enter before printing startup messages.
  while (Serial.available() == 0) { ; }
  while (Serial.available() > 0) { Serial.read(); } // flush the trigger byte

  Serial.println("Initial state: Button un-pressed");
  Serial.println("Initial state: Fan off");
  Serial.println("");
  Serial.println("What LED? (1 or 2)");
  Serial.println("(type 'e' to write EEPROM frame demo, 'r' to read back after power cut)");

  xSerialMutex = xSemaphoreCreateMutex();
  xFrameSema   = xSemaphoreCreateCounting(NUM_FRAMES, NUM_FRAMES);

  xTaskCreate(Task_Serial, "Serial", 150, NULL, 2, NULL);
  xTaskCreate(Task_LED1,   "LED1",    80, NULL, 1, NULL);
  xTaskCreate(Task_LED2,   "LED2",    80, NULL, 1, NULL);
  xTaskCreate(Task_Fan,    "Fan",     80, NULL, 1, NULL);
}

void loop() {}

// ─── Task_Serial ──────────────────────────────────────────────────────────────
// Handles LED selection, interval input, and the 'e' EEPROM demo command.
void Task_Serial(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    if (Serial.available() > 0) {
      char c = Serial.read();

      if (c == '\n' || c == '\r') {
        if (inputLen > 0) {
          inputBuf[inputLen] = '\0';
          inputLen = 0;

          if (xSemaphoreTake(xSerialMutex, portMAX_DELAY) == pdTRUE) {

            // ── EEPROM demo command ──────────────────────────────────────────
            if (inputBuf[0] == 'r' || inputBuf[0] == 'R') {
              // Read frame 0 payload directly — no acquire/release needed.
              // Use this after a power cut to prove data survived in EEPROM.
              Serial.print("Frame 0 after power cut: ");
              for (int i = 0; i < 8; i++) {
                uint8_t b = EEPROM.read(frameAddr[0] + 1 + i);
                if (b < 0x10) Serial.print("0");
                Serial.print(b, HEX);
                Serial.print(" ");
              }
              Serial.println();
              Serial.println("What LED? (1 or 2)");

            } else if (inputBuf[0] == 'e' || inputBuf[0] == 'E') {
              runEepromDemo();
              Serial.println("What LED? (1 or 2)");

            // ── LED / interval flow ──────────────────────────────────────────
            } else {
              int value = atoi(inputBuf);

              if (!awaitingInterval) {
                selectedLED = value;
                if (selectedLED == 1 || selectedLED == 2) {
                  Serial.println("What interval (in msec)?");
                  awaitingInterval = true;
                } else {
                  Serial.println("Invalid LED. Enter 1 or 2.");
                  Serial.println("What LED? (1 or 2)");
                }
              } else {
                unsigned long ms = (unsigned long)value;
                if (ms > 0) {
                  if (selectedLED == 1) interval1 = ms;
                  else                  interval2 = ms;
                  Serial.println("What LED? (1 or 2)");
                }
                awaitingInterval = false;
              }
            }

            xSemaphoreGive(xSerialMutex);
          }
        }
      } else {
        if (inputLen < 15) inputBuf[inputLen++] = c;
      }
    }
    vTaskDelay(1);
  }
}

// ─── LED Blink Helper ─────────────────────────────────────────────────────────
static void blinkTask(int pin, volatile unsigned long &interval) {
  for (;;) {
    unsigned long iv = interval;
    if (iv > 0) {
      digitalWrite(pin, HIGH);
      vTaskDelay(pdMS_TO_TICKS(iv / 2));
      digitalWrite(pin, LOW);
      vTaskDelay(pdMS_TO_TICKS(iv / 2));
    } else {
      digitalWrite(pin, LOW);
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}

void Task_LED1(void *pvParameters) { (void)pvParameters; blinkTask(LED1_PIN, interval1); }
void Task_LED2(void *pvParameters) { (void)pvParameters; blinkTask(LED2_PIN, interval2); }

// ─── Task_Fan ─────────────────────────────────────────────────────────────────
void Task_Fan(void *pvParameters) {
  (void) pvParameters;
  bool       lastBtn       = LOW;
  bool       debouncing    = false;
  TickType_t debounceStart = 0;

  for (;;) {
    bool btn = digitalRead(BTN_PIN);

    if (!debouncing && btn == HIGH && lastBtn == LOW) {
      fanStep = (fanStep + 1) % 6;
      analogWrite(FAN_PIN, fanSpeeds[fanStep]);

      if (xSemaphoreTake(xSerialMutex, portMAX_DELAY) == pdTRUE) {
        Serial.println(fanLabels[fanStep]);
        xSemaphoreGive(xSerialMutex);
      }

      debouncing    = true;
      debounceStart = xTaskGetTickCount();
    }

    if (debouncing && (xTaskGetTickCount() - debounceStart) >= pdMS_TO_TICKS(200)) {
      debouncing = false;
      lastBtn = btn;  // only update lastBtn after debounce window clears
    } else if (!debouncing) {
      lastBtn = btn;  // update normally when not debouncing
    }
    // while debouncing, lastBtn is frozen so a bouncy release can't re-trigger

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
