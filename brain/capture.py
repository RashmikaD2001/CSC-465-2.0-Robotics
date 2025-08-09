import threading
from queue import Queue
from stt import speak_to_microphone
from camera_manager import camera_manager  # Import the shared camera manager

def capture_image(output_file):
    """MODIFIED - Capture image using shared camera manager"""
    print("Capturing image from shared camera")
    success = camera_manager.capture_image(output_file)
    if not success:
        print("Failed to capture image from shared camera")
    return success

def capture_both_simultaneously():
    """MODIFIED - Uses shared camera manager"""
    text_queue = Queue()
    # Create threads
    image_thread = threading.Thread(target=capture_image, args=("output_image.jpg",))
    speech_thread = threading.Thread(target=speak_to_microphone, args=(text_queue,))
    # Start threads
    image_thread.start()
    speech_thread.start()
    # Wait for both
    image_thread.join()
    speech_thread.join()
    # Get the spoken text
    spoken_text = text_queue.get()
    print('step 1')
    return spoken_text