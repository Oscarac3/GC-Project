#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// =====================================================
// WIFI
// =====================================================

#define WIFI_SSID     "Your Own"
#define WIFI_PASSWORD "Your Own "

// =====================================================
// AZURE
// =====================================================

#define IOT_HUB_HOST "Your Own"
#define DEVICE_ID    "chair-esp32"

#define SAS_TOKEN "Your Own"

const char* IOT_ENDPOINT =
    "https://" IOT_HUB_HOST
    "/devices/" DEVICE_ID
    "/messages/events?api-version=2021-04-12";

// Normal telemetry every 15 seconds
const unsigned long TELEMETRY_INTERVAL_MS = 15000;

unsigned long lastTelemetryTime = 0;

// =====================================================
// FSR PINS
// =====================================================

#define SEAT_FSR_PIN   34
#define LUMBAR_FSR_PIN 35
#define LEFT_FSR_PIN   32
#define RIGHT_FSR_PIN  33

// =====================================================
// SENSOR THRESHOLDS
// =====================================================

#define SEAT_THRESHOLD       1000
#define LUMBAR_THRESHOLD     2500
#define SIDE_TOTAL_MIN       800
#define BALANCE_THRESHOLD    0.40

// Bad posture alert after 30 seconds
#define BAD_POSTURE_TIME_MS 30000

// Sitting-too-long alert
// Keep your current demo value here.
// 120000 = 2 minutes
#define SITTING_ALERT_TIME_MS 120000

// =====================================================
// POSTURE TYPES
// =====================================================

enum Posture {
    EMPTY,
    GOOD,
    LEAN_LEFT,
    LEAN_RIGHT,
    NO_LUMBAR_SUPPORT,
    UNCERTAIN
};

bool isSitting = false;

unsigned long sittingStartTime = 0;
unsigned long badPostureStartTime = 0;

// This tells us whether we are currently in one
// continuous period of bad posture.
bool badPostureTiming = false;

bool postureAlertActive = false;
bool sittingAlertActive = false;

// =====================================================
// READ SENSOR AVERAGE
// =====================================================

int readAverage(int pin) {

    long total = 0;

    for (int i = 0; i < 5; i++) {

        total += analogRead(pin);
        delay(5);
    }

    return total / 5;
}

// =====================================================
// POSTURE NAME
// =====================================================

const char* postureName(Posture posture) {

    switch (posture) {

        case EMPTY:
            return "CHAIR EMPTY";

        case GOOD:
            return "GOOD / CENTERED";

        case LEAN_LEFT:
            return "LEANING LEFT";

        case LEAN_RIGHT:
            return "LEANING RIGHT";

        case NO_LUMBAR_SUPPORT:
            return "NO LUMBAR SUPPORT / LEANING FORWARD";

        case UNCERTAIN:
            return "UNCERTAIN / LOW SIDE CONTACT";

        default:
            return "UNKNOWN";
    }
}

// =====================================================
// ALERT TYPE
// =====================================================

const char* alertTypeName(
    bool postureAlert,
    bool sittingAlert
) {

    if (postureAlert && sittingAlert) {
        return "POSTURE_AND_SITTING";
    }

    if (postureAlert) {
        return "POSTURE";
    }

    if (sittingAlert) {
        return "SITTING";
    }

    return "NONE";
}

// =====================================================
// CLASSIFY POSTURE
// =====================================================

Posture classifyPosture(
    int seatValue,
    int lumbarValue,
    int leftValue,
    int rightValue
) {

    // Nobody sitting
    if (seatValue < SEAT_THRESHOLD) {
        return EMPTY;
    }

    // No lumbar contact
    if (lumbarValue < LUMBAR_THRESHOLD) {
        return NO_LUMBAR_SUPPORT;
    }

    int sideTotal =
        leftValue + rightValue;

    // Not enough side pressure to confidently
    // determine left/right balance
    if (sideTotal < SIDE_TOTAL_MIN) {
        return UNCERTAIN;
    }

    float balance =
        (float)(leftValue - rightValue) /
        (float)sideTotal;

    if (balance > BALANCE_THRESHOLD) {
        return LEAN_LEFT;
    }

    if (balance < -BALANCE_THRESHOLD) {
        return LEAN_RIGHT;
    }

    return GOOD;
}

// =====================================================
// WIFI
// =====================================================

void connectWiFi() {

    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    Serial.println();
    Serial.print("Connecting to Wi-Fi");

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    while (
        WiFi.status() != WL_CONNECTED
    ) {

        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("Wi-Fi connected!");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.println();
}

// =====================================================
// SEND TELEMETRY TO AZURE
// =====================================================

void sendTelemetry(
    int seatValue,
    int lumbarValue,
    int leftValue,
    int rightValue,
    Posture posture,
    unsigned long sittingSeconds,
    unsigned long badPostureSeconds,
    bool postureAlert,
    bool sittingAlert
) {

    connectWiFi();

    WiFiClientSecure client;

    client.setInsecure();

    HTTPClient http;

    if (!http.begin(client, IOT_ENDPOINT)) {

        Serial.println(
            "Azure connection failed."
        );

        return;
    }

    http.addHeader(
        "Content-Type",
        "application/json"
    );

    http.addHeader(
        "Authorization",
        SAS_TOKEN
    );

    // =================================================
    // JSON
    // =================================================

    JsonDocument doc;

    doc["device"] = DEVICE_ID;

    doc["seat"] = seatValue;
    doc["lumbar"] = lumbarValue;
    doc["left"] = leftValue;
    doc["right"] = rightValue;

    doc["posture"] =
        postureName(posture);

    doc["sittingSeconds"] =
        sittingSeconds;

    doc["badPostureSeconds"] =
        badPostureSeconds;

    doc["postureAlert"] =
        postureAlert;

    doc["sittingAlert"] =
        sittingAlert;

    doc["alert"] =
        postureAlert ||
        sittingAlert;

    doc["alertType"] =
        alertTypeName(
            postureAlert,
            sittingAlert
        );

    String json;

    serializeJson(
        doc,
        json
    );

    // =================================================
    // SEND
    // =================================================

    Serial.println();
    Serial.println(
        "Sending to Azure:"
    );

    Serial.println(json);

    int httpResponseCode =
        http.POST(json);

    Serial.print(
        "Azure HTTP response: "
    );

    Serial.println(
        httpResponseCode
    );

    if (httpResponseCode == 204) {

        Serial.println(
            "Telemetry sent successfully!"
        );
    }

    else {

        Serial.println(
            "Telemetry FAILED."
        );
    }

    Serial.println();

    http.end();
}

// =====================================================
// SETUP
// =====================================================

void setup() {

    Serial.begin(115200);

    delay(1000);

    connectWiFi();

    Serial.println(
        "Chair posture monitor started."
    );
}

// =====================================================
// LOOP
// =====================================================

void loop() {

    int seatValue =
        readAverage(SEAT_FSR_PIN);

    int lumbarValue =
        readAverage(LUMBAR_FSR_PIN);

    int leftValue =
        readAverage(LEFT_FSR_PIN);

    int rightValue =
        readAverage(RIGHT_FSR_PIN);

    Posture posture =
        classifyPosture(
            seatValue,
            lumbarValue,
            leftValue,
            rightValue
        );

    unsigned long sittingSeconds = 0;
    unsigned long badPostureSeconds = 0;

    // Important event that should be sent
    // immediately instead of waiting 15 sec.
    bool immediateTelemetryNeeded = false;

    // =================================================
    // CHAIR EMPTY
    // =================================================

    if (posture == EMPTY) {

        // Person JUST stood up
        if (isSitting) {

            Serial.println();
            Serial.println(
                "=============================="
            );

            Serial.println(
                "CHAIR EMPTY"
            );

            Serial.println(
                "Sitting timer reset."
            );

            Serial.println(
                "Bad posture timer reset."
            );

            Serial.println(
                "Sending update immediately."
            );

            Serial.println(
                "=============================="
            );

            Serial.println();

            isSitting = false;

            sittingStartTime = 0;

            // Reset continuous bad-posture timer
            badPostureStartTime = 0;
            badPostureTiming = false;

            postureAlertActive = false;
            sittingAlertActive = false;

            immediateTelemetryNeeded = true;
        }
    }

    // =================================================
    // CHAIR OCCUPIED
    // =================================================

    else {

        // Person JUST sat down
        if (!isSitting) {

            isSitting = true;

            sittingStartTime =
                millis();

            badPostureStartTime = 0;
            badPostureTiming = false;

            postureAlertActive = false;
            sittingAlertActive = false;

            Serial.println();
            Serial.println(
                "=============================="
            );

            Serial.println(
                "CHAIR OCCUPIED"
            );

            Serial.println(
                "Sitting timer started."
            );

            Serial.println(
                "Sending update immediately."
            );

            Serial.println(
                "=============================="
            );

            Serial.println();

            immediateTelemetryNeeded = true;
        }

        sittingSeconds =
            (millis() - sittingStartTime)
            / 1000;

        // =================================================
        // SITTING TOO LONG
        // =================================================

        if (
            !sittingAlertActive &&
            millis() - sittingStartTime >=
                SITTING_ALERT_TIME_MS
        ) {

            sittingAlertActive = true;

            immediateTelemetryNeeded = true;

            Serial.println();
            Serial.println(
                "=============================="
            );

            Serial.println(
                "SITTING ALERT!"
            );

            Serial.println(
                "YOU HAVE BEEN SITTING TOO LONG"
            );

            Serial.println(
                "STAND UP AND TAKE A BREAK!"
            );

            Serial.println(
                "=============================="
            );

            Serial.println();
        }

        // =================================================
        // GOOD POSTURE
        // =================================================

        if (posture == GOOD) {

            // ---------------------------------------------
            // GOOD POSTURE IS WHAT RESETS THE
            // BAD-POSTURE TIMER
            // ---------------------------------------------

            if (badPostureTiming) {

                Serial.println();
                Serial.println(
                    "Bad posture timer reset:"
                );

                Serial.println(
                    "GOOD POSTURE DETECTED"
                );

                Serial.println();
            }

            badPostureStartTime = 0;
            badPostureTiming = false;

            // If posture alert was active,
            // clear it immediately.
            if (postureAlertActive) {

                postureAlertActive = false;

                immediateTelemetryNeeded = true;

                Serial.println();
                Serial.println(
                    "=============================="
                );

                Serial.println(
                    "POSTURE ALERT CLEARED"
                );

                Serial.println(
                    "POSTURE FIXED"
                );

                Serial.println(
                    "=============================="
                );

                Serial.println();
            }
        }

        // =================================================
        // ANY BAD POSTURE
        // =================================================

        else {

            // ---------------------------------------------
            // START BAD-POSTURE TIMER ONLY ONCE
            //
            // IMPORTANT:
            // Switching between LEFT, RIGHT,
            // FORWARD, and UNCERTAIN does NOT
            // restart this timer.
            // ---------------------------------------------

            if (!badPostureTiming) {

                badPostureTiming = true;

                badPostureStartTime =
                    millis();

                Serial.println();
                Serial.println(
                    "Bad posture timer started."
                );

                Serial.println();
            }

            // Timer continues regardless of which
            // bad posture type is currently detected.
            badPostureSeconds =
                (
                    millis() -
                    badPostureStartTime
                ) / 1000;

            // =================================================
            // 30 SECOND POSTURE ALERT
            // =================================================

            if (
                !postureAlertActive &&
                millis() -
                    badPostureStartTime >=
                    BAD_POSTURE_TIME_MS
            ) {

                postureAlertActive = true;

                immediateTelemetryNeeded = true;

                Serial.println();
                Serial.println(
                    "=============================="
                );

                Serial.print(
                    "POSTURE ALERT: "
                );

                // This still shows the CURRENT
                // detected posture.
                Serial.println(
                    postureName(posture)
                );

                Serial.println(
                    "BAD POSTURE FOR 30 SECONDS!"
                );

                Serial.println(
                    "=============================="
                );

                Serial.println();
            }
        }
    }

    // =================================================
    // SERIAL STATUS
    // =================================================

    Serial.print("Seat: ");
    Serial.print(seatValue);

    Serial.print(" | Lumbar: ");
    Serial.print(lumbarValue);

    Serial.print(" | Left: ");
    Serial.print(leftValue);

    Serial.print(" | Right: ");
    Serial.print(rightValue);

    Serial.print(" | Sitting: ");
    Serial.print(sittingSeconds);
    Serial.print(" sec");

    Serial.print(" | Posture: ");
    Serial.print(
        postureName(posture)
    );

    Serial.print(
        " | Posture Alert: "
    );

    if (postureAlertActive) {
        Serial.print("ACTIVE");
    }

    else {
        Serial.print("OFF");
    }

    Serial.print(
        " | Sitting Alert: "
    );

    if (sittingAlertActive) {
        Serial.println("ACTIVE");
    }

    else {
        Serial.println("OFF");
    }

    // Show the continuous bad-posture timer
    if (
        posture != GOOD &&
        posture != EMPTY
    ) {

        Serial.print(
            "Bad posture time: "
        );

        Serial.print(
            badPostureSeconds
        );

        Serial.println(
            " / 30 sec"
        );
    }

    // =================================================
    // IMPORTANT EVENT -> IMMEDIATE AZURE UPDATE
    // =================================================

    if (immediateTelemetryNeeded) {

        sendTelemetry(
            seatValue,
            lumbarValue,
            leftValue,
            rightValue,
            posture,
            sittingSeconds,
            badPostureSeconds,
            postureAlertActive,
            sittingAlertActive
        );

        lastTelemetryTime =
            millis();
    }

    // =================================================
    // NORMAL 15 SECOND CLOUD SYNC
    // =================================================

    else if (
        millis() -
            lastTelemetryTime >=
            TELEMETRY_INTERVAL_MS
    ) {

        sendTelemetry(
            seatValue,
            lumbarValue,
            leftValue,
            rightValue,
            posture,
            sittingSeconds,
            badPostureSeconds,
            postureAlertActive,
            sittingAlertActive
        );

        lastTelemetryTime =
            millis();
    }

    delay(500);
}
