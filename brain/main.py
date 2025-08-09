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
import cv2

# ESP32 Configuration - FIXED
ESP32_URL = os.getenv('ESP32_URL')
if not ESP32_URL:
    print("❌ ERROR: ESP32_URL environment variable not set!")
    print("Set it with: export ESP32_URL='192.168.1.100'")
    exit(1)

if not ESP32_URL.startswith('http'):
    ESP32_URL = f"http://{ESP32_URL}"

print(f"🤖 ESP32 URL configured as: {ESP32_URL}")

class ESP32Client:
    """Enhanced ESP32 client with better error handling"""
    
    def __init__(self, base_url: str):  # FIXED: Changed from _init_ to __init__
        self.base_url = base_url.rstrip('/')
        
    async def send_emotion(self, sentiment: str, max_retries: int = 3) -> bool:
        """Send emotion to ESP32 with retry logic"""
        for attempt in range(max_retries):
            try:
                print(f"📤 Sending emotion: {sentiment} (attempt {attempt + 1}/{max_retries})")
                
                response = requests.post(
                    f"{self.base_url}/receive",
                    json={"sentiment": sentiment},
                    headers={'Content-Type': 'application/json'},
                    timeout=8  # Increased timeout for emotion requests
                )
                
                if response.status_code == 200:
                    print(f"✅ Emotion '{sentiment}' sent successfully!")
                    return True
                else:
                    print(f"❌ ESP32 returned status {response.status_code}: {response.text}")
                    
            except requests.exceptions.Timeout:
                print(f"⏰ Emotion request timeout (attempt {attempt + 1})")
            except requests.exceptions.ConnectionError:
                print(f"🔌 Connection error (attempt {attempt + 1})")
            except Exception as e:
                print(f"❌ Unexpected error sending emotion: {e}")
            
            if attempt < max_retries - 1:
                await asyncio.sleep(1)  # Wait before retry
        
        print(f"💥 Failed to send emotion '{sentiment}' after {max_retries} attempts")
        return False
    
    async def reset_robot(self, max_retries: int = 2) -> bool:
        """Reset ESP32 to default state"""
        for attempt in range(max_retries):
            try:
                print(f"🔄 Resetting ESP32 (attempt {attempt + 1}/{max_retries})")
                
                response = requests.post(
                    f"{self.base_url}/reset",
                    timeout=6
                )
                
                if response.status_code == 200:
                    print("✅ ESP32 reset successful!")
                    return True
                else:
                    print(f"❌ Reset failed with status {response.status_code}")
                    
            except Exception as e:
                print(f"❌ Reset error: {e}")
            
            if attempt < max_retries - 1:
                await asyncio.sleep(1)
        
        print("💥 Failed to reset ESP32")
        return False
    
    async def test_connection(self) -> bool:
        """Test if ESP32 is reachable"""
        try:
            response = requests.get(f"{self.base_url}/ping", timeout=3)
            return response.status_code == 200
        except:
            return False

async def main():
    """Main conversation loop with improved ESP32 communication"""
    
    # Initialize ESP32 client
    esp32_client = ESP32Client(ESP32_URL)
    
    # Test ESP32 connection at startup
    print("🔍 Testing ESP32 connection...")
    if not await esp32_client.test_connection():
        print("❌ Cannot connect to ESP32! Please check:")
        print("1. ESP32 IP address is correct")
        print("2. ESP32 is powered on and running")
        print("3. Both devices are on same WiFi network")
        return
    
    print("✅ ESP32 connection successful!")
    
    try:
        print("🚀 Starting system with coordinated head tracking...")
        
        # STEP 1: Start the shared camera manager
        print("📹 Starting camera manager...")
        camera_manager.start()
        await asyncio.sleep(2)
        print("✅ Camera manager started successfully")
        
        # STEP 2: Start coordinated head tracking
        print("🎯 Starting head tracking thread...")
        head_thread = start_head_tracking_thread()
        await asyncio.sleep(1)
        print("✅ Head tracking started and running in background")
        
        # STEP 3: Main conversation loop
        print("💬 Starting main conversation loop...")
        conversation_count = 0
        
        while True:
            try:
                conversation_count += 1
                print(f"\n--- 🗣  Conversation {conversation_count} ---")
                
                # Capture audio and image
                print("🎤 Capturing audio and image...")
                text = await asyncio.to_thread(capture_both_simultaneously)
                
                if not text or text.strip() == "":
                    print("⏭  No text captured, continuing...")
                    await asyncio.sleep(1)
                    continue
                
                print(f"📝 Captured text: {text[:100]}...")
                
                # Apply median filter to image
                print("🔧 Applying median filter...")
                await asyncio.to_thread(apply_median_filter)
                
                # Run emotion and sentiment analysis
                print("🧠 Running emotion and sentiment analysis...")
                emotions, sentiment = await asyncio.to_thread(
                    run_parallel_analysis, 'final_image.jpg', text
                )
                print(f"😊 Detected sentiment: {sentiment}")
                
                # Check if we should show emotion
                should_show_emotion = await asyncio.to_thread(
                    sentiment_of_conversation, sentiment=sentiment
                )
                
                if should_show_emotion:
                    print(f"🎭 Processing emotion: {sentiment}")
                    
                    # STEP A: Pause head tracking IMMEDIATELY
                    print("⏸  Pausing head tracking for emotion processing...")
                    pause_head_tracking()
                    await asyncio.sleep(0.5)
                    
                    # STEP B: Send emotion data with retry - FIXED VERSION
                    emotion_sent = await esp32_client.send_emotion(sentiment)
                    
                    if not emotion_sent:
                        print("⚠  Emotion sending failed, but continuing...")
                    
                    # STEP C: Generate and speak response
                    print("🤖 Generating response...")
                    response = await asyncio.to_thread(output_of_model, text, emotions)
                    print(f"💬 Response: {response[:100]}...")
                    
                    print("🔊 Speaking response...")
                    await asyncio.to_thread(speak_text, response)
                    
                    # STEP D: Reset ESP32 and resume head tracking
                    reset_success = await esp32_client.reset_robot()
                    if not reset_success:
                        print("⚠  Reset failed, but continuing...")
                    
                    # STEP E: Wait before resuming head tracking
                    print("⏳ Waiting before resuming head tracking...")
                    await asyncio.sleep(2)
                    
                    print("▶  Resuming head tracking...")
                    resume_head_tracking()
                    
                    print("✅ Emotion processing complete!\n")
                
                else:
                    print("😐 No strong emotion detected, continuing normal operation...")
                
                # Small delay before next iteration
                await asyncio.sleep(0.5)
                
            except KeyboardInterrupt:
                print("\n⌨  Keyboard interrupt detected...")
                break
            except Exception as e:
                print(f"❌ Error in main conversation loop: {e}")
                print("🔄 Ensuring head tracking is resumed after error...")
                resume_head_tracking()
                await asyncio.sleep(2)
                continue
        
    except KeyboardInterrupt:
        print("\n🛑 Shutting down system...")
    except Exception as e:
        print(f"💥 Critical error in main: {e}")
    finally:
        # STEP 4: Clean shutdown
        print("🧹 Cleaning up...")
        
        print("🛑 Stopping head tracking...")
        stop_head_tracking()
        
        print("📹 Stopping camera manager...")
        camera_manager.stop()
        
        print("🪟 Closing OpenCV windows...")
        cv2.destroyAllWindows()
        
        print("✅ Shutdown complete!")

if __name__ == "__main__":
    # Run the async main function
    asyncio.run(main())