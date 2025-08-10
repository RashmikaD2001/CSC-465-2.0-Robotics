from deepface import DeepFace
import os

os.environ['TF_CPP_MIN_LOG_LEVEL'] = '3'

#detector_backend='retinaface' - previously

def get_emotions_by_deepface(img_path):
    """Get emotions using DeepFace library"""
    results = DeepFace.analyze(
        img_path=img_path,
        actions=['emotion'],
        enforce_detection=False,
        detector_backend='ssd',
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
    """Display emotion analysis results"""
    for i, face in enumerate(results):
        emo = face['dominant_emotion']
        print(f"Person {i+1}:")
        print(f"  Dominant Emotion: {emo}")
        print(f"  All Emotions: {face['emotion']}")

def get_emotions_with_positions(img_path):
    """Get emotions with face positions using only DeepFace"""
    results_dict = {}
   
    # Analyze using DeepFace
    deepface_results = DeepFace.analyze(
        img_path=img_path,
        actions=['emotion'],
        enforce_detection=False,
        detector_backend='retinaface'
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
    print('step 2')
    print(results_dict)  
    return results_dict if results_dict else {}

def analyze_image_emotions(img_path):
    """Main function to analyze emotions in an image"""
    print(f"Analyzing emotions in: {img_path}")
   
    # Get emotions using DeepFace
    results = get_emotions_by_deepface(img_path)
   
    if not results:
        print("No faces detected in the image.")
        return {}
   
    print(f"\nDetected {number_of_people(results)} person(s)")
    print("\nDetailed Results:")
    show_results(results)
   
    # Get emotions with positions
    emotions_with_positions = get_emotions_with_positions(img_path)
   
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

    result = most_dominant_emotion(result_dict)

    if result in ['angry', 'fear', 'sad', 'disgust']:
        return True
    else:
        return False

'''
Type	Confidence-based decision fusion
Style	Rule-based ensemble
'''