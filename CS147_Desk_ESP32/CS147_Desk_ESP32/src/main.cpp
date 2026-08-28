#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>

// =====================================================
// WIFI
// =====================================================

#define WIFI_SSID     "Your Own"
#define WIFI_PASSWORD "Your Own"

// =====================================================
// AZURE IOT HUB
// =====================================================

#define IOT_HUB_HOST "Your Own"
#define DEVICE_ID    "desk-esp32"

#define SAS_TOKEN "Your Own"

const int MQTT_PORT = 8883;

const char* MQTT_USERNAME =
    IOT_HUB_HOST "/" DEVICE_ID "/?api-version=2021-04-12";

const char* C2D_TOPIC =
    "devices/" DEVICE_ID "/messages/devicebound/#";

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);

// =====================================================
// OLED
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

#define OLED_SDA 21
#define OLED_SCL 22

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    OLED_RESET
);

// =====================================================
// RED LED
// =====================================================

#define LED_PIN 23

const unsigned long LED_BLINK_INTERVAL_MS = 500;

bool ledState = false;
unsigned long lastLedBlinkTime = 0;

// =====================================================
// CURRENT STATE FROM CHAIR
// =====================================================

String currentPosture = "UNKNOWN";

unsigned long localSittingSeconds = 0;
unsigned long currentBadPostureSeconds = 0;

bool chairOccupied = false;

// Separate alert states
bool postureAlertActive = false;
bool sittingAlertActive = false;

// Overall alert
bool alertActive = false;

String currentAlertType = "NONE";

bool receivedChairData = false;

// Local OLED sitting timer
unsigned long lastLocalTimerUpdate = 0;

// =====================================================
// SHORT POSTURE NAME
// =====================================================

String shortPostureName(String posture) {

    if (
        posture ==
        "NO LUMBAR SUPPORT / LEANING FORWARD"
    ) {
        return "LEANING FORWARD";
    }

    if (posture == "LEANING LEFT") {
        return "LEANING LEFT";
    }

    if (posture == "LEANING RIGHT") {
        return "LEANING RIGHT";
    }

    if (
        posture ==
        "UNCERTAIN / LOW SIDE CONTACT"
    ) {
        return "LOW SIDE CONTACT";
    }

    if (posture == "GOOD / CENTERED") {
        return "GOOD / CENTERED";
    }

    if (posture == "CHAIR EMPTY") {
        return "CHAIR EMPTY";
    }

    return posture;
}

// =====================================================
// WAITING SCREEN
// =====================================================

void showWaitingScreen() {

    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.println("CS147 Desk Monitor");

    display.setCursor(0, 20);
    display.println("Connected to Azure");

    display.setCursor(0, 40);
    display.println("Waiting for chair...");

    display.display();
}

// =====================================================
// UPDATE OLED
// =====================================================

void updateOLED() {

    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // =================================================
    // WAITING FOR FIRST MESSAGE
    // =================================================

    if (!receivedChairData) {

        showWaitingScreen();
        return;
    }

    // =================================================
    // CHAIR EMPTY
    // =================================================

    if (!chairOccupied) {

        display.setCursor(0, 0);
        display.println("POSTURE MONITOR");

        display.setCursor(0, 20);
        display.println("CHAIR EMPTY");

        display.setCursor(0, 40);
        display.println("Sitting: 0 sec");

        display.display();
        return;
    }

    // =================================================
    // BOTH ALERTS ACTIVE
    // =================================================

    if (
        postureAlertActive &&
        sittingAlertActive
    ) {

        display.setCursor(0, 0);
        display.println("*** BOTH ALERTS ***");

        display.setCursor(0, 14);
        display.println(
            shortPostureName(currentPosture)
        );

        display.setCursor(0, 30);
        display.print("Sitting: ");
        display.print(localSittingSeconds);
        display.println(" sec");

        display.setCursor(0, 46);
        display.println("Fix posture + break!");

        display.display();
        return;
    }

    // =================================================
    // POSTURE ALERT ONLY
    // =================================================

    if (postureAlertActive) {

        display.setCursor(0, 0);
        display.println("*** POSTURE ALERT ***");

        display.setCursor(0, 14);
        display.println(
            shortPostureName(currentPosture)
        );

        display.setCursor(0, 30);
        display.print("Bad posture: ");
        display.print(currentBadPostureSeconds);
        display.println("s");

        display.setCursor(0, 46);
        display.println("FIX YOUR POSTURE!");

        display.display();
        return;
    }

    // =================================================
    // SITTING ALERT ONLY
    // =================================================

    if (sittingAlertActive) {

        display.setCursor(0, 0);
        display.println("*** SITTING ALERT ***");

        display.setCursor(0, 16);
        display.print("Sitting: ");
        display.print(localSittingSeconds);
        display.println(" sec");

        display.setCursor(0, 34);
        display.println("STAND UP!");

        display.setCursor(0, 50);
        display.println("Take a break.");

        display.display();
        return;
    }

    // =================================================
    // NORMAL STATUS
    // =================================================

    display.setCursor(0, 0);
    display.println("POSTURE MONITOR");

    display.setCursor(0, 14);
    display.println(
        shortPostureName(currentPosture)
    );

    display.setCursor(0, 30);
    display.print("Sitting: ");
    display.print(localSittingSeconds);
    display.println(" sec");

    display.setCursor(0, 46);

    if (currentBadPostureSeconds > 0) {

        display.print("Bad posture: ");
        display.print(currentBadPostureSeconds);
        display.println("s");
    }
    else {

        display.println("Alert: OFF");
    }

    display.display();
}

// =====================================================
// LOCAL 1-SECOND SITTING TIMER
// =====================================================

void updateLocalSittingTimer() {

    if (!chairOccupied) {

        localSittingSeconds = 0;
        return;
    }

    unsigned long now = millis();

    if (
        now - lastLocalTimerUpdate >= 1000
    ) {

        unsigned long elapsedSeconds =
            (now - lastLocalTimerUpdate) / 1000;

        localSittingSeconds +=
            elapsedSeconds;

        lastLocalTimerUpdate +=
            elapsedSeconds * 1000;

        updateOLED();
    }
}

// =====================================================
// RED LED
// =====================================================

void updateAlertLED() {

    // No alert of any kind
    if (!alertActive) {

        if (ledState) {

            ledState = false;
            digitalWrite(LED_PIN, LOW);
        }

        return;
    }

    // Any alert active -> blink continuously
    if (
        millis() - lastLedBlinkTime >=
        LED_BLINK_INTERVAL_MS
    ) {

        lastLedBlinkTime = millis();

        ledState = !ledState;

        digitalWrite(
            LED_PIN,
            ledState ? HIGH : LOW
        );
    }
}

// =====================================================
// RECEIVE MESSAGE FROM AZURE
// =====================================================

void messageCallback(
    char* topic,
    byte* payload,
    unsigned int length
) {

    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "MESSAGE RECEIVED FROM AZURE"
    );

    Serial.print("Topic: ");
    Serial.println(topic);

    // =================================================
    // CONVERT PAYLOAD TO STRING
    // =================================================

    String message;

    for (
        unsigned int i = 0;
        i < length;
        i++
    ) {

        message +=
            (char)payload[i];
    }

    Serial.print("Message: ");
    Serial.println(message);

    // =================================================
    // PARSE JSON
    // =================================================

    JsonDocument doc;

    DeserializationError error =
        deserializeJson(
            doc,
            message
        );

    if (error) {

        Serial.print(
            "JSON parsing failed: "
        );

        Serial.println(
            error.c_str()
        );

        Serial.println(
            "================================"
        );

        return;
    }

    // =================================================
    // READ CHAIR DATA
    // =================================================

    const char* receivedPosture =
        doc["posture"] | "UNKNOWN";

    unsigned long receivedSittingSeconds =
        doc["sittingSeconds"] | 0;

    unsigned long receivedBadSeconds =
        doc["badPostureSeconds"] | 0;

    bool receivedPostureAlert =
        doc["postureAlert"] | false;

    bool receivedSittingAlert =
        doc["sittingAlert"] | false;

    bool receivedAlert =
        doc["alert"] | false;

    const char* receivedAlertType =
        doc["alertType"] | "NONE";

    // =================================================
    // UPDATE LOCAL STATE
    // =================================================

    currentPosture =
        String(receivedPosture);

    localSittingSeconds =
        receivedSittingSeconds;

    currentBadPostureSeconds =
        receivedBadSeconds;

    postureAlertActive =
        receivedPostureAlert;

    sittingAlertActive =
        receivedSittingAlert;

    alertActive =
        receivedAlert;

    currentAlertType =
        String(receivedAlertType);

    receivedChairData = true;

    // Chair occupied unless explicitly empty
    chairOccupied =
        currentPosture != "CHAIR EMPTY";

    // Every Azure message resynchronizes
    // the local sitting timer.
    lastLocalTimerUpdate =
        millis();

    if (!chairOccupied) {

        localSittingSeconds = 0;
    }

    // =================================================
    // LED STATE
    // =================================================

    if (!alertActive) {

        ledState = false;

        digitalWrite(
            LED_PIN,
            LOW
        );
    }

    // =================================================
    // SERIAL DEBUG
    // =================================================

    Serial.print("Posture: ");
    Serial.println(currentPosture);

    Serial.print("Sitting: ");
    Serial.print(localSittingSeconds);
    Serial.println(" sec");

    Serial.print("Posture alert: ");

    if (postureAlertActive) {
        Serial.println("ACTIVE");
    }
    else {
        Serial.println("OFF");
    }

    Serial.print("Sitting alert: ");

    if (sittingAlertActive) {
        Serial.println("ACTIVE");
    }
    else {
        Serial.println("OFF");
    }

    Serial.print("Alert type: ");
    Serial.println(currentAlertType);

    // Update OLED immediately
    updateOLED();

    Serial.println(
        "================================"
    );

    Serial.println();
}

// =====================================================
// WIFI
// =====================================================

void connectWiFi() {

    if (
        WiFi.status() ==
        WL_CONNECTED
    ) {
        return;
    }

    Serial.print(
        "Connecting to Wi-Fi"
    );

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    while (
        WiFi.status() !=
        WL_CONNECTED
    ) {

        delay(500);
        Serial.print(".");
    }

    Serial.println();

    Serial.println(
        "Wi-Fi connected!"
    );

    Serial.print(
        "IP address: "
    );

    Serial.println(
        WiFi.localIP()
    );
}

// =====================================================
// AZURE MQTT
// =====================================================

void connectAzure() {

    while (
        !mqttClient.connected()
    ) {

        Serial.print(
            "Connecting desk ESP32 to Azure..."
        );

        bool connected =
            mqttClient.connect(
                DEVICE_ID,
                MQTT_USERNAME,
                SAS_TOKEN
            );

        if (connected) {

            Serial.println(
                "connected!"
            );

            bool subscribed =
                mqttClient.subscribe(
                    C2D_TOPIC
                );

            if (subscribed) {

                Serial.println(
                    "Subscribed for Azure messages!"
                );
            }

            else {

                Serial.println(
                    "Subscription failed."
                );
            }
        }

        else {

            Serial.print(
                "failed. MQTT state: "
            );

            Serial.println(
                mqttClient.state()
            );

            delay(3000);
        }
    }
}

// =====================================================
// SETUP
// =====================================================

void setup() {

    Serial.begin(115200);

    delay(1000);

    // =================================================
    // LED
    // =================================================

    pinMode(
        LED_PIN,
        OUTPUT
    );

    digitalWrite(
        LED_PIN,
        LOW
    );

    // =================================================
    // OLED
    // =================================================

    Wire.begin(
        OLED_SDA,
        OLED_SCL
    );

    if (
        !display.begin(
            SSD1306_SWITCHCAPVCC,
            OLED_ADDRESS
        )
    ) {

        Serial.println(
            "OLED failed to start."
        );

        while (true) {
            delay(1000);
        }
    }

    display.clearDisplay();

    display.setTextSize(1);

    display.setTextColor(
        SSD1306_WHITE
    );

    display.setCursor(0, 0);

    display.println(
        "CS147 Desk Monitor"
    );

    display.setCursor(0, 20);

    display.println(
        "Connecting..."
    );

    display.display();

    // =================================================
    // WIFI
    // =================================================

    connectWiFi();

    // =================================================
    // AZURE
    // =================================================

    secureClient.setInsecure();

    mqttClient.setServer(
        IOT_HUB_HOST,
        MQTT_PORT
    );

    mqttClient.setCallback(
        messageCallback
    );

    mqttClient.setBufferSize(
        1024
    );

    connectAzure();

    // =================================================
    // READY
    // =================================================

    showWaitingScreen();

    Serial.println();

    Serial.println(
        "DESK ESP32 READY"
    );

    Serial.println(
        "Waiting for chair..."
    );

    Serial.println();
}

// =====================================================
// LOOP
// =====================================================

void loop() {

    // =================================================
    // KEEP WIFI ALIVE
    // =================================================

    if (
        WiFi.status() !=
        WL_CONNECTED
    ) {

        connectWiFi();
    }

    // =================================================
    // KEEP AZURE ALIVE
    // =================================================

    if (
        !mqttClient.connected()
    ) {

        connectAzure();
    }

    mqttClient.loop();

    // Local OLED sitting timer
    updateLocalSittingTimer();

    // Flash LED if either alert is active
    updateAlertLED();

    delay(10);
}
