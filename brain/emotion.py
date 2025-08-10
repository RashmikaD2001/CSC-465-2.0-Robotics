from deepface import DeepFace
import os
import tensorflow as tf

# Configure TensorFlow for GPU usage
os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'
os.environ['TF_FORCE_GPU_ALLOW_GROWTH'] = 'true'

def configure_gpu():
    """Configure GPU settings for optimal performance"""
    print("🔧 Configuring GPU settings...")
    
    # Check if GPU is available
    gpus = tf.config.experimental.list_physical_devices('GPU')
    if gpus:
        try:
            # Enable memory growth to avoid allocating all GPU memory at once
            for gpu in gpus:
                tf.config.experimental.set_memory_growth(gpu, True)
            
            # Set GPU as preferred device
            tf.config.experimental.set_visible_devices(gpus[0], 'GPU')
            
            print(f"✅ GPU configured successfully!")
            print(f"📊 Available GPUs: {len(gpus)}")
            print(f"🎯 Using GPU: {gpus[0].name}")
            
            # Display GPU info
            gpu_details = tf.config.experimental.get_device_details(gpus[0])
            if gpu_details:
                print(f"💾 GPU Memory: {gpu_details.get('device_name', 'Unknown')}")
            
            return True
            
        except RuntimeError as e:
            print(f"⚠️ GPU configuration error: {e}")
            print("🔄 Falling back to CPU...")
            return False
    else:
        print("❌ No GPU detected, using CPU...")
        return False

def get_emotions_by_deepface_gpu(img_path):
    """GPU-optimized emotion detection using DeepFace"""
    try:
        # Use GPU-optimized backend and model
        results = DeepFace.analyze(
            img_path=img_path,
            actions=['emotion'],
            enforce_detection=False,
            detector_backend='mtcnn',  # MTCNN works well with GPU
            silent=True,
            # Add GPU-specific optimizations
            align=True  # Better accuracy, leverages GPU for alignment
        )
        
        if isinstance(results, dict):
            results = [results]
        return results if results else []
        
    except Exception as e:
        print(f"❌ GPU emotion detection failed: {e}")
        print("🔄 Falling back to CPU method...")
        return get_emotions_by_deepface_cpu_fallback(img_path)

def get_emotions_by_deepface_cpu_fallback(img_path):
    """CPU fallback for emotion detection"""
    results = DeepFace.analyze(
        img_path=img_path,
        actions=['emotion'],
        enforce_detection=False,
        detector_backend='opencv',  # Faster CPU backend
        silent=True
    )
    if isinstance(results, dict):
        results = [results]
    return results if results else []

def number_of_people(results):
    """Count number of faces detected"""
    num_faces = len(results)
    return num_faces

def show_results(results):
    """Display emotion analysis results with GPU performance info"""
    for i, face in enumerate(results):
        emo = face['dominant_emotion']
        confidence = face['emotion'][emo]
        print(f"Person {i+1}:")
        print(f"  Dominant Emotion: {emo} ({confidence:.2f}% confidence)")
        print(f"  All Emotions: {face['emotion']}")

def get_emotions_with_positions_gpu(img_path):
    """GPU-optimized emotion detection with face positions"""
    results_dict = {}
    
    try:
        # Use GPU-optimized detection
        deepface_results = DeepFace.analyze(
            img_path=img_path,
            actions=['emotion'],
            enforce_detection=False,
            detector_backend='mtcnn',  # Better GPU utilization
            silent=True,
            align=True
        )
        
        # Normalize to list
        if isinstance(deepface_results, dict):
            deepface_results = [deepface_results]
        
        for idx, face in enumerate(deepface_results):
            # Get DeepFace result
            dominant_emotion = face['dominant_emotion']
            confidence = face['emotion'][dominant_emotion]
            
            # Get coordinates
            region = face.get("region", {})
            x, y, w, h = region.get('x', 0), region.get('y', 0), region.get('w', 0), region.get('h', 0)
            
            # Save result with position
            results_dict[f"Person {idx+1}"] = {
                "emotion": dominant_emotion,
                "confidence": confidence,
                "x": x,
                "y": y,
                "w": w,
                "h": h
            }
            
    except Exception as e:
        print(f"❌ GPU emotion analysis failed: {e}")
        print("🔄 Using CPU fallback...")
        return get_emotions_with_positions_cpu_fallback(img_path)
    
    print('🎯 GPU Emotion Analysis Complete')
    print(results_dict)  
    return results_dict if results_dict else {}

def get_emotions_with_positions_cpu_fallback(img_path):
    """CPU fallback for emotion detection with positions"""
    results_dict = {}
    
    deepface_results = DeepFace.analyze(
        img_path=img_path,
        actions=['emotion'],
        enforce_detection=False,
        detector_backend='opencv',
        silent=True
    )
    
    if isinstance(deepface_results, dict):
        deepface_results = [deepface_results]
    
    for idx, face in enumerate(deepface_results):
        dominant_emotion = face['dominant_emotion']
        confidence = face['emotion'][dominant_emotion]
        
        region = face.get("region", {})
        x, y, w, h = region.get('x', 0), region.get('y', 0), region.get('w', 0), region.get('h', 0)
        
        results_dict[f"Person {idx+1}"] = {
            "emotion": dominant_emotion,
            "confidence": confidence,
            "x": x,
            "y": y,
            "w": w,
            "h": h
        }
    
    print('💻 CPU Emotion Analysis Complete')
    print(results_dict)
    return results_dict if results_dict else {}

def analyze_image_emotions_gpu(img_path):
    """GPU-accelerated main function to analyze emotions in an image"""
    print(f"🎯 GPU-Analyzing emotions in: {img_path}")
    
    # Get emotions using GPU-optimized DeepFace
    results = get_emotions_by_deepface_gpu(img_path)
    
    if not results:
        print("❌ No faces detected in the image.")
        return {}
    
    print(f"\n✅ Detected {number_of_people(results)} person(s) using GPU acceleration")
    print("\n📊 Detailed Results:")
    show_results(results)
    
    # Get emotions with positions using GPU
    emotions_with_positions = get_emotions_with_positions_gpu(img_path)
    
    return emotions_with_positions

def most_dominant_emotion(result_dict):

    emotions = {"angry" : 6, "fear" : 5, "sad" : 4, "disgust" : 3, "surprise" : 2, "happy" : 1, "neutral" : 0}

    result = 'neutral'
    if len(result_dict) > 0:
        for person in result_dict:
            if emotions[result_dict[person]['emotion']] > emotions[result]:
                result = result_dict[person]['emotion']
    return result

def is_negative_emotion(result_dict):
    """Check if the most dominant emotion is negative"""
    result = most_dominant_emotion(result_dict)
    if result in ['angry', 'fear', 'sad', 'disgust']:
        return True
    else:
        return False

'''
Type	Confidence-based decision fusion
Style	Rule-based ensemble
'''