// smartfence.ino

#include <WiFi.h>
#include <HardwareSerial.h>

// ===========================================================================
// HARDWARE PIN DEFINITIONS
// ===========================================================================
#define PIN_PIR      21  // PIR Motion Sensor (detects movement)
#define PIN_IR_BEAM  13  // IR Break Beam Sensor (fence intrusion detection)
#define PIN_BUZZER   14  // Piezo Buzzer (alarm)
#define PIN_RX_CAM   16  // Serial RX from ESP32-CAM
#define PIN_TX_CAM   17  // Serial TX to ESP32-CAM

// Serial communication with ESP32-CAM on UART2
HardwareSerial camSerial(2); 

// ===========================================================================
// STATE VARIABLES (Custom State Machine)
// ===========================================================================
bool systemAwake = false;      // STATE: Camera is actively scanning for humans
bool irArmed = false;          // STATE: Human confirmed, IR beam sensor is "armed"
bool waitingForUpload = false; // STATE: Waiting for ESP32-CAM to finish uploading

unsigned long wakeTimer = 0;    // Track how long camera been scanning
unsigned long irTimer = 0;      // Track how long fence has been armed
unsigned long uploadTimeout = 0;// Failsafe timeout for upload confirmation

String camData = "";            // BUFFER: Store messages from ESP32-CAM

void setup() {
    // Initialize serial communication
    Serial.begin(115200);         // Debug output to computer
    camSerial.begin(115200, SERIAL_8N1, PIN_RX_CAM, PIN_TX_CAM); // Communicate with ESP32-CAM
    
    // Configure hardware pins
    pinMode(PIN_PIR, INPUT);           // PIR sensor input
    pinMode(PIN_IR_BEAM, INPUT_PULLUP);// IR beam (LOW = broken, HIGH = intact)
    pinMode(PIN_BUZZER, OUTPUT);       // Buzzer output

    Serial.println("--- SYSTEM IDLE ---");
    delay(2000);
    
    // Tell ESP32-CAM system is secure on startup
    camSerial.println("###LOG_SECURE###");
}

void loop() {

    // =========================================================================
    // STEP 1: LISTEN TO ESP32-CAM RESPONSES
    // =========================================================================
    // ESP32-CAM sends back messages like ###HUMAN_DETECTED### or ###UPLOAD_SUCCESS###
    // We filter only messages starting with ### to avoid debug text
    if (camSerial.available()) {
        String raw = camSerial.readStringUntil('\n');
        raw.trim();
        if (raw.startsWith("###")) camData = raw;  // Store only command messages
        
        // Wait for upload confirmation before resetting system
        if (camData == "###UPLOAD_SUCCESS###") {
             Serial.println(">> SUCCESS: Dashboard Updated.");
             waitingForUpload = false;  // Release the lock
             camData = "";              // Clear buffer
        }
    }

    // =========================================================================
    // STEP 2: FAILSAFE TIMEOUT MECHANISM
    // =========================================================================
    // If ESP32-CAM fails to upload
    // The system resets after 40 seconds.
    // This prevents the system from being stuck in "waiting" state
    if (waitingForUpload && (millis() - uploadTimeout > 40000)) {
        Serial.println("[!] TIMEOUT. Resetting.");
        waitingForUpload = false;
        irArmed = false;
        systemAwake = false;
        camSerial.println("###LOG_SECURE###"); 
    }

    // =========================================================================
    // STEP 3: PIR MOTION DETECTION (First Line of Defense)
    // =========================================================================
    // Trigger Condition: Motion detected AND system is in IDLE state
    // 
    // When motion detected:
    // 1. Log to Firebase (so dashboard shows "last motion" time)
    // 2. Wake ESP32-CAM to start Human detection (###WAKE_UP###)
    // 3. Enter "systemAwake" state and start timer
    if (digitalRead(PIN_PIR) == HIGH && !systemAwake && !irArmed && !waitingForUpload) {
        Serial.println("[1] MOTION DETECTED");
        camSerial.println("###LOG_PIR###");  // Tell camera to log motion event
        delay(100);
        
        Serial.println("    -> Waking AI...");
        camSerial.println("###WAKE_UP###");   // Wake up camera to start face detection
        
        systemAwake = true;   // Enter AWAKE state
        wakeTimer = millis(); // Start 10-second scanning window
    }

    // =========================================================================
    // STEP 4: HUMAN IDENTIFICATION (AI Confirmation)
    // =========================================================================
    // ESP32-CAM's check_for_human_offline() sends ###HUMAN_DETECTED###
    // This confirms it is human
    // 
    // When human confirmed:
    // 1. Log to Firebase (dashboard shows "last human detected")
    // 2. ARM the IR beam sensor (fence becomes "hot")
    // 3. Exit AWAKE state, enter ARMED state
    // 4. The buzzer beeps once to warn human near the fence
    if (systemAwake) {
        if (camData == "###HUMAN_DETECTED###") {
            Serial.println("[2] HUMAN IDENTIFIED");
            camSerial.println("###LOG_HUMAN###");  // Log human detection event
            
            Serial.println("    -> ARMING FENCE");
            systemAwake = false;  // Exit scanning mode
            irArmed = true;       // Enter armed mode (IR beam active)
            irTimer = millis();   // Start 15-second armed window
            camData = "";         // Clear buffer
            
            // Warning beep to alert human
            digitalWrite(PIN_BUZZER, HIGH); delay(100); digitalWrite(PIN_BUZZER, LOW);
        }
        
        // TIMEOUT: No human found after 10 seconds of scanning
        // False alarm (motion was animal, wind, etc.)
        if (millis() - wakeTimer > 10000) {
            Serial.println("[X] NO HUMAN. Resetting.");
            camSerial.println("###LOG_SECURE###");  // Reset status to secure
            systemAwake = false;
            camData = "";
        }
    }

    // =========================================================================
    // STEP 5: INTRUSION DETECTION (IR BEAM BREAK)
    // =========================================================================
    // IR beam sensor: LOW = broken (someone crossed fence)
    // Only checked when irArmed = true (human was confirmed nearby)
    // This to prevent false alarms from animals or objects
    // 
    // When beam broken:
    // 1. Trigger evidence capture on ESP32-CAM
    // 2. Sound loud alarm (5 beeps)
    // 3. Wait for upload confirmation
    // 4. System locks until upload completes or timeout
    if (irArmed) {
        if (digitalRead(PIN_IR_BEAM) == LOW) {  // Beam broken!
            Serial.println("!!! ALARM: BREACHED !!!");
            
            delay(500); // Brief delay for camera to be ready
            camSerial.println("###CAPTURE_EVIDENCE###");  // Command ESP32-CAM to capture
            
            waitingForUpload = true;   // Enter WAITING state
            uploadTimeout = millis();  // Start 40-second timeout
            
            // Sound alarm (5 rapid beeps)
            for(int i=0; i<5; i++) {
                digitalWrite(PIN_BUZZER, HIGH); delay(100); 
                digitalWrite(PIN_BUZZER, LOW);  delay(50);
            }
            irArmed = false;  // Disarm to prevent multiple captures
        }

        // TIMEOUT: No breach after 15 seconds of being armed
        // Human likely moved away or didn't cross fence
        if (millis() - irTimer > 15000) {
            Serial.println("    -> No Breach. Secure.");
            camSerial.println("###LOG_SECURE###");  // Reset to secure
            irArmed = false;
        }
    }
}