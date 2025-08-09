#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <AsyncTCP.h>

// WiFi Credentials
const char* ssid = "Redmi Note 10S";
const char* password = "12345678";

// OLED Config - FIXED: Changed SDA/SCL pins to avoid conflicts
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// Left Eye OLED
#define OLED_LEFT_SDA 21
#define OLED_LEFT_SCL 22
// Right Eye OLED - CHANGED: Different pins to avoid I2C conflicts
#define OLED_RIGHT_SDA 16
#define OLED_RIGHT_SCL 17

TwoWire wireLeft = TwoWire(0);
Adafruit_SSD1306 displayLeft(SCREEN_WIDTH, SCREEN_HEIGHT, &wireLeft, -1);
TwoWire wireRight = TwoWire(1);
Adafruit_SSD1306 displayRight(SCREEN_WIDTH, SCREEN_HEIGHT, &wireRight, -1);

// Servo Pins
#define SERVO_HEAD 13
#define SERVO_LEFT_ARM 14
#define SERVO_RIGHT_ARM 27
Servo headServo;
Servo leftArmServo;
Servo rightArmServo;

// FIXED: Reduced server instance overhead
AsyncWebServer server(80);

// Enhanced state management
String current_emotion = "happy";
bool emotion_mode_active = false;
bool head_tracking_enabled = true;

// FIXED: Simplified request tracking
unsigned long last_request_time = 0;
const unsigned long REQUEST_COOLDOWN = 100; // Increased cooldown
volatile bool server_busy = false;

// WiFi health monitoring
unsigned long last_wifi_check = 0;
const unsigned long WIFI_CHECK_INTERVAL = 10000; // Check every 10 seconds

// FIXED: Simplified task system - removed complex queue
struct SimpleTask {
  bool has_emotion_task;
  bool has_head_task;
  String emotion_data;
  int head_angle;
  unsigned long task_time;
};
SimpleTask currentTask = {false, false, "", 90, 0};

// Servo state with safety limits
struct ServoState {
  int currentPos;
  int targetPos;
  unsigned long lastMoveTime;
  bool moving;
  int minPos;
  int maxPos;
};

// FIXED: Added safety limits
ServoState headState = {90, 90, 0, false, 60, 120};
ServoState leftArmState = {180, 180, 0, false, 0, 180};
ServoState rightArmState = {0, 0, 0, false, 0, 180};

// FIXED: Slower servo movement for stability
const int SERVO_STEP_SIZE = 2;
const int SERVO_MOVE_DELAY = 50;

// Blink timing - FIXED: Simplified
unsigned long lastBlinkTime = 0;
const unsigned long blinkInterval = 3000;
const unsigned long blinkDuration = 200;
bool isBlinking = false;

// Eye parameters
#define EYE_CENTER_X 64
#define EYE_CENTER_Y 32
#define EYE_WIDTH 40
#define EYE_HEIGHT 30
#define PUPIL_SIZE 12

// FIXED: WiFi reconnection with better error handling
void reconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("🔄 Reconnecting WiFi...");
    WiFi.disconnect();
    delay(1000);
    WiFi.begin(ssid, password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi reconnected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\n❌ WiFi reconnection failed");
    }
  }
}

// FIXED: Safe servo movement with bounds checking
void moveServoSafely(Servo& servo, ServoState& state, int targetAngle) {
  // Apply safety limits
  int safeAngle = constrain(targetAngle, state.minPos, state.maxPos);
  
  if (abs(state.targetPos - safeAngle) > 3) {
    state.targetPos = safeAngle;
    state.moving = true;
    Serial.print("Moving servo to: ");
    Serial.println(safeAngle);
  }
}

// FIXED: Safer servo updates with watchdog
void updateServoPositions() {
  unsigned long currentTime = millis();
  
  // Update head servo
  if (headState.moving && (currentTime - headState.lastMoveTime >= SERVO_MOVE_DELAY)) {
    if (headState.currentPos < headState.targetPos) {
      headState.currentPos = min(headState.currentPos + SERVO_STEP_SIZE, headState.targetPos);
    } else if (headState.currentPos > headState.targetPos) {
      headState.currentPos = max(headState.currentPos - SERVO_STEP_SIZE, headState.targetPos);
    }
    
    // FIXED: Added try-catch equivalent and bounds check
    if (headState.currentPos >= headState.minPos && headState.currentPos <= headState.maxPos) {
      headServo.write(headState.currentPos);
    }
    headState.lastMoveTime = currentTime;
    
    if (headState.currentPos == headState.targetPos) {
      headState.moving = false;
    }
  }
  
  // Update left arm servo
  if (leftArmState.moving && (currentTime - leftArmState.lastMoveTime >= SERVO_MOVE_DELAY)) {
    if (leftArmState.currentPos < leftArmState.targetPos) {
      leftArmState.currentPos = min(leftArmState.currentPos + SERVO_STEP_SIZE, leftArmState.targetPos);
    } else if (leftArmState.currentPos > leftArmState.targetPos) {
      leftArmState.currentPos = max(leftArmState.currentPos - SERVO_STEP_SIZE, leftArmState.targetPos);
    }
    
    if (leftArmState.currentPos >= leftArmState.minPos && leftArmState.currentPos <= leftArmState.maxPos) {
      leftArmServo.write(leftArmState.currentPos);
    }
    leftArmState.lastMoveTime = currentTime;
    
    if (leftArmState.currentPos == leftArmState.targetPos) {
      leftArmState.moving = false;
    }
  }
  
  // Update right arm servo
  if (rightArmState.moving && (currentTime - rightArmState.lastMoveTime >= SERVO_MOVE_DELAY)) {
    if (rightArmState.currentPos < rightArmState.targetPos) {
      rightArmState.currentPos = min(rightArmState.currentPos + SERVO_STEP_SIZE, rightArmState.targetPos);
    } else if (rightArmState.currentPos > rightArmState.targetPos) {
      rightArmState.currentPos = max(rightArmState.currentPos - SERVO_STEP_SIZE, rightArmState.targetPos);
    }
    
    if (rightArmState.currentPos >= rightArmState.minPos && rightArmState.currentPos <= rightArmState.maxPos) {
      rightArmServo.write(rightArmState.currentPos);
    }
    rightArmState.lastMoveTime = currentTime;
    
    if (rightArmState.currentPos == rightArmState.targetPos) {
      rightArmState.moving = false;
    }
  }
}

// FIXED: Simplified emotion mode timeout
void checkEmotionTimeout() {
  if (emotion_mode_active && (millis() - currentTask.task_time > 8000)) {
    Serial.println("🔄 Emotion timeout - returning to normal");
    emotion_mode_active = false;
    head_tracking_enabled = true;
    current_emotion = "happy";
    updateEyeDesign();
    updateArmPose();
    moveServoSafely(headServo, headState, 90);
  }
}

// FIXED: Optimized eye drawing functions with memory management
void drawLeftEye(String emotion) {
  displayLeft.clearDisplay();
  
  if (emotion == "happy") {
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y - 8, EYE_WIDTH/2, BLACK);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y + 5, PUPIL_SIZE/2, WHITE);
  }
  else if (emotion == "sad") {
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y + 8, EYE_WIDTH/2, BLACK);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y - 5, PUPIL_SIZE/2, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X + 15, EYE_CENTER_Y + 15, 3, WHITE);
  }
  else if (emotion == "angry") {
    displayLeft.fillRect(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 5, EYE_WIDTH, 15, WHITE);
    displayLeft.fillTriangle(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 15,
                            EYE_CENTER_X + EYE_WIDTH/2, EYE_CENTER_Y - 25,
                            EYE_CENTER_X + EYE_WIDTH/2, EYE_CENTER_Y - 15, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE/2, BLACK);
  }
  else if (emotion == "fear") {
    displayLeft.drawCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE - 3, BLACK);
  }
  else {
    // Default happy
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y - 8, EYE_WIDTH/2, BLACK);
    displayLeft.fillCircle(EYE_CENTER_X, EYE_CENTER_Y + 5, PUPIL_SIZE/2, WHITE);
  }
  
  displayLeft.display();
}

void drawRightEye(String emotion) {
  displayRight.clearDisplay();
  
  if (emotion == "happy") {
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y - 8, EYE_WIDTH/2, BLACK);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y + 5, PUPIL_SIZE/2, WHITE);
  }
  else if (emotion == "sad") {
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y + 8, EYE_WIDTH/2, BLACK);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y - 5, PUPIL_SIZE/2, WHITE);
    displayRight.fillCircle(EYE_CENTER_X - 15, EYE_CENTER_Y + 15, 3, WHITE);
  }
  else if (emotion == "angry") {
    displayRight.fillRect(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 5, EYE_WIDTH, 15, WHITE);
    displayRight.fillTriangle(EYE_CENTER_X + EYE_WIDTH/2, EYE_CENTER_Y - 15,
                             EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 25,
                             EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 15, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE/2, BLACK);
  }
  else if (emotion == "fear") {
    displayRight.drawCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, PUPIL_SIZE - 3, BLACK);
  }
  else {
    // Default happy
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y, EYE_WIDTH/2, WHITE);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y - 8, EYE_WIDTH/2, BLACK);
    displayRight.fillCircle(EYE_CENTER_X, EYE_CENTER_Y + 5, PUPIL_SIZE/2, WHITE);
  }
  
  displayRight.display();
}

void showBlink() {
  displayLeft.clearDisplay();
  displayLeft.fillRect(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 2, EYE_WIDTH, 4, WHITE);
  displayLeft.display();
  
  displayRight.clearDisplay();
  displayRight.fillRect(EYE_CENTER_X - EYE_WIDTH/2, EYE_CENTER_Y - 2, EYE_WIDTH, 4, WHITE);
  displayRight.display();
}

void updateEyeDesign() {
  drawLeftEye(current_emotion);
  drawRightEye(current_emotion);
}

void updateArmPose() {
  if (current_emotion == "sad" || current_emotion == "fear") {
    moveServoSafely(rightArmServo, rightArmState, 90);
    moveServoSafely(leftArmServo, leftArmState, 90);
  } else if (current_emotion == "angry") {
    moveServoSafely(rightArmServo, rightArmState, 150);
    moveServoSafely(leftArmServo, leftArmState, 30);
  } else {
    // Happy - default pose
    moveServoSafely(rightArmServo, rightArmState, 0);
    moveServoSafely(leftArmServo, leftArmState, 180);
  }
}

// FIXED: Simplified task processing
void processSimpleTasks() {
  unsigned long currentTime = millis();
  
  // Process emotion task
  if (currentTask.has_emotion_task) {
    current_emotion = currentTask.emotion_data;
    emotion_mode_active = true;
    head_tracking_enabled = false;
    currentTask.task_time = currentTime;
    
    updateEyeDesign();
    updateArmPose();
    moveServoSafely(headServo, headState, 75);
    
    Serial.print("🎭 Processing emotion: ");
    Serial.println(currentTask.emotion_data);
    
    currentTask.has_emotion_task = false; // Mark as processed
  }
  
  // Process head task (only if not in emotion mode)
  if (currentTask.has_head_task && !emotion_mode_active) {
    int safeAngle = constrain(currentTask.head_angle, 70, 110);
    moveServoSafely(headServo, headState, safeAngle);
    
    Serial.print("👤 Processing head move: ");
    Serial.println(safeAngle);
    
    currentTask.has_head_task = false; // Mark as processed
  }
}

// FIXED: Ultra-fast response handlers with minimal processing
void handleReceiveRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (server_busy) {
    request->send(503, "application/json", "{\"error\":\"busy\"}");
    return;
  }
  
  server_busy = true;
  
  // FIXED: Smaller JSON buffer
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    request->send(400, "application/json", "{\"error\":\"json\"}");
    server_busy = false;
    return;
  }
  
  if (doc.containsKey("sentiment")) {
    String sentiment = doc["sentiment"].as<String>();
    
    // Queue task - FIXED: Simple assignment instead of complex queue
    currentTask.has_emotion_task = true;
    currentTask.emotion_data = sentiment;
    
    request->send(200, "application/json", "{\"status\":\"ok\"}");
    Serial.print("📥 Emotion queued: ");
    Serial.println(sentiment);
  } else {
    request->send(400, "application/json", "{\"error\":\"missing\"}");
  }
  
  server_busy = false;
}

void handleHeadRequest(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  if (server_busy || emotion_mode_active) {
    request->send(503, "application/json", "{\"status\":\"blocked\"}");
    return;
  }
  
  server_busy = true;
  
  DynamicJsonDocument doc(128);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    request->send(400, "application/json", "{\"error\":\"json\"}");
    server_busy = false;
    return;
  }
  
  if (doc.containsKey("angle")) {
    int angle = doc["angle"].as<int>();
    currentTask.has_head_task = true;
    currentTask.head_angle = angle;
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  } else if (doc.containsKey("number")) {
    int people = doc["number"].as<int>();
    // Simple people-based head positioning
    int angle = 90 + (people > 1 ? (people - 1) * 5 : 0);
    currentTask.has_head_task = true;
    currentTask.head_angle = angle;
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    request->send(400, "application/json", "{\"error\":\"missing\"}");
  }
  
  server_busy = false;
}

void handleResetRequest(AsyncWebServerRequest *request) {
  current_emotion = "happy";
  emotion_mode_active = false;
  head_tracking_enabled = true;
  
  updateEyeDesign();
  updateArmPose();
  moveServoSafely(headServo, headState, 90);
  
  request->send(200, "application/json", "{\"status\":\"reset\"}");
  Serial.println("🔄 Reset requested");
}

// FIXED: Lightweight status response
void handleStatusRequest(AsyncWebServerRequest *request) {
  String status = "{\"emotion\":\"" + current_emotion + "\",";
  status += "\"head\":" + String(headState.currentPos) + ",";
  status += "\"heap\":" + String(ESP.getFreeHeap()) + "}";
  
  request->send(200, "application/json", status);
}

void setup() {
  Serial.begin(115200);
  Serial.println("🚀 Starting ESP32 Robot Controller (Fixed Version)...");
  
  // FIXED: More conservative I2C initialization
  wireLeft.begin(OLED_LEFT_SDA, OLED_LEFT_SCL);
  wireRight.begin(OLED_RIGHT_SDA, OLED_RIGHT_SCL);
  
  // FIXED: Added delays between OLED initializations
  delay(100);
  
  if (!displayLeft.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("❌ Left OLED failed");
  } else {
    Serial.println("✅ Left OLED OK");
  }
  
  delay(100);
  
  if (!displayRight.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    Serial.println("❌ Right OLED failed");
  } else {
    Serial.println("✅ Right OLED OK");
  }
  
  // FIXED: Safer servo initialization with longer delays
  headServo.attach(SERVO_HEAD);
  delay(200);
  leftArmServo.attach(SERVO_LEFT_ARM);
  delay(200);
  rightArmServo.attach(SERVO_RIGHT_ARM);
  delay(200);
  
  // Initialize to safe positions
  headServo.write(90);
  delay(500);
  leftArmServo.write(180);
  delay(500);
  rightArmServo.write(0);
  delay(500);
  
  Serial.println("✅ Servos initialized safely");
  
  // Display initial eyes
  updateEyeDesign();
  
  // FIXED: More robust WiFi setup
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false); // CHANGED: Disable flash writes
  
  Serial.print("🌐 Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Yield to prevent watchdog
    yield();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi failed - continuing anyway");
  }
  
  // FIXED: Simplified server setup
  server.on("/receive", HTTP_POST, 
    [](AsyncWebServerRequest *request){
      request->send(400, "application/json", "{\"error\":\"no_body\"}");
    }, nullptr, handleReceiveRequest);
  
  server.on("/head", HTTP_POST, 
    [](AsyncWebServerRequest *request){
      request->send(400, "application/json", "{\"error\":\"no_body\"}");
    }, nullptr, handleHeadRequest);
  
  server.on("/reset", HTTP_POST, handleResetRequest);
  server.on("/status", HTTP_GET, handleStatusRequest);
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "pong");
  });
  
  server.begin();
  Serial.println("✅ Server started - Robot ready!");
}

void loop() {
  unsigned long currentTime = millis();
  
  // Core functions only
  processSimpleTasks();
  updateServoPositions();
  checkEmotionTimeout();
  
  // FIXED: Simpler blinking
  static unsigned long lastBlink = 0;
  if (!isBlinking && (currentTime - lastBlink > blinkInterval)) {
    isBlinking = true;
    lastBlink = currentTime;
    showBlink();
  }
  
  if (isBlinking && (currentTime - lastBlink > blinkDuration)) {
    isBlinking = false;
    updateEyeDesign();
  }
  
  // FIXED: Less frequent WiFi checks
  if (currentTime - last_wifi_check > WIFI_CHECK_INTERVAL) {
    reconnectWiFi();
    last_wifi_check = currentTime;
  }
  
  // FIXED: Minimal debug output
  static unsigned long lastDebug = 0;
  if (currentTime - lastDebug > 60000) { // Every minute
    Serial.print("🔍 Status - Emotion: ");
    Serial.print(emotion_mode_active ? "ON" : "OFF");
    Serial.print(", Head: ");
    Serial.print(headState.currentPos);
    Serial.print("°, Heap: ");
    Serial.println(ESP.getFreeHeap());
    lastDebug = currentTime;
  }
  
  // FIXED: Longer delay for stability
  delay(20);
}