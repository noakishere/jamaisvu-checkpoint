#include "Microphone_PDM.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/Picopixel.h>

SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(SEMI_AUTOMATIC);

SerialLogHandler logHandler;

#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 

#define OLED_DC     A4
#define OLED_CS     A3
#define OLED_RESET  A5
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
  &SPI, OLED_DC, OLED_RESET, OLED_CS);

const unsigned long MAX_RECORDING_LENGTH_MS = 10000; //This is the max recording length. Change this for desired max length
const unsigned long holdThreshold = 500; //Threshold button hold trigger

IPAddress serverAddr = IPAddress(172, 30, 137, 121);
int serverPort = 7123;

// Pin definitions
int buttonPin = D2;
int redLedPin = D3;
int statusPin = D4;

TCPClient client;
unsigned long recordingStart;
unsigned long buttonPressStart = 0;
bool buttonHeld = false;

enum State { STATE_WAITING, STATE_CONNECT, STATE_RUNNING, STATE_FINISH };
State state = STATE_WAITING;

int dotCount = 0;                     
unsigned long lastDotUpdate = 0;
const unsigned long dotUpdateInterval = 500;

// Function to display "Hold to Record" with animated dots
void NowRecording(int dotCount) {
    display.clearDisplay();
    display.setFont(&Picopixel);
    display.setTextColor(WHITE);
    display.setTextSize(2);
    display.setCursor(0, 30);
    display.println(F("Hold to"));
    display.print(F("Record"));
    // Add animated dots
    for (int i = 0; i < dotCount; i++) {
        display.print(F("."));  // Print one dot for each count
    }
    display.display();
}

// Function to draw a loading bar based on progress
void testdrawstyles(int progress) {
    int barWidth = SCREEN_WIDTH;
    int filledWidth = (barWidth * progress) / 100; // Calculate filled portion
    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setFont(&Picopixel);
    display.setTextSize(2);
    display.setCursor(0, SCREEN_HEIGHT / 2 - 10);
    display.print(F("Listening"));
    display.drawRect(0, SCREEN_HEIGHT / 2, barWidth, 10, WHITE);
    display.fillRect(0, SCREEN_HEIGHT / 2, filledWidth, 10, WHITE);
    display.display();
}

// Function to toggle recording state
void toggleRecording() {
    if (state == STATE_WAITING && WiFi.ready()) {
        state = STATE_CONNECT; 
        Serial.println("Recording started");
        digitalWrite(redLedPin, HIGH); 
    } else if (state == STATE_RUNNING) {
        state = STATE_FINISH;
        Serial.println("Recording stopped");
        digitalWrite(redLedPin, LOW);
    }
}

void setup() {
    WiFi.connect();
    Serial.begin(115200);
    waitFor(Serial.isConnected, 5000);

    // Initialize OLED display
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
        Serial.println(F("SSD1306 allocation failed"));
        for (;;); // Don't proceed, loop forever
    }

    display.display();
    delay(2000);
    display.clearDisplay();

    // Initialize pins
    pinMode(D7, OUTPUT);
    digitalWrite(D7, LOW);
    pinMode(buttonPin, INPUT_PULLUP);
    pinMode(redLedPin, OUTPUT);
    pinMode(statusPin, OUTPUT);

    // Initialize PDM microphone
    int err = Microphone_PDM::instance()
        .withOutputSize(Microphone_PDM::OutputSize::SIGNED_16)
        .withRange(Microphone_PDM::Range::RANGE_2048)
        .withSampleRate(8000)
        .init();

    if (err) {
        Log.error("PDM decoder init err=%d", err);
    }

    err = Microphone_PDM::instance().start();
    if (err) {
        Log.error("PDM decoder start err=%d", err);
    }

    NowRecording(dotCount);
}

void loop() {
    // Check for button hold to start or stop recording
    int buttonState = digitalRead(buttonPin);

    if (buttonState == LOW) { 
        if (buttonPressStart == 0) { 
            buttonPressStart = millis(); 
        }

        if (millis() - buttonPressStart >= holdThreshold && !buttonHeld) {
            buttonHeld = true; 
            toggleRecording();
        }
    } else { 
        buttonPressStart = 0; 
        if (buttonHeld) { 
            state = STATE_FINISH;
            digitalWrite(redLedPin, LOW);
        }
        buttonHeld = false;
    }

    // Update animated dots while waiting
    if (state == STATE_WAITING) {
        if (millis() - lastDotUpdate >= dotUpdateInterval) {
            lastDotUpdate = millis();
            dotCount = (dotCount + 1) % 4; 
            NowRecording(dotCount); 
        }
    }

    // State machine for recording and communication
    switch (state) {
        case STATE_WAITING:
            // Waiting for user to hold the button
            break;

        case STATE_CONNECT:
            if (client.connect(serverAddr, serverPort)) {
                Log.info("Starting");
                recordingStart = millis();
                digitalWrite(D7, HIGH);
                testdrawstyles(0);
                state = STATE_RUNNING;
            } else {
                Log.info("Failed to connect to server");
                state = STATE_WAITING;
            }
            break;

        case STATE_RUNNING: {
            unsigned long elapsed = millis() - recordingStart;
            int progress = (elapsed * 100) / MAX_RECORDING_LENGTH_MS;
            if (progress > 100) progress = 100; 

            testdrawstyles(progress);

            Microphone_PDM::instance().noCopySamples([](void *pSamples, size_t numSamples) {
                client.write((const uint8_t *)pSamples, Microphone_PDM::instance().getBufferSizeInBytes());
            });

            if (!buttonHeld || elapsed >= MAX_RECORDING_LENGTH_MS) {
                state = STATE_FINISH;
            }
            break;
        }

        case STATE_FINISH:
            digitalWrite(D7, LOW);
            client.stop();
            Log.info("Stopping");
            NowRecording(dotCount);
            state = STATE_WAITING;
            break;
    }
}
