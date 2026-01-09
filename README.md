# Smart Perimeter Fence Monitoring
CPC357 IOT ARCHITECTURE AND SMART APPLICATIONS (PROJECT)

## Project Description
This project presents an IoT-based smart perimeter fence monitoring system that design to enhance security in gated environments such as universities, residential compounds, and industrial facilitiesin the smart city environment.

The system combines PIR motion sensing, ESP32-CAM human presence detection, and IR break-beam intrusion confirmation to provide accurate, real-time detection of unauthorized boundary breaches. Verified intrusion events are logged to a cloud platform and visualized through a web-based dashboard, enabling faster response and reduced false alarms.

## Project Objectives
- To detect unauthorized activities around fenced areas.
- To verify human presence before activating intrusion detection
- To utilize motion sensing and camera-based detection to identify potential human intrusion events.
- To trigger immediate alerts when a fence breach is detected to improve security response time.
- To enable real-time monitoring and event logging through a centralized cloud platform.

## System Architecture
The system follows an event-driven, sensor-fusion architecture:
1. PIR motion sensor detects movement near the fence
2. ESP32-CAM verifies human presence using built-in face detection
3. IR break-beam confirms physical boundary crossing
4. Intrusion events are uploaded to the cloud in real time
5. Alerts and evidence are displayed on a web dashboard

---------------------

## Hardware Components
- NodeMCU ESP32 Dev Board
- ESP32-CAM (AI Thinker)
- PIR Motion Sensor
- Active IR Break-Beam Sensor
- Active Buzzer
- Wi-Fi Network
- 5V Power Supply (Powerbank)
- Resistor
- Transistor

## Software Components
- Arduino IDE
- ESP32 Board Support Package
- Firebase Realtime Database
- Firebase Cloud Storage
- HTML, CSS, JavaScript (Web Dashboard)

## Cloud Platform (Google Cloud Platform)
- Firebase Realtime Database (alert and event logs & live system status)
- Firebase Cloud Storage (intrusion image evidence)
- Firebase Hosting (dashboard deployment)

## Dashboard Features
- Real-time system status (SECURE / BREACHED)
- Alert logs (Motion detected, Human detected, Intrusion) and its counter
- Visual evidence of intrusion display

---------------------
## Setup and Installation Instructions
### 1. Hardware Setup
- Connect PIR sensor, IR break-beam, and buzzer to NodeMCU ESP32
- Connect ESP32-CAM to NodeMCU via UART
- Ensure proper power supply for ESP32-CAM and NodeMCU ESP32

### 2. Firmware Setup
1. Install Arduino IDE
2. Install ESP32 Board Package
3. Install required libraries:
   - Firebase ESP Client
4. Configure Wi-Fi and Firebase credentials in the '.ino' files
5. Upload firmware to NodeMCU and ESP32-CAM

### 3. Cloud Setup
- Create a Firebase project
- Enable Realtime Database and Cloud Storage
- Set database rules and obtain API keys
- Update credentials in firmware code

---------------------
## Dependencies and Requirements
### Software Libraries
- ESP32 Board Package 2.0.0v
- Firebase_ESP_Client

---------------------
## Repository Structure

---------------------

## SDG 11 Contribution
This project supports SDG 11 Smart City by improving urban safety, reducing reliance on manual security monitoring, and promoting efficient use of energy and human resources through automation and edge intelligence.

This project is developed for academic purposes for CPC357 IoT and Smart Application.
