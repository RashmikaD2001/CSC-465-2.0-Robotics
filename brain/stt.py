import azure.cognitiveservices.speech as speechsdk
import os
from dotenv import load_dotenv

load_dotenv()
api_key = os.getenv('api_key')
region = os.getenv('region')

def speak_to_microphone(text_queue):
    speech_config = speechsdk.SpeechConfig(subscription=api_key, region=region)
    speech_config.speech_recognition_language = 'en-US'
    audio_config = speechsdk.audio.AudioConfig(use_default_microphone=True)

    recognizer = speechsdk.SpeechRecognizer(speech_config=speech_config, audio_config=audio_config)

    # Set timeouts
    recognizer.properties.set_property(
        speechsdk.PropertyId.SpeechServiceConnection_InitialSilenceTimeoutMs, '5000'
    )
    recognizer.properties.set_property(
        speechsdk.PropertyId.SpeechServiceConnection_EndSilenceTimeoutMs, '1500'
    )

    print("Listening to microphone...")
    result = recognizer.recognize_once_async().get()

    if result.reason == speechsdk.ResultReason.RecognizedSpeech:
        text_queue.put(result.text)
        print(result.text)
    else:
        text_queue.put("Speech not recognized or error.")
  

'''

audio_file_path = 'output_audio.wav'

# Configure speech service
speech_config = speechsdk.SpeechConfig(subscription=api_key, region=region)
audio_config = speechsdk.audio.AudioConfig(filename=audio_file_path)

# Create recognizer
speech_recognizer = speechsdk.SpeechRecognizer(speech_config=speech_config, audio_config=audio_config)

# Recognize speech from file
result = speech_recognizer.recognize_once()

if result.reason == speechsdk.ResultReason.RecognizedSpeech:
    print("Recognized text:", result.text)
else:
    print("Speech not recognized or an error occurred.")

'''

