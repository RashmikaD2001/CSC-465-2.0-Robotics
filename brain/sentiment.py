import requests
import json
import os
import numpy as np
from dotenv import load_dotenv
from huggingface_hub import InferenceClient

load_dotenv()

# HuggingFace Endpoint
API_URL = os.getenv('API_URL')

# HuggingFace API TOKEN

HUGGINGFACEHUB_API_TOKEN = os.getenv("HUGGINGFACEHUB_API_TOKEN")

os.environ["HUGGINGFACEHUB_API_TOKEN"] = HUGGINGFACEHUB_API_TOKEN 

headers = {
    "Authorization": f"Bearer {HUGGINGFACEHUB_API_TOKEN}",  
    "Content-Type": "application/json"
}

def setup_data(text):
    return {
        "inputs": text,
        "parameters": {
            "candidate_labels": ["anger", "fear", "neutral", "sad", "disgust", "happy", "surprise"],
            "multi_label": True
        }
    }


def text_sentiment(data):
    print('step 3')
    response = requests.post(API_URL, headers=headers, json=data)

    if response.status_code == 200:
        responseJson = response.json()
        sentiment = responseJson['labels'][np.argmax(responseJson['scores'])]
        print(sentiment)
        return sentiment
    else:
        print("Error:", response.status_code, response.text)
        return 'Error during sentiment: ' + response.text


def sentiment_of_conversation(sentiment):

    if sentiment in ["anger", "fear", "sad", "disgust"]:
        return True
    else:
        return False




