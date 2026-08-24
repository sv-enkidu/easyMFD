# easyMFD
ESP32 and Node Red package to display numerical and text data from any signalk path to various tft displays.  In a later phase I will add graphical guages, but for now it simply diplays path data numerically plus a display title and unit of measurement

## Basic Architecture
Node Red UI allows user to enter signalk path data and some basic dislay and data configuration inputs (font size, font color, brightness) then convert output to a simple json schema which is sent via MQTT OUT node to an ESP32/tft display pair to display. Settings are saved using Node Red native storage.  Note, since tft drivers and sizes very, a class must be added to the display firmware specific to the display (example in my firmware code).  All the rest of the code remains unchanged, so long as the class is instantiated with static LGFX lcd;.  Supports multiple dispays, each with unique TOPIC names, which are the destination address of the MQTT data.  A MATH field is included in the UI (whcih I will pretty up one day) to convert data format from signalk default to desired output format (from Kelvin to Celsius, or radians to degrees, for example).  

Exaample json payload output (saved by Node Red with the Config Store funciton):

{
            "name": "Helm - Outside Temp",
            "topic": "boat/display/helm",
            "path": "environment.outside.temperature",
            "source": "",
            "title": "Temperature",
            "unit": "C",
            "math": "x",
            "decimals": 1,
            "color": "green",
            "size": 4,
            "brightness": 255
        }

Node Red setup (set up two flows):
1. Signalk On Delta node - > Universal Function Node (in repo) -> MQTT out node (leave topic blank, it is inherited from upstream function)
2. easyMFD Dispay manager node -> easyMFD Config Store (in repo) -> looped back to the input of the Display Manager node

Next step is to pretty up the UI and add graphical guages using 


