import asyncio
import os
import requests
from capture import capture_both_simultaneously
from sentiment import sentiment_of_conversation
from agent import output_of_model
from tts import speak_text
from analysis import run_parallel_analysis
from filter import apply_median_filter
from head_tracker import start_head_tracking_thread, pause_head_tracking, resume_head_tracking, stop_head_tracking
from camera_manager import camera_manager
from emotion import is_negative_emotion  # Import the new function
import cv2

# ESP32 Configuration - FIXED
ESP32_URL = os.getenv('ESP32_URL')
if not ESP32_URL:
    print("ERROR: ESP32_URL environment variable not set!")
    print("Set it with: export ESP32_URL='192.168.1.100'")
    exit(1)

if not ESP32_URL.startswith('http'):
    ESP32_URL = f"http://{ESP32_URL}"

print(f"ESP32 URL configured as: {ESP32_URL}")

class ESP32Client:
    """Enhanced ESP32 client with better error handling"""
    
    def __init__(self, base_url: str):
        self.base_url = base_url.rstrip('/')
        
    async def send_emotion(self, sentiment: str, max_retries: int = 3) -> bool:
        """Send emotion to ESP32 with retry logic"""
        for attempt in range(max_retries):
            try:
                print(f"Sending emotion: {sentiment} (attempt {attempt + 1}/{max_retries})")
                
                response = requests.post(
                    f"{self.base_url}/receive",
                    json={"sentiment": sentiment},
                    headers={'Content-Type': 'application/json'},
                    timeout=8
                )
                
                if response.status_code == 200:
                    print(f"Emotion '{sentiment}' sent successfully!")
                    return True
                else:
                    print(f"ESP32 returned status {response.status_code}: {response.text}")
                    
            except requests.exceptions.Timeout:
                print(f"Emotion request timeout (attempt {attempt + 1})")
            except requests.exceptions.ConnectionError:
                print(f"Connection error (attempt {attempt + 1})")
            except Exception as e:
                print(f"Unexpected error sending emotion: {e}")
            
            if attempt < max_retries - 1:
                await asyncio.sleep(1)
        
        print(f"Failed to send emotion '{sentiment}' after {max_retries} attempts")
        return False
    
    async def reset_robot(self, max_retries: int = 2) -> bool:
        """Reset ESP32 to default state - FIXED TIMING"""
        for attempt in range(max_retries):
            try:
                print(f"Resetting ESP32 (attempt {attempt + 1}/{max_retries})")
                
                response = requests.post(
                    f"{self.base_url}/reset",
                    timeout=6
                )
                
                if response.status_code == 200:
                    print("ESP32 reset successful!")
                    return True
                else:
                    print(f"Reset failed with status {response.status_code}")
                    
            except Exception as e:
                print(f"Reset error: {e}")
            
            if attempt < max_retries - 1:
                await asyncio.sleep(1)
        
        print("Failed to reset ESP32")
        return False
    
    async def test_connection(self) -> bool:
        """Test if ESP32 is reachable"""
        try:
            response = requests.get(f"{self.base_url}/ping", timeout=3)
            return response.status_code == 200
        except:
            return False

def should_process_emotion_response(sentiment: str, emotions_dict: dict) -> tuple[bool, str]:
    """
    Enhanced decision logic for emotion processing
    
    Returns:
        tuple: (should_process, reason)
    """
    # Step 1: Check if sentiment is negative
    sentiment_is_negative = sentiment_of_conversation(sentiment=sentiment)
    
    # Step 2: Check if facial emotions are negative
    emotion_is_negative = is_negative_emotion(emotions_dict)
    
    if sentiment_is_negative and emotion_is_negative:
        return True, "Both sentiment and emotion are negative"
    elif sentiment_is_negative and not emotion_is_negative:
        return True, "Sentiment is negative (emotion positive/neutral)"
    elif not sentiment_is_negative and emotion_is_negative:
        return True, "Emotion is negative (sentiment positive)"
    else:
        return False, "Both sentiment and emotion are positive/neutral"

async def speak_text_async(text: str):
    """Async wrapper for speak_text that we can await properly"""
    return await asyncio.to_thread(speak_text, text)

async def main():
    """Main conversation loop with ENHANCED EMOTION-SENTIMENT LOGIC"""
    
    # Initialize ESP32 client
    esp32_client = ESP32Client(ESP32_URL)
    
    # Test ESP32 connection at startup
    print("Testing ESP32 connection...")
    if not await esp32_client.test_connection():
        print("Cannot connect to ESP32! Please check:")
        print("1. ESP32 IP address is correct")
        print("2. ESP32 is powered on and running")
        print("3. Both devices are on same WiFi network")
        return
    
    print("ESP32 connection successful!")
    
    try:
        print("Starting system with coordinated head tracking...")
        
        # STEP 1: Start the shared camera manager
        print("Starting camera manager...")
        camera_manager.start()
        await asyncio.sleep(2)
        print("Camera manager started successfully")
        
        # STEP 2: Start coordinated head tracking
        print("Starting head tracking thread...")
        head_thread = start_head_tracking_thread()
        await asyncio.sleep(1)
        print("Head tracking started and running in background")
        
        # STEP 3: Main conversation loop
        print("Starting main conversation loop...")
        conversation_count = 0
        
        while True:
            try:
                conversation_count += 1
                print(f"\n---  Conversation {conversation_count} ---")
                
                # Capture audio and image
                print("Capturing audio and image...")
                text = await asyncio.to_thread(capture_both_simultaneously)
                
                if not text or text.strip() == "":
                    print("No text captured, continuing...")
                    await asyncio.sleep(1)
                    continue
                
                print(f"Captured text: {text[:100]}...")
                
                # Apply median filter to image
                print("Applying median filter...")
                await asyncio.to_thread(apply_median_filter)
                
                # Run emotion and sentiment analysis
                print("Running emotion and sentiment analysis...")
                emotions, sentiment = await asyncio.to_thread(
                    run_parallel_analysis, 'final_image.jpg', text
                )
                print(f"Detected sentiment: {sentiment}")
                print(f"Detected emotions: {emotions}")
                
                # ENHANCED LOGIC: Check both sentiment and emotion
                should_process, reason = should_process_emotion_response(sentiment, emotions)
                print(f"Decision: {reason}")
                
                if should_process:
                    print(f"Processing emotion response - Reason: {reason}")
                    
                    # STEP A: Pause head tracking IMMEDIATELY
                    print("Pausing head tracking for emotion processing...")
                    pause_head_tracking()
                    await asyncio.sleep(0.5)
                    
                    # STEP B: Send emotion data (use sentiment for consistency with ESP32)
                    emotion_sent = await esp32_client.send_emotion(sentiment)
                    
                    if not emotion_sent:
                        print("Emotion sending failed, but continuing...")
                    
                    # STEP C: Generate response
                    print("Generating response...")
                    response = await asyncio.to_thread(output_of_model, text, emotions)
                    print(f"Response: {response[:100]}...")
                    
                    # STEP D: Speak response and WAIT for completion
                    print("Speaking response...")
                    await speak_text_async(response)
                    print("Speech completed!")
                    
                    # STEP E: Wait a moment after speech completes
                    print("Waiting 2 seconds after speech completion...")
                    await asyncio.sleep(0.5)
                    
                    # STEP F: NOW send reset AFTER speech is completely done
                    print("Sending reset acknowledgment AFTER speech completion...")
                    
                    # STEP F: Wait 3 seconds, THEN send reset AFTER speech is completely done
                    print("Waiting 1 seconds before sending reset acknowledgment...")
                    await asyncio.sleep(0.5)
                    
                    print("Sending reset acknowledgment AFTER speech completion...")
                    reset_success = await esp32_client.reset_robot()
                    if reset_success:
                        print("Reset acknowledgment sent successfully after speech!")
                    else:
                        print("Reset acknowledgment failed, but continuing...")
                    
                    # STEP G: Wait before resuming head tracking
                    print("Waiting 1 more second before resuming head tracking...")
                    await asyncio.sleep(0.2)
                    
                    print("Resuming head tracking...")
                    resume_head_tracking()
                    
                    print("Complete emotion processing sequence finished!\n")
                
                else:
                    print("No negative emotion or sentiment detected, continuing normal operation...")
                
                # Small delay before next iteration
                await asyncio.sleep(0.5)
                
            except KeyboardInterrupt:
                print("\nKeyboard interrupt detected...")
                break
            except Exception as e:
                print(f"Error in main conversation loop: {e}")
                print("Ensuring head tracking is resumed after error...")
                resume_head_tracking()
                await asyncio.sleep(1)
                continue
        
    except KeyboardInterrupt:
        print("\nShutting down system...")
    except Exception as e:
        print(f"Critical error in main: {e}")
    finally:
        # STEP 4: Clean shutdown
        print("Cleaning up...")
        
        print("Stopping head tracking...")
        stop_head_tracking()
        
        print("Stopping camera manager...")
        camera_manager.stop()
        
        print("Closing OpenCV windows...")
        cv2.destroyAllWindows()
        
        print("Shutdown complete!")

if __name__ == "__main__":
    # Run the async main function
    asyncio.run(main())