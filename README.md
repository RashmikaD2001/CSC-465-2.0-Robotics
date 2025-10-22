# 🤖 HarmonyBot: A Conflict Mediating Social Robot

HarmonyBot is an innovative **social robot** designed to mediate interpersonal conflicts through **emotion recognition**, **sentiment analysis**, and **empathetic dialogue**.  
It integrates **AI, computer vision, natural language processing, and robotics hardware** to detect emotional tension and promote calm, respectful communication.

---

## 🧠 Project Overview

HarmonyBot represents an interdisciplinary blend of **robotics and emotional intelligence**, aiming to:

- Detect emotional tones in conversations using **sentiment analysis** and **facial emotion recognition**
- Understand and mediate real-time conflicts through **empathetic AI responses**
- Express emotions physically via **OLED eyes, LED lights, and servo gestures**

The robot’s intervention strategies are **non-intrusive**, **context-aware**, and **emotionally adaptive**, fostering understanding between individuals.

---

## 🏗️ System Architecture

HarmonyBot is built on a **modular architecture** with three main subsystems:

### 1. Input Processing
- Captures data from camera and microphone sensors
- Detects human presence and records conversations

### 2. AI Models
- **DeepFace** → Emotion recognition  
- **Speech-to-Text** → Transcribes conversations using Azure AI Speech
- **Text-to-Speech** → Convert responses to human-like speech audio using Azure AI Speecg
- **Qwen2.5-7B** → Generates natural responses  
- **bge-m3-zeroshot-v2.0** → Performs sentiment analysis  

### 3. Actuator Control (ESP32)
- Controls OLED eyes, head and arm servos, and LED indicators  
- Communicates wirelessly with the Python backend for real-time reactions  

---

## ⚙️ Hardware Design

| Component | Description |
|------------|-------------|
| **ESP32** | Central controller managing servos, LEDs, and OLEDs |
| **MG90S Servos** | Three servos (Head: GPIO13, Left Arm: GPIO14, Right Arm: GPIO27) |
| **OLED Displays (SSD1306 128×64)** | Dual expressive eyes (Left: I2C0 - 0x3C, Right: I2C1 - 0x3D) |
| **LED Strip** | Indicates emotional states (calm = blue, conflict = red, happy = green) |
| **Power** | USB or battery operation (portable deployment supported) |

All servos feature **step-based interpolation** for smooth and realistic motion.

---

## 💻 Software Implementation

### 🐍 Python Backend
Implements the robot’s cognitive functions:
- Emotion recognition with **DeepFace**
- Human detection using **YOLO**
- Conversation management via **LangGraph**
- Natural language reasoning with **Qwen2.5-7B**
- Sentiment analysis through **transformer models**

### 🔧 ESP32 Firmware
Handles all **hardware controls** and exposes REST API endpoints for:
- Emotion display  
- Head and arm movements    

Includes:
- Automatic Wi-Fi reconnection  
- Servo safety boundaries    

---

## ⚡ Real-Time Processing Pipeline

1. Capture image + audio  
2. Detect humans (YOLO)  
3. Recognize emotions (DeepFace)  
4. Analyze sentiment (text-based)  
5. Generate mediation response (Qwen2.5-7B)  
6. Control physical expression via ESP32  

---

## 🧩 Tech Stack

| Layer | Technology |
|--------|-------------|
| **Backend AI** | Python, HuggingFace Transformers, DeepFace, YOLO, LangGraph |
| **Hardware Control** | ESP32 (Arduino C++) |
| **Communication** | HTTP / Wi-Fi |
| **Display & Motion** | SSD1306 OLEDs, MG90S Servos, LED Strip |

---
