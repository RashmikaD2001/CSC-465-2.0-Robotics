import cv2
import time
import requests
from ultralytics import YOLO
import numpy as np
from camera_manager import camera_manager
import os
import threading

# MISSING GLOBAL VARIABLES - FIXED
ESP32 = os.getenv('ESP32_URL')
ESP32_URL = f"http://{ESP32}/head"  # Use /head endpoint for head tracking

model = YOLO("yolov8n.pt")

# Global control variables
head_tracking_active = True
emotion_mode_active = False
last_people_count = -1  # Track previous count to avoid unnecessary requests
last_successful_send = 0
REQUEST_COOLDOWN = 1.0  # Reduced from 2.0 seconds for more responsive movement

# Thread lock for coordination - THIS WAS MISSING
tracking_lock = threading.Lock()

def detect_people_from_frame(frame):
    """Detect people in frame with debugging info - MODIFIED to use passed frame"""
    try:
        # Resize frame for faster processing
        height, width = frame.shape[:2]
        if width > 640:
            scale = 640 / width
            new_width = int(width * scale)
            new_height = int(height * scale)
            frame = cv2.resize(frame, (new_width, new_height))
        
        results = model(frame, verbose=False)
        people_count = 0
        
        for result in results:
            if result.boxes is not None and len(result.boxes) > 0:
                classes = result.boxes.cls.cpu().numpy()
                confidences = result.boxes.conf.cpu().numpy()
                
                for i, class_id in enumerate(classes):
                    if int(class_id) == 0 and confidences[i] > 0.6:  # Increased confidence threshold
                        people_count += 1
                
                # Only print debug info occasionally
                if time.time() - getattr(detect_people_from_frame, 'last_debug', 0) > 10:
                    print(f"Head tracking - People detected: {people_count}")
                    detect_people_from_frame.last_debug = time.time()
        
        return people_count
        
    except Exception as e:
        print(f"Error in detection: {e}")
        return 0

def send_people_count_to_esp32(number):
    """Send people count to ESP32 with improved error handling and rate limiting - FIXED"""
    global last_people_count, last_successful_send, emotion_mode_active
    
    with tracking_lock:
        # Skip if in emotion mode
        if emotion_mode_active:
            return False
            
        # Rate limiting - don't send too frequently
        current_time = time.time()
        if current_time - last_successful_send < REQUEST_COOLDOWN:
            return False
        
        # REMOVED duplicate count check for continuous movement
    
    data = {"number": number}
    
    try:
        # Shorter timeout for head tracking to avoid blocking
        resp = requests.post(ESP32_URL, json=data, timeout=1.5)
        
        if resp.status_code == 200:
            last_people_count = number
            last_successful_send = current_time
            # Only log occasionally to reduce spam
            if current_time % 5 < 1:
                print(f"Head tracking active: {number} people")
            return True
        else:
            if current_time % 10 < 1:  # Log errors less frequently
                print(f"ESP32 head tracking failed: {resp.status_code}")
            return False
            
    except requests.exceptions.Timeout:
        # Don't log timeout errors for head tracking (expected during emotion processing)
        return False
    except requests.exceptions.ConnectionError:
        if current_time % 15 < 1:  # Log connection errors even less frequently
            print("Head tracking connection error")
        return False
    except Exception as e:
        print(f"Head tracking error: {e}")
        return False

def pause_head_tracking():
    """Pause head tracking for emotion processing"""
    global emotion_mode_active
    with tracking_lock:
        emotion_mode_active = True
    print("Head tracking PAUSED for emotion")

def resume_head_tracking():
    """Resume head tracking after emotion processing"""
    global emotion_mode_active, last_people_count
    with tracking_lock:
        emotion_mode_active = False
        last_people_count = -1  # Reset to force next detection
    print("Head tracking RESUMED")

def track_head_loop():
    """Main tracking loop - FIXED with proper globals and error handling"""
    global head_tracking_active
    
    print("Starting coordinated head tracking with ALWAYS-ON movement...")
    detection_failures = 0
    max_failures = 5
    
    # Force initial movement after system startup
    time.sleep(2)  # Let system initialize
    
    # Send initial count to get movement started
    try:
        resp = requests.post(ESP32_URL, json={"number": 0}, timeout=1)
        print("✅ Initial head movement triggered")
    except Exception as e:
        print(f"❌ Failed to send initial head movement: {e}")
    
    while head_tracking_active:
        try:
            # Check if we should be tracking
            with tracking_lock:
                if emotion_mode_active:
                    time.sleep(0.5)  # Wait while emotion is processing
                    continue
            
            # Get frame from shared camera manager
            frame = camera_manager.get_frame()
            if frame is None:
                detection_failures += 1
                if detection_failures > max_failures:
                    print("Too many camera failures, pausing head tracking")
                    time.sleep(5)
                    detection_failures = 0
                continue
            
            detection_failures = 0  # Reset on successful frame
            
            # Detect people
            count = detect_people_from_frame(frame)
            
            # ALWAYS send to ESP32 (with built-in rate limiting)
            # ESP32 will handle continuous movement logic
            send_people_count_to_esp32(count)
            
            # Shorter sleep for more responsive movement
            time.sleep(1.0)  # Reduced from 1.5 seconds
            
        except Exception as e:
            print(f"Error in head tracking loop: {e}")
            time.sleep(2)  # Longer wait on error

def start_head_tracking_thread():
    """Start head tracking in a daemon thread"""
    head_thread = threading.Thread(target=track_head_loop, daemon=True)
    head_thread.start()
    return head_thread

def stop_head_tracking():
    """Stop head tracking gracefully"""
    global head_tracking_active
    head_tracking_active = False
    print("Head tracking stopped")

# Export the control functions
__all__ = ['start_head_tracking_thread', 'pause_head_tracking', 'resume_head_tracking', 'stop_head_tracking']