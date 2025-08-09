import cv2
import time
import threading

class SharedCameraManager:
    """Manages a single camera instance shared between multiple processes"""
    
    def __init__(self, camera_index=0):
        self.camera_index = camera_index
        self.cap = None
        self.running = False
        self.frame_lock = threading.Lock()
        self.current_frame = None
        self.frame_timestamp = 0
        self.capture_thread = None
        
    def start(self):
        """Start the camera capture thread"""
        if self.running:
            return
            
        self.cap = cv2.VideoCapture(self.camera_index)
        if not self.cap.isOpened():
            raise RuntimeError("Could not open camera")
            
        # Set camera properties for better performance
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
        self.cap.set(cv2.CAP_PROP_FPS, 15)
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        
        self.running = True
        self.capture_thread = threading.Thread(target=self._capture_loop, daemon=True)
        self.capture_thread.start()
        
        # Wait for first frame
        time.sleep(0.5)
        
    def stop(self):
        """Stop the camera capture"""
        self.running = False
        if self.capture_thread:
            self.capture_thread.join()
        if self.cap:
            self.cap.release()
            
    def _capture_loop(self):
        """Continuous capture loop running in background thread"""
        while self.running:
            ret, frame = self.cap.read()
            if ret:
                with self.frame_lock:
                    self.current_frame = frame.copy()
                    self.frame_timestamp = time.time()
            time.sleep(1/30)  # 30 FPS max
            
    def get_frame(self):
        """Get the latest frame (thread-safe)"""
        with self.frame_lock:
            if self.current_frame is not None:
                return self.current_frame.copy()
            return None
            
    def capture_image(self, output_file):
        """Capture and save current frame to file"""
        frame = self.get_frame()
        if frame is not None:
            cv2.imwrite(output_file, frame)
            return True
        return False

# Global camera manager instance
camera_manager = SharedCameraManager()