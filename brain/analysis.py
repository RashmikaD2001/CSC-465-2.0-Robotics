import threading
from emotion import get_emotions_with_positions
from sentiment import text_sentiment, setup_data

# Containers to store results
emotion_result = {}
sentiment_result = {}

def analyze_emotions(image_path):
    global emotion_result
    emotion_result = get_emotions_with_positions(image_path)

def analyze_sentiment(text):
    global sentiment_result
    sentiment_data = setup_data(text)
    sentiment_result['value'] = text_sentiment(sentiment_data)

def run_parallel_analysis(image_path, text):
    """Run emotion and sentiment analysis in parallel threads."""
    global emotion_result, sentiment_result

    # Reset results
    emotion_result = {}
    sentiment_result = {}

    # Define threads
    emotion_thread = threading.Thread(target=analyze_emotions, args=(image_path,))
    sentiment_thread = threading.Thread(target=analyze_sentiment, args=(text,))

    # Start both
    emotion_thread.start()
    sentiment_thread.start()

    # Wait for both to finish
    emotion_thread.join()
    sentiment_thread.join()

    return emotion_result, sentiment_result.get('value')
