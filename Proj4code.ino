
#include <Arduino_FreeRTOS.h>
#include <semphr.h>

#define LED1_PIN    8
#define LED2_PIN    7
#define FAN_PIN     3
#define BTN_PIN     2

volatile unsigned long interval1 = 0;
volatile unsigned long interval2 = 0;

const int fanSpeeds[] = {0, 26, 38, 64, 38, 26};
const char *fanLabels[] = {
  "Fan off", "Fan on low", "Fan on medium",
  "Fan on high", "Fan on medium", "Fan on low"};

volatile int fanStep           = 0;
volatile int selectedLED       = 0;
volatile bool awaitingInterval = false;

char inputBuf[16];
uint8_t inputLen = 0;

SemaphoreHandle_t xSerialMutex;

void Task_Serial(void *pvParameters);
void Task_LED1(void *pvParameters);
void Task_LED2(void *pvParameters);
void Task_Fan(void *pvParameters);

void setup() {
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(FAN_PIN,  OUTPUT);
  pinMode(BTN_PIN,  INPUT);

  analogWrite(FAN_PIN, 0);
  Serial.begin(9600);

  Serial.println("Initial state: Button un-pressed");
  Serial.println("Initial state: Fan off");
  Serial.println("");
  Serial.println("What LED? (1 or 2)");

  xSerialMutex = xSemaphoreCreateMutex();

  xTaskCreate(Task_Serial, "Serial", 150, NULL, 2, NULL);
  xTaskCreate(Task_LED1,   "LED1",    80, NULL, 1, NULL);
  xTaskCreate(Task_LED2,   "LED2",    80, NULL, 1, NULL);
  xTaskCreate(Task_Fan,    "Fan",     80, NULL, 1, NULL);
}

void loop() {}

void Task_Serial(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    if (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (inputLen > 0) {
          inputBuf[inputLen] = '\0';
          int value = atoi(inputBuf);
          inputLen = 0;

          if (xSemaphoreTake(xSerialMutex, portMAX_DELAY) == pdTRUE) {
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
            xSemaphoreGive(xSerialMutex);
          }
        }
      } else {
        if (inputLen < 15) {
          inputBuf[inputLen++] = c;
        }
      }
    }
    vTaskDelay(1);
  }
}

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

void Task_LED1(void *pvParameters) {
  (void) pvParameters;
  blinkTask(LED1_PIN, interval1);
}

void Task_LED2(void *pvParameters) {
  (void) pvParameters;
  blinkTask(LED2_PIN, interval2);
}

void Task_Fan(void *pvParameters) {
  (void) pvParameters;
  bool lastBtn = LOW;
  bool debouncing = false;
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

      debouncing = true;
      debounceStart = xTaskGetTickCount();
    }

    if (debouncing && (xTaskGetTickCount() - debounceStart) >= pdMS_TO_TICKS(200)) {
      debouncing = false;
    }

    lastBtn = btn;               // ← always updated every loop
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
