import os
import azure.cognitiveservices.speech as speechsdk
from dotenv import load_dotenv

load_dotenv()

def speak_text(text):
    api_key = os.getenv('api_key')
    region = os.getenv('region')

    # Set up config
    speech_config = speechsdk.SpeechConfig(subscription=api_key, region=region)
    speech_config.speech_synthesis_voice_name = "en-US-SaraNeural"

    # Output to default speaker
    audio_config = speechsdk.audio.AudioOutputConfig(use_default_speaker=True)

    # Create synthesizer
    synthesizer = speechsdk.SpeechSynthesizer(speech_config, audio_config)

    # Strip and speak text
    text = text.strip()
    result = synthesizer.speak_text_async(text).get()

    # Optional: check if it succeeded
    if result.reason != speechsdk.ResultReason.SynthesizingAudioCompleted:
        print("TTS failed:", result.reason)

