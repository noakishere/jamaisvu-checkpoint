#include "Microphone_PDM.h"

SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(SEMI_AUTOMATIC);

SerialLogHandler logHandler;

const unsigned long MAX_RECORDING_LENGTH_MS = 10000;
//Holding time before it actuates
const unsigned long holdThreshold = 2000;

//Ip Address of the Server that the example is running on
IPAddress serverAddr = IPAddress(172,30,137,143);
int serverPort = 7123;

// Defining the pins
int buttonPin = D2; //Button pin
int redLedPin = D3; // Activates when recording starts
int statusPin = D4; //To show that device has been powered on

TCPClient client;
unsigned long recordingStart;
unsigned long buttonPressStart = 0; // Tracks the time when the button was first pressed
bool buttonHeld = false;            // Tracks if the button is held

enum State
{
  STATE_WAITING,
  STATE_CONNECT,
  STATE_RUNNING,
  STATE_FINISH
};
State state = STATE_WAITING;

// Function to toggle recording state when button is held
void toggleRecording()
{
  if (state == STATE_WAITING && WiFi.ready())
  {
    state = STATE_CONNECT; // Start recording
    digitalWrite(redLedPin, HIGH); // Turn on red LED when recording
  }
  else if (state == STATE_RUNNING)
  {
    state = STATE_FINISH; // Stop recording
    digitalWrite(redLedPin, LOW); // Turn off red LED when not recording
  }
}

void setup()
{
  WiFi.connect();

  // Initialize LED and button pins
  pinMode(D7, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(redLedPin, OUTPUT);
  pinMode(statusPin, OUTPUT);
  digitalWrite(D7, LOW);
  digitalWrite(statusPin, HIGH); //Always on to show that device is on

  //Library Code from Microphone_PDM which uses 8000 sample rate 
  int err = Microphone_PDM::instance()
                .withOutputSize(Microphone_PDM::OutputSize::SIGNED_16)
                .withRange(Microphone_PDM::Range::RANGE_2048)
                .withSampleRate(8000)
                .init();

  if (err)
  {
    Log.error("PDM decoder init err=%d", err);
  }

  err = Microphone_PDM::instance().start();
  if (err)
  {
    Log.error("PDM decoder start err=%d", err);
  }
//End of library code

}

void loop()
{
  // Check for button hold to start or stop recording
  int buttonState = digitalRead(buttonPin);

  if (buttonState == LOW)
  { // Button is pressed
    if (buttonPressStart == 0)
    {                              // First press detected
      buttonPressStart = millis(); // Record the press time
    }

    if (millis() - buttonPressStart >= holdThreshold && !buttonHeld)
    {
      buttonHeld = true; // Mark button as held
      toggleRecording(); // Toggle recording state
    }
  }
  else
  {                       // Button is not pressed
    buttonPressStart = 0; // Reset press time
    buttonHeld = false;   // Reset hold state
  }

  // Existing state machine for microphone recording and TCP communication
  switch (state)
  {
  case STATE_WAITING:
    // Waiting for the user to hold the button
    break;

  case STATE_CONNECT:
    if (client.connect(serverAddr, serverPort))
    {
      Log.info("starting");
      recordingStart = millis();
      digitalWrite(D7, HIGH);
      state = STATE_RUNNING;
    }
    else
    {
      Log.info("failed to connect to server");
      state = STATE_WAITING;
    }
    break;

  case STATE_RUNNING:
    // Sending microphone data to server
    Microphone_PDM::instance().noCopySamples([](void *pSamples, size_t numSamples)
                                             { client.write((const uint8_t *)pSamples, Microphone_PDM::instance().getBufferSizeInBytes()); });

    if (millis() - recordingStart >= MAX_RECORDING_LENGTH_MS)
    {
      state = STATE_FINISH;
    }
    break;

  case STATE_FINISH:
    digitalWrite(D7, LOW);
    client.stop();
    Log.info("stopping");
    digitalWrite(redLedPin, LOW);
    state = STATE_WAITING;
    break;
  }
}
