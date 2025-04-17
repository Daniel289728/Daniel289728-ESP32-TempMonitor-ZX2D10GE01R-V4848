# ESP32-TempMonitor-ZX2D10GE01R-V4848
This project is a smart thermostat system built on an ESP32 microcontroller with a color touchscreen display using ZX2D10GE01R-V4848. The system connects to a local WiFi network to fetch temperature data and control a heating boiler.
# Features

Graphical User Interface: Clean UI with temperature displays and visual indicators
Rotary Encoder Control: Adjust temperature setpoint using a magnetic encoder (MT8901)
WiFi Connectivity: Connects to your local network for remote control and monitoring
REST API Integration: Communicates with a server to control heating system
Power Management: Screen timeout to conserve energy when idle

# Development Environment
This project uses PlatformIO with the following configuration:

Platform: espressif32
Board: esp32-s3-devkitc-1
Framework: Arduino
Flash size: 32MB
Partition scheme: huge_app.csv
Memory type: qio_opi (Quad I/O with octal PSRAM)
# Software Dependencies
The project depends on the following libraries (automatically managed by PlatformIO):

LVGL (v8.0.0) - Graphics library
GFX Library for Arduino - Display driver
FastLED - LED control
ArduinoJson - JSON parsing
JPEGDEC - JPEG decoding

# Configuration
Edit the following variables in the code to match your setup:
cppconst char* ssid = "IZZI-D31416";         // Your WiFi SSID
const char* password = "JxF9btVjyZeHLJtN"; // Your WiFi password
const char* serverIP = "192.168.4.1";      // IP address of the control server

# Building and Uploading
Clone this repository
Open the project in PlatformIO
Configure your WiFi credentials and server IP in the code
Build and upload to your ESP32-S3 board
Monitor serial output at 115200 baud

# API Endpoints
The thermostat communicates with a server using these endpoints:

http://{serverIP}/setTemp - POST endpoint to set desired temperature
http://{serverIP}/Temp?plain - GET endpoint to fetch current temperature
http://{serverIP}/boilerStatus?plain - GET endpoint to check if boiler is active

# Debug Level
The project is configured with debug level 2 (WARN). You can adjust the debug level by modifying the CORE_DEBUG_LEVEL build flag in platformio.ini:
-DCORE_DEBUG_LEVEL=2 ; 5=VERBOSE, 4=DEBUG, 3=INFO, 2=WARN, 1=ERROR

# Notes
The project uses a huge app partition scheme to accommodate the LVGL library and graphics resources
PSRAM is enabled for display buffer allocation