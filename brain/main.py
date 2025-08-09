from capture import capture_both_simultaneously
from sentiment import sentiment_of_conversation
from agent import output_of_model
from tts import speak_text
from analysis import run_parallel_analysis
from filter import apply_median_filter
import esp32
import asyncio
from head_tracker import start_head_tracking_thread, pause_head_tracking, resume_head_tracking, stop_head_tracking
from camera_manager import camera_manager
import cv2
import requests
import os

async def main():  # Make main function async
    try:
        print("Starting system with coordinated head tracking...")
        
        # STEP 1: Start the shared camera manager FIRST
        print("Starting camera manager...")
        camera_manager.start()
        await asyncio.sleep(2)  # Use asyncio.sleep instead of time.sleep
        print("Camera manager started successfully")
        
        # STEP 2: Start coordinated head tracking
        print("Starting head tracking thread...")
        head_thread = start_head_tracking_thread()
        await asyncio.sleep(1)  # Let head tracking initialize
        print("Head tracking started and running in background")
        
        # STEP 3: Main conversation loop with proper coordination
        print("Starting main conversation loop...")
        conversation_count = 0
        
        while True:
            try:
                conversation_count += 1
                print(f"\n--- Conversation {conversation_count} ---")
                
                # Capture audio and image (blocking operations - run in thread pool)
                print("Capturing audio and image...")
                text = await asyncio.to_thread(capture_both_simultaneously)
                
                if not text or text.strip() == "":
                    print("No text captured, continuing...")
                    await asyncio.sleep(1)
                    continue
                
                print(f"📝 Captured text: {text[:100]}...")
                
                # Apply median filter to image (blocking - run in thread pool)
                print("Applying median filter...")
                await asyncio.to_thread(apply_median_filter)
                
                # Run emotion and sentiment analysis (blocking - run in thread pool)
                print("Running emotion and sentiment analysis...")
                emotions, sentiment = await asyncio.to_thread(
                    run_parallel_analysis, 'final_image.jpg', text
                )
                print(f"Detected sentiment: {sentiment}")
                
                # Check if we should show emotion (blocking - run in thread pool)
                should_show_emotion = await asyncio.to_thread(
                    sentiment_of_conversation, sentiment=sentiment
                )
                
                if should_show_emotion:
                    print(f"Processing emotion: {sentiment}")
                    
                    # STEP A: Pause head tracking IMMEDIATELY
                    print("Pausing head tracking for emotion processing...")
                    pause_head_tracking()
                    await asyncio.sleep(0.5)  # Give head tracking time to pause
                    
                    # STEP B: Send emotion data with retry
                    max_emotion_retries = 3
                    emotion_sent = False
                    
                    for attempt in range(max_emotion_retries):
                        try:
                            print(f"Sending emotion (attempt {attempt + 1}/{max_emotion_retries}): {sentiment}")
                            # data = esp32.Sentiment(sentiment=sentiment)
                            # acknowledgement = await esp32.send_emotion(data)

                            response = requests.post(
                                f"http://{os.getenv('ESP32_URL')}/receive",
                                json={"sentiment": sentiment},
                                timeout=5
                            )
                            acknowledgement = {"status": response.status_code == 200}
                            
                            if acknowledgement.get("status", False):
                                print("Emotion sent successfully!")
                                emotion_sent = True
                                break
                            else:
                                print(f"Emotion send failed: {acknowledgement}")
                                await asyncio.sleep(1)  # Wait before retry
                                
                        except Exception as e:
                            print(f"Error sending emotion (attempt {attempt + 1}): {e}")
                            if attempt < max_emotion_retries - 1:
                                await asyncio.sleep(1)  # Wait before retry
                    
                    if not emotion_sent:
                        print("Failed to send emotion after all attempts")
                    
                    # STEP C: Generate and speak response (blocking operations)
                    print("Generating response...")
                    response = await asyncio.to_thread(output_of_model, text, emotions)
                    print(f"💬 Response: {response[:100]}...")
                    
                    print("Speaking response...")
                    await asyncio.to_thread(speak_text, response)
                    
                    # STEP D: Reset ESP32 and resume head tracking
                    print("Resetting ESP32 to default state...")
                    try:
                        reset_ack = await esp32.reset_to_default()
                        if reset_ack.get("status", False):
                            print("ESP32 reset successful")
                        else:
                            print(f"ESP32 reset failed: {reset_ack}")
                    except Exception as e:
                        print(f"Error resetting ESP32: {e}")
                    
                    # STEP E: Wait before resuming head tracking
                    print("Waiting before resuming head tracking...")
                    await asyncio.sleep(2)  # Give ESP32 time to reset
                    
                    print("Resuming head tracking...")
                    resume_head_tracking()
                    
                    print("Emotion processing complete!\n")
                
                else:
                    print("No strong emotion detected, continuing normal operation...")
                
                # Small delay before next iteration
                await asyncio.sleep(0.5)
                
            except KeyboardInterrupt:
                print("\nKeyboard interrupt detected...")
                break
            except Exception as e:
                print(f"Error in main conversation loop: {e}")
                print("Ensuring head tracking is resumed after error...")
                resume_head_tracking()  # Always resume head tracking after any error
                await asyncio.sleep(2)  # Wait a bit before continuing
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
    # Run the async main function directly
    asyncio.run(main())