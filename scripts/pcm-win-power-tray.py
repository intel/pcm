import pystray
from PIL import Image, ImageDraw, ImageFont
import threading
import time
import subprocess  # nosec B404 - subprocess used for controlled system commands, no user input
import csv
import io

def create_image(text, background_color):
    width = 12 
    height = 12
    image = Image.new('RGB', (width, height), background_color)
    draw = ImageDraw.Draw(image)

    # Use the default font and scale it
    font = ImageFont.load_default()

    # Calculate text size and position
    text_bbox = draw.textbbox((0, 0), text, font=font)
    text_width = text_bbox[2] - text_bbox[0]
    text_height = text_bbox[3] - text_bbox[1]
    text_x = (width - text_width) // 2
    text_y = -2 + (height - text_height) // 2

    # Draw the text on the image
    draw.text((text_x, text_y), text, fill="white", font=font)

    # Scale the image down to fit the system tray icon size
    image = image.resize((64, 64), Image.LANCZOS)

    return image

# global process variable to kill the pcm.exe process when the icon is clicked
process = None

def update_icon(icon):
    # Start the pcm.exe process with -csv 3 parameters
    # store process into the global process variable
    global process
    process = subprocess.Popen(
        # change the path to pcm.exe as needed
        ["..\\windows_build22\\bin\\Release\\pcm.exe", "-r", "-csv", "3"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    count = 0

    while True:
        # find the header line to find the index of "SYSTEM Energy (Joules)"
        if (count > 1000):
            print (f"Can't find SYSTEM Energy (Joules) metric")
            process.kill()
            exit(0)
        count = count + 1
        line = process.stdout.readline()
        csv_reader = csv.reader(io.StringIO(line))
        header = next(csv_reader)
        try:
            system_energy_index = header.index("SYSTEM Energy (Joules)")
            print (f"SYSTEM Energy (Joules) found at index {system_energy_index}")
            break
        except ValueError:
            #print("SYSTEM Energy (Joules) not found in the header")
            continue

    # Skip the second header line
    process.stdout.readline()

    while True:
        # Read the output line by line
        line = process.stdout.readline()
        # print (line)
        if not line:
            break

        system_energy_joules = -1

        # Parse the CSV output
        csv_reader = csv.reader(io.StringIO(line))
        for row in csv_reader:
            # Extract the system power consumption in Watts
            try:
                system_energy_joules = float(row[system_energy_index])
                print (f"SYSTEM Energy (Joules): {system_energy_joules}")
                # Convert Joules to Watts
                power_consumption_watts = system_energy_joules / 3.0
                if (power_consumption_watts > 30) :
                    background_color = "red"
                elif (power_consumption_watts > 20) :
                    background_color = "darkblue"
                else:
                    background_color = "darkgreen"
                # Update the icon with the current power consumption in Watts
                icon.icon = create_image(f"{power_consumption_watts:.0f}", background_color)
            except (IndexError, ValueError):
                continue

        if (system_energy_joules == -1):
            continue

    print (f"pcm.exe exited with code {process.returncode}")

def on_quit(icon, item):
    icon.stop()
    process.kill()

def main():
    # Create the system tray icon
    icon = pystray.Icon("Intel PCM: System Watts")
    icon.icon = create_image("P", "darkblue")
    icon.title = "Intel PCM: System Watts"
    icon.menu = pystray.Menu(
        pystray.MenuItem("Quit", on_quit)
    )

    # Start a thread to update the icon
    threading.Thread(target=update_icon, args=(icon,), daemon=True).start()

    # Run the icon
    icon.run()

if __name__ == "__main__":
    main()