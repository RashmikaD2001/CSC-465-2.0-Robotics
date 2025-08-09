import requests
from fastapi import FastAPI
from pydantic import BaseModel
import os
import time
import threading
import queue

app = FastAPI()
ESP32_IP = os.getenv('ESP32_URL')

# Fix URL format - ensure it has http:// prefix
if ESP32_IP and not ESP32_IP.startswith(('http://', 'https://')):
    ESP32_IP = f"http://{ESP32_IP}"

print(f"ESP32 URL configured as: {ESP32_IP}")

# Enhanced rate limiting with priority queues
emotion_request_lock = threading.Lock()
head_request_lock = threading.Lock()
last_emotion_request = 0
last_head_request = 0

# Minimum intervals (emotion gets priority)
EMOTION_MIN_INTERVAL = 0.1    # 100ms between emotion requests
HEAD_MIN_INTERVAL = 1.0       # 1 second between head requests (much longer)

# Request queues for better handling
emotion_queue = queue.Queue(maxsize=5)  # Small queue for emotions
head_queue = queue.Queue(maxsize=2)     # Very small queue for head tracking

class Sentiment(BaseModel):
    sentiment: str

class HeadPosition(BaseModel):
    angle: int
    number: int = None  # Optional people count

def priority_rate_limiter(request_type="emotion"):
    """Enhanced rate limiter with priority for emotions"""
    def decorator(func):
        def wrapper(*args, **kwargs):
            global last_emotion_request, last_head_request
            
            current_time = time.time()
            
            if request_type == "emotion":
                with emotion_request_lock:
                    time_since_last = current_time - last_emotion_request
                    if time_since_last < EMOTION_MIN_INTERVAL:
                        time.sleep(EMOTION_MIN_INTERVAL - time_since_last)
                    
                    result = func(*args, **kwargs)
                    last_emotion_request = time.time()
                    return result
                    
            else:  # head tracking
                with head_request_lock:
                    time_since_last = current_time - last_head_request
                    if time_since_last < HEAD_MIN_INTERVAL:
                        # For head tracking, skip the request if too recent
                        print(f"Skipping head request (too frequent)")
                        return {
                            "status": False,
                            "message": "Rate limited - skipped"
                        }
                    
                    result = func(*args, **kwargs)
                    last_head_request = time.time()
                    return result
        
        return wrapper
    return decorator

@app.post("/")
@priority_rate_limiter("emotion")
async def send_emotion(data: Sentiment):
    """Send emotion data to ESP32 - HIGHEST PRIORITY"""
    try:
        print(f"PRIORITY: Sending emotion to ESP32: {data.sentiment}")
        
        # Clear any pending head requests to prioritize emotion
        try:
            while not head_queue.empty():
                head_queue.get_nowait()
        except:
            pass
        
        resp = requests.post(
            f"{ESP32_IP}/receive",
            json=data.dict(),
            timeout=5,  # Longer timeout for emotion requests
            headers={'Content-Type': 'application/json'}
        )
        
        if resp.status_code == 200:
            print(f"Emotion sent successfully: {data.sentiment}")
            return {
                "esp32_response": resp.json(),
                "status": True
            }
        else:
            print(f"ESP32 returned status code: {resp.status_code}")
            return {
                "error": f"ESP32 error: {resp.status_code}",
                "status": False
            }
            
    except requests.exceptions.Timeout:
        print("ESP32 emotion request timed out")
        return {
            "error": "ESP32 timeout",
            "status": False
        }
    except Exception as e:
        print(f"Error sending emotion to ESP32: {e}")
        return {
            "error": str(e),
            "status": False
        }

# Additional helper function for direct calls
async def send_emotion_direct(sentiment: str):
    """Direct function to send emotion without needing the Sentiment class"""
    try:
        print(f"DIRECT: Sending emotion to ESP32: {sentiment}")
        
        resp = requests.post(
            f"{ESP32_IP}/receive",
            json={"sentiment": sentiment},
            timeout=5,
            headers={'Content-Type': 'application/json'}
        )
        
        if resp.status_code == 200:
            print(f"Emotion sent successfully: {sentiment}")
            return {
                "esp32_response": resp.json(),
                "status": True
            }
        else:
            print(f"ESP32 returned status code: {resp.status_code}")
            return {
                "error": f"ESP32 error: {resp.status_code}",
                "status": False
            }
            
    except requests.exceptions.Timeout:
        print("ESP32 emotion request timed out")
        return {
            "error": "ESP32 timeout",
            "status": False
        }
    except Exception as e:
        print(f"Error sending emotion to ESP32: {e}")
        return {
            "error": str(e),
            "status": False
        }

@app.post("/head")
@priority_rate_limiter("head")
async def send_head_position(data: HeadPosition):
    """Send head position to ESP32 - LOWER PRIORITY, heavily rate limited"""
    try:
        # Prepare data - handle both angle and number
        request_data = {}
        if data.angle is not None:
            request_data["angle"] = data.angle
        if data.number is not None:
            request_data["number"] = data.number
        
        if not request_data:
            return {"error": "No angle or number provided", "status": False}
        
        resp = requests.post(
            f"{ESP32_IP}/head",
            json=request_data,
            timeout=1,  # Very short timeout for head requests
            headers={'Content-Type': 'application/json'}
        )
        
        if resp.status_code == 200:
            # Only log successful head tracking occasionally
            if time.time() % 10 < 1:  # Log every ~10 seconds
                print(f"Head tracking: {request_data}")
            
            return {
                "esp32_response": resp.json(),
                "status": True
            }
        else:
            return {
                "error": f"Head request failed: {resp.status_code}",
                "status": False
            }
            
    except requests.exceptions.Timeout:
        # Don't log timeout errors for head tracking (expected during emotion processing)
        return {
            "error": "timeout",
            "status": False
        }
    except Exception as e:
        return {
            "error": str(e),
            "status": False
        }

@app.post("/reset")
@priority_rate_limiter("emotion")  # Reset has same priority as emotion
async def reset_to_default():
    """Reset ESP32 to default state"""
    try:
        print("Sending reset to ESP32...")
        
        resp = requests.post(
            f"{ESP32_IP}/reset",
            json={"action": "reset"},
            timeout=5,  # Longer timeout for reset
            headers={'Content-Type': 'application/json'}
        )
        
        if resp.status_code == 200:
            print("ESP32 reset successful")
            return {
                "esp32_response": resp.json(),
                "status": True
            }
        else:
            return {
                "error": f"Reset failed: {resp.status_code}",
                "status": False
            }
    except Exception as e:
        print(f"Error resetting ESP32: {e}")
        return {
            "error": str(e),
            "status": False
        }

# Health check endpoint
@app.get("/health")
async def health_check():
    """Check if the service is running"""
    return {
        "status": "healthy",
        "last_emotion_request": last_emotion_request,
        "last_head_request": last_head_request
    }


# python -m uvicorn esp32:app --reload
