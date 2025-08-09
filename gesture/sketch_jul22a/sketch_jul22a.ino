#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

const char* ssid = "SLT FIBRE HOME";
const char* password = "0112516405";

WebServer server(80);
Servo myServo;

// Head tracking variables
bool person_detected = false;
unsigned long lastMoveTime = 0;
unsigned long lastHeadDataReceived = 0;
bool sweepingRight = true;
int angle = 90;
int people_count = 0;

// Emotion variables
String current_emotion = "neutral";
String current_sentiment = "neutral";
unsigned long lastEmotionDataReceived = 0;

// Debouncing variables
const unsigned long DATA_TIMEOUT = 5000;  // 5 seconds timeout
const unsigned long DETECTION_DEBOUNCE = 1000;  // 1 second debounce
bool stable_person_detected = false;
unsigned long detection_change_time = 0;

// Handle head tracking data (people count only)
void handleHeadTracking() {
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (!error) {
      // Check if this is head tracking data (has "number" field)
      if (doc.containsKey("number")) {
        people_count = doc["number"];
        lastHeadDataReceived = millis();
        
        Serial.print("HEAD TRACKING - People count: ");
        Serial.println(people_count);
        
        // Debounce the detection
        bool new_detection = (people_count > 0);
        if (new_detection != person_detected) {
          detection_change_time = millis();
          person_detected = new_detection;
        }
        
        server.send(200, "application/json", "{\"status\":\"head_data_received\"}");
      }
      // Check if this is emotion data
      else if (doc.containsKey("emotion") && doc.containsKey("sentiment")) {
        current_emotion = doc["emotion"].as<String>();
        current_sentiment = doc["sentiment"].as<String>();
        lastEmotionDataReceived = millis();
        
        Serial.print("EMOTION DATA - Emotion: ");
        Serial.print(current_emotion);
        Serial.print(", Sentiment: ");
        Serial.println(current_sentiment);
        
        server.send(200, "application/json", "{\"status\":\"emotion_data_received\"}");
      }
      else {
        Serial.println("Unknown data format");
        server.send(400, "application/json", "{\"status\":\"unknown_format\"}");
      }
    } else {
      Serial.println("JSON parsing error");
      server.send(400, "application/json", "{\"status\":\"bad_json\"}");
    }
  } else {
    server.send(405, "application/json", "{\"status\":\"method_not_allowed\"}");
  }
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Single endpoint that handles both types of data
  server.on("/receive", HTTP_POST, handleHeadTracking);
  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Endpoints:");
  Serial.println("  POST /receive - Accepts both head tracking and emotion data");

  myServo.setPeriodHertz(50);
  myServo.attach(13, 500, 2400);
  myServo.write(angle);
  delay(1000);
  
  lastHeadDataReceived = millis();
  Serial.println("Setup complete - ready for dual API");
}

void loop() {
  server.handleClient();
  
  unsigned long now = millis();
  
  // Check if we haven't received head tracking data recently
  bool head_data_fresh = (now - lastHeadDataReceived) < DATA_TIMEOUT;
  bool emotion_data_fresh = (now - lastEmotionDataReceived) < DATA_TIMEOUT;
  
  // Apply debouncing to person detection
  if (now - detection_change_time > DETECTION_DEBOUNCE) {
    stable_person_detected = person_detected;
  }
  
  // SERVO CONTROL LOGIC (based only on head tracking data)
  if (!head_data_fresh) {
    // No recent head tracking data - sweep slowly
    if (now - lastMoveTime > 100) {
      angle += sweepingRight ? 2 : -2;
      
      if (angle >= 180) {
        angle = 180;
        sweepingRight = false;
      } else if (angle <= 0) {
        angle = 0;
        sweepingRight = true;
      }
      
      myServo.write(angle);
      lastMoveTime = now;
    }
  } else if (!stable_person_detected) {
    // Recent head data but no person detected - normal sweep
    if (now - lastMoveTime > 50) {
      angle += sweepingRight ? 3 : -3;
      
      if (angle >= 180) {
        angle = 180;
        sweepingRight = false;
      } else if (angle <= 0) {
        angle = 0;
        sweepingRight = true;
      }
      
      myServo.write(angle);
      lastMoveTime = now;
    }
  } else {
    // Person detected - stop at center
    if (abs(angle - 90) > 5) {
      if (now - lastMoveTime > 30) {
        if (angle > 90) {
          angle -= 2;
        } else {
          angle += 2;
        }
        myServo.write(angle);
        lastMoveTime = now;
      }
    }
  }
  
  // Debug output every 3 seconds
  static unsigned long lastDebugTime = 0;
  if (now - lastDebugTime > 3000) {
    Serial.println("=== STATUS ===");
    Serial.print("HEAD TRACKING - People: ");
    Serial.print(people_count);
    Serial.print(", Detected: ");
    Serial.print(stable_person_detected ? "YES" : "NO");
    Serial.print(", Fresh: ");
    Serial.println(head_data_fresh ? "YES" : "NO");
    
    Serial.print("EMOTION DATA - Emotion: ");
    Serial.print(current_emotion);
    Serial.print(", Sentiment: ");
    Serial.print(current_sentiment);
    Serial.print(", Fresh: ");
    Serial.println(emotion_data_fresh ? "YES" : "NO");
    
    Serial.print("SERVO - Angle: ");
    Serial.println(angle);
    Serial.println("===============");
    
    lastDebugTime = now;
  }
  
  delay(10);
}