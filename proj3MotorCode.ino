//Proj 3 code 
const int led1Pin = 8;
const int led2Pin = 7;

bool led1State = LOW;
bool led2State = LOW;

unsigned long previousMillis1 = 0;
unsigned long previousMillis2 = 0;

unsigned long interval1 = 0;
unsigned long interval2 = 0;

// Serial Control
int selectedLED = 0;
bool waitingForInterval = false;
String serialBuffer = "";

// Fan / Motor Control
const int fanPin = 3;
const int buttonPin = 2;

const int fanSpeeds[] = {0, 26, 38, 64, 38, 26};
const int numFanSteps = 6;
int fanStep = 0;

bool lastButtonState = LOW;
unsigned long lastPressTime = 0;
const unsigned long debounceDelay = 200;

// Scheduler
#define NUM_TASKS 4

void Task_Serial();
void Task_LED1();
void Task_LED2();
void Task_Fan();

void (*taskList[NUM_TASKS])() = {
  Task_Serial,
  Task_LED1,
  Task_LED2,
  Task_Fan
};

int currentTask = 0;


// SETUP

void setup() {
  pinMode(led1Pin, OUTPUT);
  pinMode(led2Pin, OUTPUT);
  pinMode(fanPin, OUTPUT);
  pinMode(buttonPin, INPUT);

  analogWrite(fanPin, 0);

  Serial.begin(9600);
  while (!Serial);

  Serial.println("Initial state: Button un-pressed");
  Serial.println("Initial state: Fan off");
  Serial.println("");
  Serial.println("What LED? (1 or 2)");
}


// CYCLIC EXEC

void loop() {
  taskList[currentTask]();
  currentTask = (currentTask + 1) % NUM_TASKS;
}


// TASKS


// Task 1: Serial Input (non-blocking)
void Task_Serial() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      serialBuffer.trim();

      if (serialBuffer.length() > 0) {
        int value = serialBuffer.toInt();
        serialBuffer = "";

        if (!waitingForInterval) {
          selectedLED = value;
          if (selectedLED == 1 || selectedLED == 2) {
            Serial.println("What interval (in msec)?");
            waitingForInterval = true;
          } else {
            Serial.println("Invalid LED. Enter 1 or 2.");
            Serial.println("What LED? (1 or 2)");
          }
        } else {
          unsigned long interval = (unsigned long)value;
          if (interval > 0) {
            if (selectedLED == 1) interval1 = interval;
            else interval2 = interval;
            Serial.println("What LED? (1 or 2)");
          }
          waitingForInterval = false;
        }
      }
    } else {
      serialBuffer += c;
    }
  }
}

// Task 2: LED1
void Task_LED1() {
  if (interval1 > 0) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis1 >= interval1 / 2) {
      previousMillis1 = currentMillis;
      led1State = !led1State;
      digitalWrite(led1Pin, led1State);
    }
  }
}

// Task 3: LED2
void Task_LED2() {
  if (interval2 > 0) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis2 >= interval2 / 2) {
      previousMillis2 = currentMillis;
      led2State = !led2State;
      digitalWrite(led2Pin, led2State);
    }
  }
}

// Task 4: Fan button off then low then medium then high then medium then low then repeates
void Task_Fan() {
  bool reading = digitalRead(buttonPin);

  if (reading == HIGH && lastButtonState == LOW) {
    unsigned long now = millis();
    if (now - lastPressTime > debounceDelay) {
      lastPressTime = now;

      fanStep = (fanStep + 1) % numFanSteps;
      analogWrite(fanPin, fanSpeeds[fanStep]);

      switch (fanStep) {
        case 0: Serial.println("Fan off");       break;
        case 1: Serial.println("Fan on low");    break;
        case 2: Serial.println("Fan on medium"); break;
        case 3: Serial.println("Fan on high");   break;
        case 4: Serial.println("Fan on medium"); break;
        case 5: Serial.println("Fan on low");    break;
      }
    }
  }

  lastButtonState = reading;
}