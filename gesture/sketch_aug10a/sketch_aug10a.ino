/* Enhanced ESP32 Robot Controller with Fixed Eye Blinking and FastLED Strip
   - Fixed eye blinking functionality with proper timing
   - Converted from NeoPixel to FastLED for better performance
   - Added 5-second delay for reset acknowledgment
   - Improved error handling and debug logging
   - Safe servo movement with bounds checking
   - Dual OLED eye displays with multiple emotions
   - WiFi reconnection and health monitoring
   - Memory-safe AsyncWebServer implementation
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ===== CONFIGURATION =====
const char* WIFI_SSID = "Redmi Note 10S";
const char* WIFI_PASS = "12345678";

// Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Left OLED (I2C bus 0)
#define OLED_LEFT_SDA 21
#define OLED_LEFT_SCL 22
TwoWire wireLeft = TwoWire(0);
Adafruit_SSD1306 displayLeft(SCREEN_WIDTH, SCREEN_HEIGHT, &wireLeft, -1);
bool leftOledOk = false;

// Right OLED (I2C bus 1)
#define OLED_RIGHT_SDA 18
#define OLED_RIGHT_SCL 19
TwoWire wireRight = TwoWire(1);
Adafruit_SSD1306 displayRight(SCREEN_WIDTH, SCREEN_HEIGHT, &wireRight, -1);
bool rightOledOk = false;

// FastLED settings
#define LED_PIN 4
#define LED_COUNT 4
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
CRGB leds[LED_COUNT];
bool ledStripOk = false;

// Servo pins and objects
#define SERVO_HEAD 13
#define SERVO_LEFT_ARM 14
#define SERVO_RIGHT_ARM 27
Servo headServo, leftArmServo, rightArmServo;

// Web server
AsyncWebServer server(80);

// ===== STATE VARIABLES =====
String current_emotion = "happy";
String current_sentiment = "happy";
bool emotion_mode_active = false;
bool head_tracking_enabled = true;
bool debug_mode = true; // Set to false to reduce serial output

// ===== EYE BLINKING SYSTEM =====
unsigned long lastBlinkTime = 0;
unsigned long nextBlinkInterval = 3000; // Random between 2-5 seconds
bool isBlinking = false;
unsigned long blinkStartTime = 0;
const unsigned long BLINK_DURATION = 150; // How long eyes stay closed
bool eyesOpen = true;

// ===== PENDING REQUEST SYSTEM =====
// Thread-safe request queuing to avoid crashes in interrupt context
volatile bool pendingEmotion = false;
String pendingEmotionValue = "";

volatile bool pendingHead = false;
int pendingHeadNumber = -1;
int pendingHeadAngle = -1;

volatile bool pendingReset = false;
volatile bool resetAcknowledged = false;
unsigned long resetStartTime = 0;

// ===== SERVO CONTROL SYSTEM =====
struct ServoState {
  int currentPos;
  int targetPos;
  unsigned long lastMoveTime;
  bool moving;
  int minPos;
  int maxPos;
  const char* name;
};

ServoState headState = {90, 90, 0, false, 45, 135, "HEAD"};
ServoState leftArmState = {180, 180, 0, false, 0, 180, "LEFT_ARM"};
ServoState rightArmState = {0, 0, 0, false, 0, 180, "RIGHT_ARM"};

const int SERVO_STEP_SIZE = 2;      // degrees per step
const int SERVO_MOVE_DELAY = 25;    // milliseconds between steps

// ===== EYE DISPLAY CONSTANTS =====
#define EYE_CENTER_X 64
#define EYE_CENTER_Y 32
#define EYE_WIDTH 45
#define EYE_HEIGHT 35
#define PUPIL_SIZE 14

// ===== LED COLOR DEFINITIONS (FastLED CRGB colors) =====
// Emotion-based LED colors
#define COLOR_HAPPY CRGB::Green
#define COLOR_ANGRY CRGB::Red
#define COLOR_SAD CRGB::White
#define COLOR_FEAR CRGB::White
#define COLOR_DISGUST CRGB::Blue
#define COLOR_SURPRISE CRGB::Yellow
#define COLOR_JOY CRGB::Green
#define COLOR_SADNESS CRGB::White
#define COLOR_ANGER CRGB::Red
#define COLOR_SCARED CRGB::White
#define COLOR_SURPRISED CRGB::Yellow
#define COLOR_OFF CRGB::Black

// ===== TIMING CONSTANTS =====
unsigned long emotion_mode_start = 0;
const unsigned long EMOTION_MODE_DURATION = 12000; // 12 seconds
unsigned long last_wifi_check = 0;
const unsigned long WIFI_CHECK_INTERVAL = 15000; // 15 seconds
unsigned long last_heartbeat = 0;
const unsigned long HEARTBEAT_INTERVAL = 30000; // 30 seconds

// ===== FastLED CONTROL FUNCTIONS =====
void initializeLEDs() {
  Serial.println("Initializing FastLED strip...");
  
  // Initialize FastLED
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LED_COUNT);
  FastLED.setBrightness(80); // Set brightness (0-255)
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 500); // Power management
  
  // Clear all LEDs first
  FastLED.clear();
  FastLED.show();
  delay(100);
  
  // Test sequence to verify LEDs are working
  Serial.println("Testing FastLED strip...");
  
  // Test each LED individually
  for (int i = 0; i < LED_COUNT; i++) {
    leds[i] = CRGB::Red;
    FastLED.show();
    delay(200);
    leds[i] = CRGB::Black;
    FastLED.show();
    delay(100);
  }
  
  // Set default color (green for happy)
  fill_solid(leds, LED_COUNT, COLOR_HAPPY);
  FastLED.show();
  
  ledStripOk = true;
  Serial.println("✓ FastLED strip initialized and tested");
}

void setEmotionLEDs(const String &emotion) {
  if (!ledStripOk) {
    Serial.println("LED Error: Strip not initialized");
    return;
  }
  
  CRGB color = COLOR_OFF;
  
  if (emotion == "happy" || emotion == "joy") {
    color = COLOR_HAPPY; // Green
  } 
  else if (emotion == "angry" || emotion == "anger") {
    color = COLOR_ANGRY; // Red
  } 
  else if (emotion == "sad" || emotion == "sadness") {
    color = COLOR_SAD; // White
  } 
  else if (emotion == "fear" || emotion == "scared") {
    color = COLOR_FEAR; // White
  } 
  else if (emotion == "disgust") {
    color = COLOR_DISGUST; // Blue
  } 
  else if (emotion == "surprise" || emotion == "surprised") {
    color = COLOR_SURPRISE; // Yellow
  }
  else {
    // Default - happy green
    color = COLOR_HAPPY;
  }
  
  // Set all LEDs to the emotion color
  fill_solid(leds, LED_COUNT, color);
  FastLED.show();
  
  if (debug_mode) {
    Serial.print("LEDs: Set to emotion color for '");
    Serial.print(emotion);
    Serial.println("'");
  }
}

void pulseEmotionLEDs(const String &emotion) {
  if (!ledStripOk) return;
  
  CRGB color = COLOR_HAPPY; // Default
  
  if (emotion == "happy" || emotion == "joy") {
    color = COLOR_HAPPY;
  } 
  else if (emotion == "angry" || emotion == "anger") {
    color = COLOR_ANGRY;
  } 
  else if (emotion == "sad" || emotion == "sadness") {
    color = COLOR_SAD;
  } 
  else if (emotion == "fear" || emotion == "scared") {
    color = COLOR_FEAR;
  } 
  else if (emotion == "disgust") {
    color = COLOR_DISGUST;
  } 
  else if (emotion == "surprise" || emotion == "surprised") {
    color = COLOR_SURPRISE;
  }
  
  // Create a pulsing effect using FastLED brightness control
  for (int brightness = 20; brightness <= 150; brightness += 10) {
    FastLED.setBrightness(brightness);
    fill_solid(leds, LED_COUNT, color);
    FastLED.show();
    delay(50);
  }
  
  for (int brightness = 150; brightness >= 20; brightness -= 10) {
    FastLED.setBrightness(brightness);
    fill_solid(leds, LED_COUNT, color);
    FastLED.show();
    delay(50);
  }
  
  // Set back to normal brightness
  FastLED.setBrightness(80);
  setEmotionLEDs(emotion);
}

void flashLEDs(CRGB color, int flashes, int duration) {
  if (!ledStripOk) return;
  
  for (int flash = 0; flash < flashes; flash++) {
    // Turn on
    fill_solid(leds, LED_COUNT, color);
    FastLED.show();
    delay(duration);
    
    // Turn off
    FastLED.clear();
    FastLED.show();
    delay(duration);
  }
  
  // Restore emotion color
  setEmotionLEDs(current_emotion);
}

void rainbowCycle(int speed) {
  if (!ledStripOk) return;
  
  static uint8_t startIndex = 0;
  startIndex = startIndex + 1; /* motion speed */
  
  fill_rainbow(leds, LED_COUNT, startIndex, 7);
  FastLED.show();
  delay(speed);
}

void breatheEffect(CRGB color, int cycles) {
  if (!ledStripOk) return;
  
  for (int cycle = 0; cycle < cycles; cycle++) {
    // Fade in
    for (int brightness = 0; brightness <= 150; brightness += 5) {
      FastLED.setBrightness(brightness);
      fill_solid(leds, LED_COUNT, color);
      FastLED.show();
      delay(30);
    }
    
    // Fade out
    for (int brightness = 150; brightness >= 0; brightness -= 5) {
      FastLED.setBrightness(brightness);
      fill_solid(leds, LED_COUNT, color);
      FastLED.show();
      delay(30);
    }
  }
  
  // Restore normal brightness and emotion color
  FastLED.setBrightness(80);
  setEmotionLEDs(current_emotion);
}

void sparkleEffect(CRGB color, int duration) {
  if (!ledStripOk) return;
  
  unsigned long startTime = millis();
  while (millis() - startTime < duration) {
    // Random sparkle
    int led = random(LED_COUNT);
    leds[led] = color;
    FastLED.show();
    delay(50);
    leds[led] = CRGB::Black;
    FastLED.show();
    delay(50);
  }
  
  // Restore emotion color
  setEmotionLEDs(current_emotion);
}

// ===== SERVO MOVEMENT FUNCTIONS =====
void moveServoSafely(Servo &servo, ServoState &state, int targetAngle) {
  int safeAngle = constrain(targetAngle, state.minPos, state.maxPos);
  if (abs(state.targetPos - safeAngle) > 2) {
    state.targetPos = safeAngle;
    state.moving = true;
    if (debug_mode) {
      Serial.print("SERVO: ");
      Serial.print(state.name);
      Serial.print(" moving from ");
      Serial.print(state.currentPos);
      Serial.print(" to ");
      Serial.println(state.targetPos);
    }
  }
}

void updateServoPositions() {
  unsigned long now = millis();
  
  // Update HEAD servo
  if (headState.moving && (now - headState.lastMoveTime >= SERVO_MOVE_DELAY)) {
    if (headState.currentPos < headState.targetPos) {
      headState.currentPos = min(headState.currentPos + SERVO_STEP_SIZE, headState.targetPos);
    } else if (headState.currentPos > headState.targetPos) {
      headState.currentPos = max(headState.currentPos - SERVO_STEP_SIZE, headState.targetPos);
    }

    if (headState.currentPos >= headState.minPos && headState.currentPos <= headState.maxPos) {
      headServo.write(headState.currentPos);
    }
    headState.lastMoveTime = now;
    if (headState.currentPos == headState.targetPos) {
      headState.moving = false;
      if (debug_mode) {
        Serial.print("SERVO: ");
        Serial.print(headState.name);
        Serial.print(" reached position ");
        Serial.println(headState.currentPos);
      }
    }
  }

  // Update LEFT ARM servo
  if (leftArmState.moving && (now - leftArmState.lastMoveTime >= SERVO_MOVE_DELAY)) {
    if (leftArmState.currentPos < leftArmState.targetPos) {
      leftArmState.currentPos = min(leftArmState.currentPos + SERVO_STEP_SIZE, leftArmState.targetPos);
    } else if (leftArmState.currentPos > leftArmState.targetPos) {
      leftArmState.currentPos = max(leftArmState.currentPos - SERVO_STEP_SIZE, leftArmState.targetPos);
    }

    if (leftArmState.currentPos >= leftArmState.minPos && leftArmState.currentPos <= leftArmState.maxPos) {
      leftArmServo.write(leftArmState.currentPos);
    }
    leftArmState.lastMoveTime = now;
    if (leftArmState.currentPos == leftArmState.targetPos) {
      leftArmState.moving = false;
    }
  }

  // Update RIGHT ARM servo
  if (rightArmState.moving && (now - rightArmState.lastMoveTime >= SERVO_MOVE_DELAY)) {
    if (rightArmState.currentPos < rightArmState.targetPos) {
      rightArmState.currentPos = min(rightArmState.currentPos + SERVO_STEP_SIZE, rightArmState.targetPos);
    } else if (rightArmState.currentPos > rightArmState.targetPos) {
      rightArmState.currentPos = max(rightArmState.currentPos - SERVO_STEP_SIZE, rightArmState.targetPos);
    }

    if (rightArmState.currentPos >= rightArmState.minPos && rightArmState.currentPos <= rightArmState.maxPos) {
      rightArmServo.write(rightArmState.currentPos);
    }
    rightArmState.lastMoveTime = now;
    if (rightArmState.currentPos == rightArmState.targetPos) {
      rightArmState.moving = false;
    }
  }
}

// ===== EYE DISPLAY FUNCTIONS =====
void drawLeftEye(const String &emotion, bool isOpen = true) {
  if (!leftOledOk) return;
  
  displayLeft.clearDisplay();
  
  if (!isOpen) {
    // Draw closed eye - just a line
    displayLeft.drawLine(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y, 
                         EYE_CENTER_X + EYE_WIDTH/2, EYE_CENTER_Y, WHITE);
    displayLeft.display();
    return;
  }
  
  if (emotion == "happy" || emotion == "joy") {
    // Happy eyes - curved bottom
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y - 10, EYE_WIDTH/2 - 3, BLACK);
    displayLeft.fillCircle(EYE_CENTER_X - 5, EYE_CENTER_Y + 8, PUPIL_SIZE/2, WHITE);
  } 
  else if (emotion == "sad" || emotion == "sadness") {
    // Sad eyes - curved top
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y + 10, EYE_WIDTH/2 - 3, BLACK);
    displayLeft.fillCircle(EYE_CENTER_X - 5, EYE_CENTER_Y - 8, PUPIL_SIZE/2, WHITE);
  } 
  else if (emotion == "angry" || emotion == "anger") {
    // Angry eyes - angled rectangle
    displayLeft.fillRect(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 8, EYE_WIDTH, 16, WHITE);
    displayLeft.fillTriangle(
      EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 8,
      EYE_CENTER_X - EYE_WIDTH/4, EYE_CENTER_Y - 15,
      EYE_CENTER_X + EYE_WIDTH/4, EYE_CENTER_Y - 8,
      BLACK
    );
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE/2, BLACK);
  } 
  else if (emotion == "surprise" || emotion == "surprised") {
    // Surprised eyes - wide open circles
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2 - 5, BLACK);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE/2, BLACK);
  } 
  else if (emotion == "fear" || emotion == "scared") {
    // Fearful eyes - small and shifted
    displayLeft.fillCircle(EYE_CENTER_X - 10, EYE_CENTER_Y - 5, EYE_WIDTH/3, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X - 10, EYE_CENTER_Y - 5, PUPIL_SIZE/2, BLACK);
  } 
  else if (emotion == "disgust") {
    // Disgusted eyes - squinted
    displayLeft.fillRect(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 3, EYE_WIDTH, 6, WHITE);
    displayLeft.fillRect(EYE_CENTER_X - PUPIL_SIZE/2, EYE_CENTER_Y - 2, PUPIL_SIZE, 4, BLACK);
  }
  else {
    // Default - neutral/happy
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE/2, BLACK);
  }

  displayLeft.display();
}

void drawRightEye(const String &emotion, bool isOpen = true) {
  if (!rightOledOk) return;
  
  displayRight.clearDisplay();
  
  if (!isOpen) {
    // Draw closed eye - just a line
    displayRight.drawLine(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y, 
                          EYE_CENTER_X + EYE_WIDTH/2, EYE_CENTER_Y, WHITE);
    displayRight.display();
    return;
  }
  
  if (emotion == "happy" || emotion == "joy") {
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y - 10, EYE_WIDTH/2 - 3, BLACK);
    displayRight.fillCircle(EYE_CENTER_X + 5, EYE_CENTER_Y + 8, PUPIL_SIZE/2, WHITE);
  } 
  else if (emotion == "sad" || emotion == "sadness") {
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y + 10, EYE_WIDTH/2 - 3, BLACK);
    displayRight.fillCircle(EYE_CENTER_X + 5, EYE_CENTER_Y - 8, PUPIL_SIZE/2, WHITE);
  } 
  else if (emotion == "angry" || emotion == "anger") {
    displayRight.fillRect(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 8, EYE_WIDTH, 16, WHITE);
    displayRight.fillTriangle(
      EYE_CENTER_X + EYE_WIDTH/2, EYE_CENTER_Y - 8,
      EYE_CENTER_X + EYE_WIDTH/4, EYE_CENTER_Y - 15,
      EYE_CENTER_X - EYE_WIDTH/4, EYE_CENTER_Y - 8,
      BLACK
    );
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE/2, BLACK);
  } 
  else if (emotion == "surprise" || emotion == "surprised") {
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2 - 5, BLACK);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE/2, BLACK);
  } 
  else if (emotion == "fear" || emotion == "scared") {
    displayRight.fillCircle(EYE_CENTER_X + 10, EYE_CENTER_Y - 5, EYE_WIDTH/3, WHITE);
    displayRight.fillCircle(EYE_CENTER_X + 10, EYE_CENTER_Y - 5, PUPIL_SIZE/2, BLACK);
  } 
  else if (emotion == "disgust") {
    displayRight.fillRect(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 3, EYE_WIDTH, 6, WHITE);
    displayRight.fillRect(EYE_CENTER_X - PUPIL_SIZE/2, EYE_CENTER_Y - 2, PUPIL_SIZE, 4, BLACK);
  }
  else {
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE/2, BLACK);
  }

  displayRight.display();
}

void updateEyeDesign() {
  if (debug_mode) {
    Serial.print("EYES: Updating to emotion: ");
    Serial.println(current_emotion);
  }
  drawLeftEye(current_emotion, eyesOpen);
  drawRightEye(current_emotion, eyesOpen);
}

// ===== FIXED EYE BLINKING SYSTEM =====
void updateEyeBlinking() {
  unsigned long now = millis();
  
  // Handle blinking state
  if (isBlinking) {
    if (now - blinkStartTime >= BLINK_DURATION) {
      // End blink - open eyes
      isBlinking = false;
      eyesOpen = true;
      updateEyeDesign();
      
      // Set next blink interval (random between 2-5 seconds)
      nextBlinkInterval = random(2000, 5000);
      lastBlinkTime = now;
      
      if (debug_mode) {
        Serial.print("BLINK: Eyes opened, next blink in ");
        Serial.print(nextBlinkInterval);
        Serial.println("ms");
      }
    }
  } else {
    // Check if it's time for next blink
    if (now - lastBlinkTime >= nextBlinkInterval) {
      // Start blink - close eyes
      isBlinking = true;
      eyesOpen = false;
      blinkStartTime = now;
      updateEyeDesign();
      
      if (debug_mode) {
        Serial.println("BLINK: Eyes closed");
      }
    }
  }
}

// ===== MANUAL BLINK FUNCTION =====
void blinkEyes() {
  if (debug_mode) {
    Serial.println("BLINK: Manual blink triggered");
  }
  
  // Close eyes
  drawLeftEye(current_emotion, false);
  drawRightEye(current_emotion, false);
  delay(BLINK_DURATION);
  
  // Open eyes
  drawLeftEye(current_emotion, true);
  drawRightEye(current_emotion, true);
  
  // Reset automatic blinking timer
  lastBlinkTime = millis();
  nextBlinkInterval = random(2000, 5000);
  isBlinking = false;
  eyesOpen = true;
}

void processPendingRequests() {
  // Process reset request with 5-second delay
  if (pendingReset && !resetAcknowledged) {
    noInterrupts();
    resetAcknowledged = true;
    resetStartTime = millis();
    pendingReset = false;
    interrupts();
    
    Serial.println("RESET: Reset acknowledged, starting 5-second delay...");
    
    // Flash LEDs to indicate reset acknowledgment
    if (ledStripOk) {
      flashLEDs(CRGB::Yellow, 3, 300); // Yellow flash
    }
  }
  
  // Execute reset after 5-second delay
  if (resetAcknowledged) {
    unsigned long elapsed = millis() - resetStartTime;
    if (elapsed >= 5000) { // 5 seconds
      resetAcknowledged = false;
      
      Serial.println("RESET: Executing reset to default state");
      current_emotion = "happy";
      current_sentiment = "happy";
      emotion_mode_active = false;
      head_tracking_enabled = true;
      
      updateEyeDesign();
      setEmotionLEDs("happy");
      moveServoSafely(headServo, headState, 90);
      moveServoSafely(leftArmServo, leftArmState, 180);
      moveServoSafely(rightArmServo, rightArmState, 0);
      
      // Reset blinking system
      lastBlinkTime = millis();
      nextBlinkInterval = random(2000, 5000);
      isBlinking = false;
      eyesOpen = true;
      
      Serial.println("RESET: Complete");
    } else {
      // Show countdown every second
      static unsigned long lastCountdown = 0;
      if (millis() - lastCountdown >= 1000) {
        int remaining = 5 - (elapsed / 1000);
        Serial.print("RESET: Resetting in ");
        Serial.print(remaining);
        Serial.println(" seconds...");
        lastCountdown = millis();
      }
    }
  }

  // Process emotion request
  if (pendingEmotion) {
    noInterrupts();
    String emotion = pendingEmotionValue;
    pendingEmotion = false;
    pendingEmotionValue = "";
    interrupts();

    Serial.print("EMOTION: Processing -> ");
    Serial.println(emotion);
    
    current_sentiment = emotion;
    current_emotion = emotion;
    emotion_mode_active = true;
    head_tracking_enabled = false;
    emotion_mode_start = millis();

    // Update visual displays and LEDs
    updateEyeDesign();
    setEmotionLEDs(emotion);
    
    // Add LED effects for dramatic emotions
    if (emotion == "angry" || emotion == "anger" || emotion == "surprise" || emotion == "surprised") {
      // Flash effect for strong emotions
      if (emotion == "angry" || emotion == "anger") {
        flashLEDs(COLOR_ANGRY, 3, 200);
      } else {
        flashLEDs(COLOR_SURPRISE, 2, 150);
      }
    } else if (emotion == "happy" || emotion == "joy") {
      // Sparkle effect for happiness
      sparkleEffect(COLOR_HAPPY, 1000);
    } else if (emotion == "sad" || emotion == "sadness") {
      // Breathing effect for sadness
      breatheEffect(COLOR_SAD, 2);
    }
    
    // Set arm positions based on emotion
    if (emotion == "sad" || emotion == "sadness" || emotion == "fear" || emotion == "scared") {
      moveServoSafely(leftArmServo, leftArmState, 90);   // Arms down
      moveServoSafely(rightArmServo, rightArmState, 90);
      moveServoSafely(headServo, headState, 70);         // Head down slightly
    } 
    else if (emotion == "angry" || emotion == "anger") {
      moveServoSafely(leftArmServo, leftArmState, 45);   // Arms up aggressively
      moveServoSafely(rightArmServo, rightArmState, 135);
      moveServoSafely(headServo, headState, 95);         // Head up slightly
    } 
    else if (emotion == "surprise" || emotion == "surprised") {
      moveServoSafely(leftArmServo, leftArmState, 120);  // Arms spread wide
      moveServoSafely(rightArmServo, rightArmState, 60);
      moveServoSafely(headServo, headState, 85);
    }
    else if (emotion == "happy" || emotion == "joy") {
      moveServoSafely(leftArmServo, leftArmState, 160);  // Arms up happily
      moveServoSafely(rightArmServo, rightArmState, 20);
      moveServoSafely(headServo, headState, 95);
    }
    else {
      // Default/neutral position
      moveServoSafely(leftArmServo, leftArmState, 180);
      moveServoSafely(rightArmServo, rightArmState, 0);
      moveServoSafely(headServo, headState, 90);
    }
  }

  // Process head movement request
  if (pendingHead) {
    noInterrupts();
    int number = pendingHeadNumber;
    int angle = pendingHeadAngle;
    pendingHead = false;
    pendingHeadNumber = -1;
    pendingHeadAngle = -1;
    interrupts();

    if (!emotion_mode_active && head_tracking_enabled) {
      // DIRECT ANGLE CONTROL
      if (angle != -1) {
        int safeAngle = constrain(angle, headState.minPos, headState.maxPos);
        Serial.print("HEAD: Moving to direct angle ");
        Serial.println(safeAngle);
        moveServoSafely(headServo, headState, safeAngle);
      }
      // PERSON COUNT CONTROL
      else if (number != -1) {
        static unsigned long lastMoveTime = 0;
        static int currentDirection = 1; // 1 for right, -1 for left
        static int movementSpeed = 5;    // degrees per movement
        unsigned long currentTime = millis();
        
        if (currentTime - lastMoveTime > 2000) { // Move every 2 seconds
          int newAngle;
          
          if (number == 0) {
            // NO PEOPLE: Gentle scanning motion
            newAngle = headState.currentPos + (currentDirection * movementSpeed);
            
            if (newAngle >= headState.maxPos) {
              newAngle = headState.maxPos;
              currentDirection = -1;
            } else if (newAngle <= headState.minPos) {
              newAngle = headState.minPos;
              currentDirection = 1;
            }
            
            Serial.print("HEAD: No people - scanning to angle ");
            Serial.println(newAngle);
          }
          else if (number >= 1) {
            // PEOPLE DETECTED: More active tracking
            movementSpeed = 8;
            newAngle = headState.currentPos + (currentDirection * movementSpeed);
            
            if (random(0, 100) < 30) { // 30% chance to change direction
              currentDirection *= -1;
            }
            
            if (newAngle >= headState.maxPos) {
              newAngle = headState.maxPos;
              currentDirection = -1;
            } else if (newAngle <= headState.minPos) {
              newAngle = headState.minPos;
              currentDirection = 1;
            }
            
            Serial.print("HEAD: Tracking ");
            Serial.print(number);
            Serial.print(" person(s) - moving to angle ");
            Serial.println(newAngle);
          }
          
          moveServoSafely(headServo, headState, newAngle);
          lastMoveTime = currentTime;
        }
      }
    } else {
      if (debug_mode) {
        Serial.println("HEAD: Movement blocked (emotion mode active or tracking disabled)");
      }
    }
  }
}

// ===== ADDITIONAL DEBUG FUNCTION =====
void debugHeadTracking() {
  Serial.println("=== HEAD TRACKING DEBUG ===");
  Serial.print("Current head position: ");
  Serial.println(headState.currentPos);
  Serial.print("Target head position: ");
  Serial.println(headState.targetPos);
  Serial.print("Head moving: ");
  Serial.println(headState.moving ? "Yes" : "No");
  Serial.print("Emotion mode active: ");
  Serial.println(emotion_mode_active ? "Yes" : "No");
  Serial.print("Head tracking enabled: ");
  Serial.println(head_tracking_enabled ? "Yes" : "No");
  Serial.print("Pending head requests: ");
  Serial.println(pendingHead ? "Yes" : "No");
  Serial.print("LED strip status: ");
  Serial.println(ledStripOk ? "OK" : "Failed");
  Serial.print("Eye blinking status: ");
  Serial.println(eyesOpen ? "Open" : "Closed");
  Serial.print("Next blink in: ");
  Serial.print((nextBlinkInterval - (millis() - lastBlinkTime)) / 1000.0);
  Serial.println(" seconds");
  Serial.println("===========================");
}

// ===== WEB SERVER HANDLERS =====
void onReceiveHandler(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (debug_mode) {
    Serial.print("RECEIVE: Got ");
    Serial.print(len);
    Serial.print(" bytes: ");
    for(size_t i = 0; i < len && i < 100; i++) {
      Serial.print((char)data[i]);
    }
    Serial.println();
  }

  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    Serial.print("RECEIVE: JSON parse error: ");
    Serial.println(error.c_str());
    request->send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }
  
  if (!doc.containsKey("sentiment")) {
    Serial.println("RECEIVE: Missing sentiment field");
    request->send(400, "application/json", "{\"error\":\"missing_sentiment\"}");
    return;
  }
  
  String sentiment = doc["sentiment"].as<String>();
  sentiment.toLowerCase();
  
  // Queue the emotion for processing
  noInterrupts();
  pendingEmotionValue = sentiment;
  pendingEmotion = true;
  interrupts();
  
  Serial.print("RECEIVE: Queued emotion: ");
  Serial.println(sentiment);
  request->send(200, "application/json", "{\"status\":\"emotion_queued\",\"emotion\":\"" + sentiment + "\"}");
}

void onHeadHandler(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (debug_mode) {
    Serial.print("HEAD: Received ");
    Serial.print(len);
    Serial.print(" bytes: ");
    for(size_t i = 0; i < len && i < 50; i++) {
      Serial.print((char)data[i]);
    }
    Serial.println();
  }

  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    Serial.print("HEAD: JSON parse error: ");
    Serial.println(error.c_str());
    request->send(400, "application/json", "{\"error\":\"invalid_json\",\"details\":\"" + String(error.c_str()) + "\"}");
    return;
  }

  if (doc.containsKey("number")) {
    int number = doc["number"].as<int>();
    Serial.print("HEAD: Got person count: ");
    Serial.println(number);
    
    noInterrupts();
    pendingHeadNumber = number;
    pendingHeadAngle = -1;
    pendingHead = true;
    interrupts();
    
    request->send(200, "application/json", "{\"status\":\"head_number_queued\",\"number\":" + String(number) + "}");
    return;
  }
  
  if (doc.containsKey("angle")) {
    int angle = doc["angle"].as<int>();
    Serial.print("HEAD: Got angle: ");
    Serial.println(angle);
    
    noInterrupts();
    pendingHeadAngle = angle;
    pendingHeadNumber = -1;
    pendingHead = true;
    interrupts();
    
    request->send(200, "application/json", "{\"status\":\"head_angle_queued\",\"angle\":" + String(angle) + "}");
    return;
  }

  Serial.println("HEAD: No valid number or angle found");
  request->send(400, "application/json", "{\"error\":\"missing_number_or_angle\"}");
}

void onHeadPositionHandler(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (debug_mode) {
    Serial.print("HEAD_POS: Received ");
    Serial.print(len);
    Serial.print(" bytes: ");
    for(size_t i = 0; i < len && i < 50; i++) {
      Serial.print((char)data[i]);
    }
    Serial.println();
  }

  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    Serial.print("HEAD_POS: JSON parse error: ");
    Serial.println(error.c_str());
    request->send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }

  // Expected format: {"x": 320, "y": 240, "frame_width": 640}
  if (doc.containsKey("x") && doc.containsKey("frame_width")) {
    int personX = doc["x"].as<int>();
    int frameWidth = doc["frame_width"].as<int>();
    
    // Convert X position to head angle
    // Left side of frame (x=0) -> head turns left (smaller angle)
    // Right side of frame (x=frameWidth) -> head turns right (larger angle)
    int headAngle = map(personX, 0, frameWidth, headState.minPos, headState.maxPos);
    
    Serial.print("HEAD_POS: Person at X=");
    Serial.print(personX);
    Serial.print("/");
    Serial.print(frameWidth);
    Serial.print(" -> angle ");
    Serial.println(headAngle);
    
    if (!emotion_mode_active && head_tracking_enabled) {
      noInterrupts();
      pendingHeadAngle = headAngle;
      pendingHeadNumber = -1;
      pendingHead = true;
      interrupts();
      
      request->send(200, "application/json", "{\"status\":\"head_position_queued\",\"angle\":" + String(headAngle) + "}");
    } else {
      request->send(200, "application/json", "{\"status\":\"ignored_emotion_mode\"}");
    }
    return;
  }

  Serial.println("HEAD_POS: Missing x or frame_width");
  request->send(400, "application/json", "{\"error\":\"missing_x_or_frame_width\"}");
}

void onResetHandler(AsyncWebServerRequest *request) {
  Serial.println("RESET: Reset requested");
  noInterrupts();
  pendingReset = true;
  interrupts();
  request->send(200, "application/json", "{\"status\":\"reset_acknowledged\",\"delay\":\"5_seconds\"}");
}

void onStatusHandler(AsyncWebServerRequest *request) {
  String json = "{";
  json += "\"emotion\":\"" + current_emotion + "\",";
  json += "\"sentiment\":\"" + current_sentiment + "\",";
  json += "\"head_pos\":" + String(headState.currentPos) + ",";
  json += "\"left_arm_pos\":" + String(leftArmState.currentPos) + ",";
  json += "\"right_arm_pos\":" + String(rightArmState.currentPos) + ",";
  json += "\"emotion_mode\":" + String(emotion_mode_active ? "true" : "false") + ",";
  json += "\"head_tracking\":" + String(head_tracking_enabled ? "true" : "false") + ",";
  json += "\"left_oled\":" + String(leftOledOk ? "true" : "false") + ",";
  json += "\"right_oled\":" + String(rightOledOk ? "true" : "false") + ",";
  json += "\"led_strip\":" + String(ledStripOk ? "true" : "false") + ",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"eyes_open\":" + String(eyesOpen ? "true" : "false") + ",";
  json += "\"blinking\":" + String(isBlinking ? "true" : "false") + ",";
  json += "\"reset_pending\":" + String(resetAcknowledged ? "true" : "false") + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"uptime\":" + String(millis());
  json += "}";
  
  request->send(200, "application/json", json);
}

void onPingHandler(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "pong");
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n================================");
  Serial.println("ESP32 Advanced Robot Controller");
  Serial.println("With Fixed Eye Blinking & FastLED");
  Serial.println("================================");
  Serial.print("Free heap at start: ");
  Serial.println(ESP.getFreeHeap());

  // Initialize random seed for blinking
  randomSeed(analogRead(0));

  // Initialize FastLED strip first
  Serial.println("Initializing FastLED strip...");
  initializeLEDs();
  
  // Show startup animation
  if (ledStripOk) {
    flashLEDs(CRGB::Magenta, 2, 300); // Magenta flash for startup
    setEmotionLEDs("happy"); // Default to green
  }

  // Initialize I2C buses
  Serial.println("Initializing I2C buses...");
  wireLeft.begin(OLED_LEFT_SDA, OLED_LEFT_SCL, 400000);
  delay(100);
  wireRight.begin(OLED_RIGHT_SDA, OLED_RIGHT_SCL, 400000);
  delay(100);

  // Initialize OLED displays
  Serial.println("Initializing OLED displays...");
  leftOledOk = displayLeft.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (leftOledOk) {
    Serial.println("✓ Left OLED initialized (0x3C)");
    displayLeft.clearDisplay();
    displayLeft.display();
  } else {
    Serial.println("✗ Left OLED failed to initialize (0x3C)");
  }
  
  delay(200);
  
  rightOledOk = displayRight.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  if (rightOledOk) {
    Serial.println("✓ Right OLED initialized (0x3D)");
    displayRight.clearDisplay();
    displayRight.display();
  } else {
    Serial.println("✗ Right OLED failed to initialize (0x3D)");
  }

  // Initialize servos
  Serial.println("Initializing servos...");
  headServo.attach(SERVO_HEAD);
  delay(50);
  leftArmServo.attach(SERVO_LEFT_ARM);
  delay(50);
  rightArmServo.attach(SERVO_RIGHT_ARM);
  delay(50);

  // Set initial positions
  headState.currentPos = 90;
  headState.targetPos = 90;
  leftArmState.currentPos = 180;
  leftArmState.targetPos = 180;
  rightArmState.currentPos = 0;
  rightArmState.targetPos = 0;

  headServo.write(headState.currentPos);
  leftArmServo.write(leftArmState.currentPos);
  rightArmServo.write(rightArmState.currentPos);
  delay(500);

  Serial.println("✓ Servos initialized and positioned");

  // Initialize blinking system
  lastBlinkTime = millis();
  nextBlinkInterval = random(2000, 5000);
  isBlinking = false;
  eyesOpen = true;
  
  // Draw initial eyes
  updateEyeDesign();
  Serial.println("✓ Initial eye display rendered with blinking system");

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 30000) {
    Serial.print(".");
    delay(500);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    // Flash green LEDs to indicate WiFi connection
    if (ledStripOk) {
      flashLEDs(COLOR_HAPPY, 3, 200);
      setEmotionLEDs("happy");
    }
  } else {
    Serial.println("\n✗ WiFi connection failed, continuing anyway...");
    // Flash red LEDs to indicate WiFi failure
    if (ledStripOk) {
      flashLEDs(COLOR_ANGRY, 3, 200);
      setEmotionLEDs("happy");
    }
  }

  // Setup web server routes
  Serial.println("Setting up web server routes...");
  
  server.on("/receive", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(400, "application/json", "{\"error\":\"no_body\"}");
    },
    NULL,
    onReceiveHandler
  );

  server.on("/head", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(400, "application/json", "{\"error\":\"no_body\"}");
    },
    NULL,
    onHeadHandler
  );

  server.on("/head_position", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(400, "application/json", "{\"error\":\"no_body\"}");
    },
    NULL,
    onHeadPositionHandler
  );

  server.on("/reset", HTTP_POST, onResetHandler);
  server.on("/status", HTTP_GET, onStatusHandler);
  server.on("/ping", HTTP_GET, onPingHandler);

  server.onNotFound([](AsyncWebServerRequest *request) {
    String message = "{\"error\":\"not_found\",\"path\":\"" + request->url() + "\"}";
    request->send(404, "application/json", message);
  });

  server.begin();
  Serial.println("✓ Web server started");
  Serial.println("\nAvailable endpoints:");
  Serial.println("  POST /receive - Receive emotion data");
  Serial.println("  POST /head - Control head movement");
  Serial.println("  POST /head_position - Position-based head control");
  Serial.println("  POST /reset - Reset to default state (5s delay)");
  Serial.println("  GET /status - Get robot status");
  Serial.println("  GET /ping - Health check");
  
  // Final startup indication
  if (ledStripOk) {
    setEmotionLEDs("happy");
  }
  
  Serial.println("\n================================");
  Serial.println("Robot initialization complete!");
  Serial.println("FastLED Emotion Mapping:");
  Serial.println("  Happy/Joy: Green");
  Serial.println("  Angry/Anger: Red");
  Serial.println("  Sad/Sadness/Fear/Scared: White");
  Serial.println("  Disgust: Blue");
  Serial.println("  Surprise/Surprised: Yellow");
  Serial.println("Eye Blinking: Automatic (2-5s intervals)");
  Serial.println("Reset Delay: 5 seconds after request");
  Serial.println("LED Effects: Sparkle, Breathe, Rainbow, Flash");
  Serial.println("Ready for commands...");
  Serial.println("================================\n");
}

void loop() {
  unsigned long now = millis();
  static unsigned long lastDebugTime = 0;
  
  // Process any pending requests from web handlers
  processPendingRequests();
  
  // Update servo positions smoothly
  updateServoPositions();
  
  // Update eye blinking system
  updateEyeBlinking();
  
  // Auto-exit emotion mode after timeout
  if (emotion_mode_active && (now - emotion_mode_start > EMOTION_MODE_DURATION)) {
    Serial.println("EMOTION: Timeout reached, returning to normal mode");
    emotion_mode_active = false;
    head_tracking_enabled = true;
    current_emotion = "happy";
    current_sentiment = "happy";
    updateEyeDesign();
    if (ledStripOk) {
      setEmotionLEDs("happy"); // Return to green LEDs
    }
    
    // Return to neutral position
    moveServoSafely(headServo, headState, 90);
    moveServoSafely(leftArmServo, leftArmState, 180);
    moveServoSafely(rightArmServo, rightArmState, 0);
  }
  
  // Add head tracking debug info every 10 seconds
  if (debug_mode && now - lastDebugTime > 10000) {
    lastDebugTime = now;
    debugHeadTracking();
  }
  
  // WiFi health monitoring
  if (now - last_wifi_check > WIFI_CHECK_INTERVAL) {
    last_wifi_check = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WIFI: Connection lost, attempting reconnection...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      
      // Wait up to 10 seconds for reconnection
      unsigned long reconnectStart = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - reconnectStart < 10000) {
        delay(500);
        Serial.print(".");
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWIFI: Reconnected successfully!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        // Flash green to indicate reconnection
        if (ledStripOk) {
          flashLEDs(COLOR_HAPPY, 2, 150);
          setEmotionLEDs(current_emotion);
        }
      } else {
        Serial.println("\nWIFI: Reconnection failed");
        // Flash red to indicate failure
        if (ledStripOk) {
          flashLEDs(COLOR_ANGRY, 2, 150);
          setEmotionLEDs(current_emotion);
        }
      }
    }
  }
  
  // Periodic heartbeat and system status
  if (debug_mode && now - last_heartbeat > HEARTBEAT_INTERVAL) {
    last_heartbeat = now;
    Serial.println("HEARTBEAT: System running normally");
    Serial.print("  Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    Serial.print("  WiFi: ");
    Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    Serial.print("  Current emotion: ");
    Serial.println(current_emotion);
    Serial.print("  Head position: ");
    Serial.println(headState.currentPos);
    Serial.print("  Emotion mode: ");
    Serial.println(emotion_mode_active ? "Active" : "Inactive");
    Serial.print("  Head tracking: ");
    Serial.println(head_tracking_enabled ? "Enabled" : "Disabled");
    Serial.print("  LED strip: ");
    Serial.println(ledStripOk ? "OK" : "Failed");
    Serial.print("  Eyes: ");
    Serial.println(eyesOpen ? "Open" : "Closed/Blinking");
    
    // Memory warning
    if (ESP.getFreeHeap() < 50000) {
      Serial.println("WARNING: Low memory detected!");
      // Flash orange warning
      if (ledStripOk) {
        flashLEDs(CRGB::Orange, 2, 100);
        setEmotionLEDs(current_emotion);
      }
    }
  }
  
  // Main loop delay - balance responsiveness with system stability
  delay(20);
}

// ===== ADDITIONAL UTILITY FUNCTIONS =====

void performEmotionSequence(const String &emotion) {
  // Extended emotion display sequence with FastLED effects
  Serial.print("EMOTION: Performing sequence for ");
  Serial.println(emotion);
  
  if (emotion == "happy" || emotion == "joy") {
    // Happy sequence: blink and slight head nod with sparkle effect
    if (ledStripOk) {
      sparkleEffect(COLOR_HAPPY, 2000);
    }
    blinkEyes();
    moveServoSafely(headServo, headState, 85);
    delay(300);
    moveServoSafely(headServo, headState, 95);
    delay(300);
    moveServoSafely(headServo, headState, 90);
  }
  else if (emotion == "sad" || emotion == "sadness") {
    // Sad sequence: slow head down movement with breathing effect
    if (ledStripOk) {
      breatheEffect(COLOR_SAD, 3);
    }
    moveServoSafely(headServo, headState, 70);
    delay(1000);
  }
  else if (emotion == "angry" || emotion == "anger") {
    // Angry sequence: sharp head movements with flashing red
    if (ledStripOk) {
      flashLEDs(COLOR_ANGRY, 5, 100);
    }
    moveServoSafely(headServo, headState, 100);
    delay(200);
    moveServoSafely(headServo, headState, 80);
    delay(200);
    moveServoSafely(headServo, headState, 95);
    if (ledStripOk) {
      setEmotionLEDs(emotion);
    }
  }
  else if (emotion == "surprise" || emotion == "surprised") {
    // Surprise sequence: rainbow cycle effect
    if (ledStripOk) {
      for (int i = 0; i < 50; i++) {
        rainbowCycle(20);
      }
      setEmotionLEDs(emotion);
    }
  }
}

void emergencyStop() {
  // Emergency stop function - stops all servo movement and resets LEDs
  Serial.println("EMERGENCY: Stopping all servo movement");
  headState.moving = false;
  leftArmState.moving = false;
  rightArmState.moving = false;
  
  // Flash red emergency signal
  if (ledStripOk) {
    flashLEDs(COLOR_ANGRY, 10, 100);
  }
  
  // Return to safe positions
  headServo.write(90);
  leftArmServo.write(180);
  rightArmServo.write(0);
  
  current_emotion = "happy";
  emotion_mode_active = false;
  head_tracking_enabled = true;
  updateEyeDesign();
  if (ledStripOk) {
    setEmotionLEDs("happy"); // Return to safe green
  }
}