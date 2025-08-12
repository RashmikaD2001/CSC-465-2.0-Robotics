import os
import base64
from io import BytesIO
import requests
from langchain_huggingface import HuggingFaceEndpoint
from langchain.chains import LLMChain
from langchain_core.prompts import PromptTemplate
from langgraph.checkpoint.memory import MemorySaver
from langgraph.graph import START, MessagesState, StateGraph
from langchain_core.messages import SystemMessage, HumanMessage, AIMessage
from typing import TypedDict
from PIL import Image
from dotenv import load_dotenv
import numpy as np

load_dotenv()

# HuggingFace API TOKEN
HUGGINGFACEHUB_API_TOKEN = os.getenv("HUGGINGFACEHUB_API_TOKEN")
os.environ["HUGGINGFACEHUB_API_TOKEN"] = HUGGINGFACEHUB_API_TOKEN

# Fixed endpoint URL - using the working one we discovered
endpoint_url = os.getenv('endpoint_url')


def convert_numpy_types(obj):
    """Recursively convert numpy types to native Python types"""
    if isinstance(obj, dict):
        return {key: convert_numpy_types(value) for key, value in obj.items()}
    elif isinstance(obj, list):
        return [convert_numpy_types(item) for item in obj]
    elif isinstance(obj, np.integer):
        return int(obj)
    elif isinstance(obj, np.floating):
        return float(obj)
    elif isinstance(obj, np.ndarray):
        return obj.tolist()
    else:
        return obj

class HarmonyState(TypedDict):
    messages: list
    image_path: str  # Store path instead of PIL Image to avoid serialization issues
    people: dict

sys_msg = """
You are HarmonyBot, an emotion-aware social robot designed for conflict mediation. You are a wise, empathetic and friendly robot.
Your role is to detect the emotional tone of conversations and respond with gentle prompts, empathetic language, 
humor, or subtle tone shifts to de-escalate tension and encourage calm, respectful dialogue. 
Seek to understand the root cause of the conflict and offer creative, practical and peaceful solutions. 
If the participants may not know who you are, introduce yourself naturally based on the flow of conversation.
"""

template = """
HarmonyBot emotion-aware social robot for conflict mediation, This is the current situation of conflict.
Inputs:
- Conversation: {conversation}
- People Emotion Labels with Face Coordinates: {people}
You also receive a user image (provided separately). Detect the emotions and reasons in conversation and image and
respond in a helpful and emotionally aware manner with gentle prompts, tone shifts, humor, or empathy to de-escalate tension and promote calm communication between people. Provide short and humanize output.
Don't use Person1, Person2 in output. People will feel awkward. Use a special characteristic you identified in each person.
"""

prompt = PromptTemplate.from_template(template)

def image_to_base64(image: Image.Image) -> str:
    """Convert PIL Image to base64 string"""
    buffered = BytesIO()
    image.save(buffered, format="JPEG")
    img_str = base64.b64encode(buffered.getvalue()).decode()
    return img_str

class VisionHuggingFaceEndpoint:
    """Custom vision model endpoint that mimics HuggingFaceEndpoint interface"""
    
    def __init__(self, endpoint_url, **kwargs):
        self.endpoint_url = endpoint_url
        self.max_new_tokens = kwargs.get('max_new_tokens', 600)
        self.temperature = kwargs.get('temperature', 0.7)
        self.top_k = kwargs.get('top_k', 25)
        self.top_p = kwargs.get('top_p', 0.95)
        self.repetition_penalty = kwargs.get('repetition_penalty', 1.03)
    
    def invoke(self, prompt_text, image_path=None):
        """Invoke the vision model with text and image"""
        try:
            # Load and convert image to base64
            image = Image.open(image_path).convert("RGB")
            image_b64 = image_to_base64(image)
        except Exception as e:
            return f"Error loading image: {str(e)}"
        
        # OpenAI-compatible payload with vision
        payload = {
            "messages": [
                {
                    "role": "user",
                    "content": [
                        {"type": "text", "text": prompt_text},
                        {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{image_b64}"}}
                    ]
                }
            ],
            "max_tokens": self.max_new_tokens,
            "temperature": self.temperature
        }
        
        headers = {
            "Authorization": f"Bearer {HUGGINGFACEHUB_API_TOKEN}",
            "Content-Type": "application/json"
        }
        
        try:
            response = requests.post(self.endpoint_url, 
                                   headers=headers, 
                                   json=payload,
                                   timeout=15)
            
            response.raise_for_status()
            result = response.json()
            
            # Extract response from OpenAI format
            if "choices" in result and len(result["choices"]) > 0:
                return result["choices"][0]["message"]["content"]
            else:
                return f"Unexpected response format: {result}"
                
        except requests.exceptions.RequestException as e:
            return f"API request failed: {str(e)}"
        except Exception as e:
            return f"Error processing response: {str(e)}"

# Use the custom vision model instead of regular HuggingFaceEndpoint
model = VisionHuggingFaceEndpoint(
    endpoint_url=endpoint_url,
    max_new_tokens=512,
    top_k=15,
    top_p=0.95,
    typical_p=0.95,
    temperature=0.1,
    repetition_penalty=1.03,
)

def call_model(state: HarmonyState) -> dict:
    image_path = state["image_path"]  # Get image path instead of PIL Image
    conversation = "\n".join([msg.content for msg in state["messages"] if isinstance(msg, HumanMessage)])
    prompt_text = prompt.format(
        conversation=conversation,
        people=str(state["people"]),
    )
    
    # Use custom vision model that accepts image_path
    response = model.invoke(prompt_text, image_path=image_path)
    
    return {
        "messages": state["messages"] + [AIMessage(content=response)],
        "image_path": image_path,  # Keep as path
        "people": state["people"],
    }

workflow = StateGraph(state_schema=HarmonyState)
workflow.add_node("model", call_model)
workflow.set_entry_point("model")
workflow.set_finish_point("model")

memory = MemorySaver()
app = workflow.compile(checkpointer=memory)

def output_of_model(conversation, people):
    # Convert numpy types to native Python types before using in state
    people_converted = convert_numpy_types(people)
    
    # Store image path instead of loading PIL Image into state
    image_path = "output_image.jpg"
    
    # Check if image exists
    if not os.path.exists(image_path):
        error_msg = f"Error: {image_path} not found. Please ensure the image file exists."
        print(error_msg)
        return error_msg
    
    initial_state = {
        "messages": [
            SystemMessage(content=sys_msg),
            HumanMessage(content=conversation)
        ],
        "image_path": image_path,  # Store path instead of PIL Image
        "people": people_converted  # Use converted data
    }
    
    try:
        config = {"configurable": {"thread_id": "session-1"}}
        result = app.invoke(initial_state, config=config)
        
        print(result["messages"][-1].content)
        return result["messages"][-1].content
    except Exception as e:
        print(f"Oops, something went wrong: {str(e)}")
        return f"Oops, something went wrong: {str(e)}"



