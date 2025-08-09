import cv2
import os

def apply_median_filter(directory="."):
    input_path = os.path.join(directory, "output_image.jpg")
    output_path = os.path.join(directory, "final_image.jpg")
    
    # Read the image
    image = cv2.imread(input_path)
    if image is None:
        raise FileNotFoundError(f"No image found at {input_path}")

    # Apply median blur with kernel size 5
    filtered_image = cv2.medianBlur(image, 5)

    # Save the final image
    cv2.imwrite(output_path, filtered_image)
