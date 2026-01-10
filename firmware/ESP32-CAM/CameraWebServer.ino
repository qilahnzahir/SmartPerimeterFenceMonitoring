// CameraWebServer.ino
// Added Firebase integration and custom serial command protocol for Smart Perimeter Fence Monitoring System.
#include "esp_camera.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "time.h" 

// ===========================================================================
//  1. CONFIGURATION (FILL THESE CAREFULLY)
// ===========================================================================
// Replace with actual Firebase API Key 
#define API_KEY "AIzaSyD-EXAMPLEKEY-FILLINYOURSELF"

// Replace with Firebase authentication email
#define USER_EMAIL "USEREMAIL"

// Replace with Firebase authentication password
#define USER_PASSWORD "USERPASSWORD"

// Replace with Firebase Storage Bucket ID
#define STORAGE_BUCKET_ID "your-project-id.firebasestorage.app" 

// Replace with WiFi credentials
// WIFI
const char* ssid = "SSID_NAME";
const char* password = "PASSWORD";

// TIME (GMT+8)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800;
const int   daylightOffset_sec = 0;

// ===========================================================================
//  2. HARDWARE
// ===========================================================================
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
#define FLASH_LED_PIN 4

// ===========================================================================
//  3. OBJECTS AND VARIABLES
// ===========================================================================
// - fbdo: For Realtime Database operations (logging, status updates)
// - fbdo_media: For Storage operations (uploading images)
// Using separate objects prevents "already in use" errors during simultaneous operations
FirebaseData fbdo;       // Object 1: For Database (Text logs)
FirebaseData fbdo_media; // Object 2: For Storage (Images only)
FirebaseAuth auth;
FirebaseConfig config;
bool isFirebaseReady = false;

// Added functions on top of standard CameraWebServer.ino
void startCameraServer();                           // Starts HTTP server for camera stream
void check_for_human_offline();                     // AI face detection without web stream
void uploadEvidence();                              // Captures & uploads breach evidence to Firebase
void logToAlerts(String eventType, String status, bool hasImage, String imgPath); // Logs events to Firebase
void updateDashboardLive(String node, String value); // Updates live dashboard values
void getDateTime(String &dateStr, String &timeStr);  // Gets formatted date/time from NTP

// Camera will implement Wake/Sleep System for power efficiency instead of constantly running
// 1. Stay in "sleep" mode (low-res camera)
// 2. Only wake up when NodeMCU sends ###WAKE_UP### command
// 3. Run AI detection for 10 seconds, then sleep again
bool isAwake = false;
unsigned long wakeTimer = 0;

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
    Serial.begin(115200);
    
    pinMode(FLASH_LED_PIN, OUTPUT);
    digitalWrite(FLASH_LED_PIN, LOW);

    camera_config_t cam_config;
    cam_config.ledc_channel = LEDC_CHANNEL_0;
    cam_config.ledc_timer = LEDC_TIMER_0;
    cam_config.pin_d0 = Y2_GPIO_NUM;
    cam_config.pin_d1 = Y3_GPIO_NUM;
    cam_config.pin_d2 = Y4_GPIO_NUM;
    cam_config.pin_d3 = Y5_GPIO_NUM;
    cam_config.pin_d4 = Y6_GPIO_NUM;
    cam_config.pin_d5 = Y7_GPIO_NUM;
    cam_config.pin_d6 = Y8_GPIO_NUM;
    cam_config.pin_d7 = Y9_GPIO_NUM;
    cam_config.pin_xclk = XCLK_GPIO_NUM;
    cam_config.pin_pclk = PCLK_GPIO_NUM;
    cam_config.pin_vsync = VSYNC_GPIO_NUM;
    cam_config.pin_href = HREF_GPIO_NUM;
    cam_config.pin_sscb_sda = SIOD_GPIO_NUM;
    cam_config.pin_sscb_scl = SIOC_GPIO_NUM;
    cam_config.pin_pwdn = PWDN_GPIO_NUM;
    cam_config.pin_reset = RESET_GPIO_NUM;
    cam_config.xclk_freq_hz = 20000000;
    cam_config.pixel_format = PIXFORMAT_JPEG;
    
    if(psramFound()){
        cam_config.frame_size = FRAMESIZE_UXGA; 
        cam_config.jpeg_quality = 10;
        cam_config.fb_count = 2;
    } else {
        cam_config.frame_size = FRAMESIZE_SVGA;
        cam_config.jpeg_quality = 12;
        cam_config.fb_count = 1;
    }

    esp_camera_init(&cam_config);
    
    sensor_t * s = esp_camera_sensor_get();
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1); 
        s->set_brightness(s, 1); 
        s->set_saturation(s, -2); 
    }
    s->set_framesize(s, FRAMESIZE_QVGA); 

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
    }
    Serial.println("\nWiFi Connected");

    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    config.api_key = API_KEY;
    // Replace with Firebase Realtime Database URL
    config.database_url = "https://your-project-id-default-rtdb.firebaseio.com/"; 
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    
    // Set buffer size for image upload
    config.timeout.serverResponse = 20000; 

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    isFirebaseReady = true;
    
    startCameraServer();
    Serial.println("System Ready!");
}

void loop() {

    // SERIAL COMMAND PROTOCOL
    // ESP32-CAM receives commands from NodeMCU via Serial (TX/RX pins)
    // This to allow NodeMCU to control the camera and trigger actions
    
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();

        // NodeMCU detected movement, log it to Firebase
        if (command == "###LOG_PIR###") {
            Serial.println(">> LOGGING: Motion");
            updateDashboardLive("last_motion", "NOW"); 
            logToAlerts("PIR: Motion Detected", "WARNING", false, "");
        }
        
        // Human Detected by AI
        // This is sent from check_for_human_offline() function
        else if (command == "###LOG_HUMAN###") {
            Serial.println(">> LOGGING: Human");
            updateDashboardLive("last_human", "NOW");
            logToAlerts("Camera: Human Detected", "DANGER", false, "");
        }
        
        // System Secure (Reset Status)
        else if (command == "###LOG_SECURE###") {
            Serial.println(">> STATUS: SECURE");
            if (Firebase.ready()) Firebase.RTDB.setString(&fbdo, "/dashboard_live/system_status", "SECURE");
        }
        
        // NodeMCU tells camera to start actively scanning for humans
        else if (command == "###WAKE_UP###") {
            isAwake = true;
            wakeTimer = millis();
            sensor_t * s = esp_camera_sensor_get();
            s->set_framesize(s, FRAMESIZE_QVGA); // Keep at low resolution for speed
        }
        
        // COMMAND 5: Capture Evidence (BREACH!)
        // NodeMCU detected IR beam break, capture and upload photo NOW
        else if (command == "###CAPTURE_EVIDENCE###") {
            Serial.println(">> ACTION: BREACH! UPLOADING...");
            digitalWrite(FLASH_LED_PIN, HIGH);  // Turn on flash for better image
            uploadEvidence();                    // Take photo & upload to Firebase
            digitalWrite(FLASH_LED_PIN, LOW);
            isAwake = false;                     // Return to sleep mode
        }
    }

    // =====================================================================
    // ACTIVE SCANNING MODE (Wake Cycle)
    // =====================================================================
    // When isAwake = true, continuously scan for human faces for 10 seconds after motion detected
    // If human detected, check_for_human_offline() sends ###HUMAN_DETECTED###
    // back to NodeMCU via Serial, then NodeMCU arms the fence
    
    if (isAwake) {
        check_for_human_offline();  // Run AI face detection (defined in app_httpd.cpp)
        
        // Auto-sleep after 10 seconds if no human found
        if (millis() - wakeTimer > 10000) {
            isAwake = false;
            digitalWrite(FLASH_LED_PIN, LOW);
        }
        delay(50);  // Small delay to prevent overwhelming CPU
    }
}

// --- HELPER: TIME ---
void getDateTime(String &dateStr, String &timeStr) {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
        dateStr = "N/A"; timeStr = "N/A"; return;
    }
    char dBuffer[12];
    strftime(dBuffer, 12, "%d-%m-%Y", &timeinfo);
    dateStr = String(dBuffer);
    char tBuffer[10];
    strftime(tBuffer, 10, "%H:%M:%S", &timeinfo);
    timeStr = String(tBuffer);
}

// --- HELPER: LIVE DASHBOARD ---
void updateDashboardLive(String nodeName, String val) {
    if (!Firebase.ready()) return;
    String d, t; getDateTime(d, t);
    String fullTime = t + "   " + d;
    Firebase.RTDB.setString(&fbdo, "/dashboard_live/" + nodeName, fullTime);
}

// --- HELPER: HISTORY ALERTS ---
void logToAlerts(String eventType, String status, bool hasImage, String imgPath) {
    if (!Firebase.ready()) return;
    String d, t; getDateTime(d, t);
    
    FirebaseJson json;
    json.set("event_type", eventType);
    json.set("system_status", status);
    json.set("location", "Fence 01");
    json.set("date", d);
    json.set("time", t);
    json.set("timestamp", millis());
    
    if (hasImage) json.set("image_path", imgPath);
    else json.set("image_path", "NONE");

    Firebase.RTDB.pushJSON(&fbdo, "/alerts", &json);
}

// ===========================================================================
// UPLOAD EVIDENCE TO FIREBASE STORAGE
// ===========================================================================

void uploadEvidence() {
    if (!Firebase.ready()) return;

    // update status to BREACHED
    Firebase.RTDB.setString(&fbdo, "/dashboard_live/system_status", "BREACHED");

    // Switch to higher resolution for evidence quality
    sensor_t * s = esp_camera_sensor_get();
    // USE SVGA for stability (UXGA often crashes memory)
    s->set_framesize(s, FRAMESIZE_SVGA); 
    delay(200);  // Wait for camera to adjust 

    // Capture the frame buffer (actual photo)
    camera_fb_t * fb = esp_camera_fb_get();
    if(!fb) { Serial.println("Capture Failed"); return; }

    // Generate timestamped filename for evidence tracking
    String d, t; getDateTime(d, t);
    String filename = "/evidence/" + d + "_" + t + ".jpg";
    filename.replace(":", "-"); // Remove colons (invalid in filenames)

    Serial.print("Uploading Evidence...");
    
    // Upload image to Firebase Storage using seperate fbdo_media object
    if (Firebase.Storage.upload(&fbdo_media, STORAGE_BUCKET_ID, fb->buf, fb->len, filename, "image/jpeg")) {
        Serial.println("Success!");
        
        // Update Dashboard "Evidence" Section (Live View)
        FirebaseJson evidenceJson;
        evidenceJson.set("date", d);
        evidenceJson.set("time", t);
        evidenceJson.set("location", "Fence 01");
        evidenceJson.set("image_path", filename);
        Firebase.RTDB.updateNode(&fbdo, "/dashboard_live/evidence", &evidenceJson);

        // Log to Alerts History
        logToAlerts("IR break beam: Breach Break", "BREACHED", true, filename);

        // Notify NodeMCU (Serial Communication)
        Serial.println("###UPLOAD_SUCCESS###"); 
    } 
    else {
        Serial.print("Upload Failed: ");
        Serial.println(fbdo_media.errorReason());
    }

    //Cleanup and return to low-power mode
    esp_camera_fb_return(fb);             // Free memory from frame buffer
    s->set_framesize(s, FRAMESIZE_QVGA); // Return to low resolution (saves CPU/memory)
}