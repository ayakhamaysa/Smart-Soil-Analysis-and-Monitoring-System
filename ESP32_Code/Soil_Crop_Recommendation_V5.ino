/*
=================================================
 Smart Soil Monitoring & Crop Recommendation V4
 ESP32-WROOM-32D
 Top 5 Crop Recommendations 

=================================================

Sensors
-------
DHT22
DS18B20
HW080 Soil Moisture
GPS NEO-6M

AI: TinyML Crop Recommendation
    (Edge Impulse)

Cloud:  Firebase Realtime Database
=================================================
*/

// ================= WIFI =================

#define WIFI_SSID      "YOUR_WIFI_NAME"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"


// ================= FIREBASE =================

#define DATABASE_URL "https://smartsoil-e479d-default-rtdb.firebaseio.com/"

#define FIREBASE_LIVE_URL \
"https://smartsoil-e479d-default-rtdb.firebaseio.com/devices/device_001/live.json"

// =====================================================
// Libraries
// =====================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TinyGPSPlus.h>
#include <Crop_Recommendation_TinyML_inferencing.h>

bool wifiConnected = false;
int wifiRSSI = 0;


// =====================================================
// Dataset Statistics
// =====================================================

    const float N_MEAN = 50.5518;
    const float N_STD  = 36.9173;
    const float N_MIN  = 0.0;
    const float N_MAX  = 140.0;

    const float P_MEAN = 53.3627;
    const float P_STD  = 32.9859;
    const float P_MIN  = 5.0;
    const float P_MAX  = 145.0;

    const float K_MEAN = 48.1491;
    const float K_STD  = 50.6479;
    const float K_MIN  = 5.0;
    const float K_MAX  = 205.0;

    const float PH_MEAN = 6.46948;
    const float PH_STD  = 0.77394;
    const float PH_MIN  = 3.50475;
    const float PH_MAX  = 9.93509;

    const float RAIN_MEAN = 103.4637;
    const float RAIN_STD  = 54.9584;
    const float RAIN_MIN  = 20.2113;
    const float RAIN_MAX  = 298.5601;


// =====================================================
// Gaussian Random Number Generator
// Box-Muller Transform
// =====================================================

float randomGaussian()
{
    float u1 =
        random(1, 10000) / 10000.0;

    float u2 =
        random(1, 10000) / 10000.0;

    return
        sqrt(-2.0 * log(u1))
        *
        cos(2.0 * PI * u2);
}

// =====================================================
// Generate Dataset Value
// =====================================================

float generateDatasetValue(
    float mean,
    float stdDev,
    float minValue,
    float maxValue)
{
    float value =
        mean +
        randomGaussian() * stdDev;

    return constrain(
        value,
        minValue,
        maxValue);
}

// =====================================================
// TinyML Feature Buffer
// =====================================================

float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

static int get_signal_data(
    size_t offset,
    size_t length,
    float *out_ptr)
{
    memcpy(
        out_ptr,
        features + offset,
        length * sizeof(float));

    return 0;
}

// =====================================================
// Top Crop Structure
// =====================================================

struct CropPrediction
{
    String label;
    float confidence;
};

// =====================================================
// DHT22
// =====================================================

#define DHTPIN 4
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

// =====================================================
// DS18B20
// =====================================================

#define ONE_WIRE_BUS 18

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature soilTempSensor(&oneWire);

// =====================================================
// Soil Moisture
// =====================================================

#define SOIL_MOISTURE_PIN 32

const int DRY_VALUE = 4095;
const int WET_VALUE = 1600;

// =====================================================
// GPS
// =====================================================

TinyGPSPlus gps;

HardwareSerial GPSSerial(1);

#define GPS_RX_PIN 26
#define GPS_TX_PIN 27

// =====================================================
// Setup
// =====================================================

float simN;
float simP;
float simK;
float simPH;
float simRainfall;
unsigned long lastRandomUpdate = 0;
const unsigned long RANDOM_INTERVAL = 10000;

void setup()
{
    Serial.begin(115200);
    randomSeed(micros());
    // ---------------- WiFi ----------------

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("WiFi Connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // ---------------- Sensors ----------------

    dht.begin();

    soilTempSensor.begin();

    GPSSerial.begin(
        9600,
        SERIAL_8N1,
        GPS_RX_PIN,
        GPS_TX_PIN);

    Serial.println();
    Serial.println("========================================");
    Serial.println(" Smart Soil Monitoring System V3");
    Serial.println(" TinyML Crop Recommendation");
    Serial.println("========================================");

    simN =
    generateDatasetValue(
        N_MEAN,
        N_STD,
        N_MIN,
        N_MAX);

    simP =
    generateDatasetValue(
        P_MEAN,
        P_STD,
        P_MIN,
        P_MAX);

    simK =
    generateDatasetValue(
        K_MEAN,
        K_STD,
        K_MIN,
        K_MAX);

    simPH =
    generateDatasetValue(
        PH_MEAN,
        PH_STD,
        PH_MIN,
        PH_MAX);

    simRainfall =
    generateDatasetValue(
        RAIN_MEAN,
        RAIN_STD,
        RAIN_MIN,
        RAIN_MAX);
    
    delay(2000);
}

void loop()
{
    // =====================================================
    // Feed GPS Continuously
    // =====================================================

    while (GPSSerial.available())
    {
        gps.encode(GPSSerial.read());
    }

    // =====================================================
    // Read DHT22
    // =====================================================

    float airTemp = dht.readTemperature();
    float airHum  = dht.readHumidity();

    if (isnan(airTemp) || isnan(airHum))
    {
        Serial.println();
        Serial.println("========================================");
        Serial.println("DHT22 ERROR - Skipping Cycle");
        Serial.println("========================================");

        delay(5000);
        return;
    }

    // =====================================================
    // Read DS18B20
    // =====================================================

    soilTempSensor.requestTemperatures();

    float soilTemp =
        soilTempSensor.getTempCByIndex(0);

    // =====================================================
    // Read Soil Moisture
    // =====================================================

    int moistureRaw =
        analogRead(SOIL_MOISTURE_PIN);

    int moisturePercent =
        map(
            moistureRaw,
            DRY_VALUE,
            WET_VALUE,
            0,
            100);

    moisturePercent =
        constrain(
            moisturePercent,
            0,
            100);

    if (millis() - lastRandomUpdate >= RANDOM_INTERVAL)
    {
        lastRandomUpdate = millis();

        simN =
        generateDatasetValue(
        N_MEAN,
        N_STD,
        N_MIN,
        N_MAX);

        simP =
        generateDatasetValue( 
        P_MEAN,
        P_STD,
        P_MIN,
        P_MAX);

        simK =
        generateDatasetValue(
        K_MEAN,
        K_STD,
        K_MIN,
        K_MAX);

        simPH =
        generateDatasetValue(
        PH_MEAN,
        PH_STD,
        PH_MIN,
        PH_MAX);

        simRainfall =
        generateDatasetValue(
        RAIN_MEAN,
        RAIN_STD,
        RAIN_MIN,
        RAIN_MAX);

        Serial.println();
        Serial.println("New Simulated Soil Profile Generated");
    }

    // =====================================================
    // TinyML Feature Vector
    // =====================================================
    features[0] = simN;
    features[1] = simP;
    features[2] = simK;
    features[5] = simPH;
    features[6] = simRainfall;

    // =====================================================
    // Print Sensor Readings
    // =====================================================

    Serial.println();
    Serial.println("========================================");

    Serial.print("Air Temperature : ");
    Serial.print(airTemp,1);
    Serial.println(" C");

    Serial.print("Air Humidity    : ");
    Serial.print(airHum,1);
    Serial.println(" %");

    Serial.print("Soil Temperature: ");
    Serial.print(soilTemp,2);
    Serial.println(" C");

    Serial.print("Soil Moisture   : ");
    Serial.print(moisturePercent);
    Serial.println(" %");

    Serial.print("Moisture Raw    : ");
    Serial.println(moistureRaw);

    Serial.println();

    // =====================================================
    // GPS
    // =====================================================

    if (gps.location.isValid())
    {
        Serial.println("GPS             : FIX ACQUIRED");

        Serial.print("Latitude        : ");
        Serial.println(
            gps.location.lat(),
            6);

        Serial.print("Longitude       : ");
        Serial.println(
            gps.location.lng(),
            6);

        Serial.print("Satellites      : ");
        Serial.println(
            gps.satellites.value());
    }
    else
    {
        Serial.println("GPS             : No Fix Yet");

        Serial.print("Satellites Seen : ");
        Serial.println(
            gps.satellites.value());

        Serial.print("Chars Processed : ");
        Serial.println(
            gps.charsProcessed());
    }

    // =====================================================
    // TinyML Inputs
    // =====================================================

    Serial.println();
    Serial.println("============= TinyML Inputs =============");

    Serial.print("Nitrogen   : ");
    Serial.println(features[0]);

    Serial.print("Phosphorus : ");
    Serial.println(features[1]);

    Serial.print("Potassium  : ");
    Serial.println(features[2]);

    Serial.print("pH         : ");
    Serial.println(features[5]);

    Serial.print("Rainfall   : ");
    Serial.println(features[6]);

    Serial.println("========================================");

    // =====================================================
    // Run TinyML
    // =====================================================

    signal_t signal;

    signal.total_length =
        EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;

    signal.get_data =
        &get_signal_data;

    ei_impulse_result_t result = {0};

    EI_IMPULSE_ERROR res =
        run_classifier(
            &signal,
            &result,
            false);

    if (res != EI_IMPULSE_OK)
    {
        Serial.print("Inference Error : ");
        Serial.println(res);

        delay(5000);
        return;
    }

    // =====================================================
    // Sort Predictions
    // =====================================================

    CropPrediction predictions[EI_CLASSIFIER_LABEL_COUNT];

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
    {
        predictions[i].label =
            result.classification[i].label;

        predictions[i].confidence =
            result.classification[i].value;
    }

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT - 1; i++)
    {
        for (size_t j = i + 1; j < EI_CLASSIFIER_LABEL_COUNT; j++)
        {
            if (predictions[j].confidence >
                predictions[i].confidence)
            {
                CropPrediction temp =
                    predictions[i];

                predictions[i] =
                    predictions[j];

                predictions[j] =
                    temp;
            }
        }
    }

    String bestCrop =
        predictions[0].label;

    float bestScore =
        predictions[0].confidence;

    // =====================================================
    // Print Top 5 Recommendations
    // =====================================================

    Serial.println();
    Serial.println("========== TOP 5 RECOMMENDATIONS ==========");

    for (int i = 0; i < 5; i++)
    {
        Serial.print(i + 1);
        Serial.print(". ");

        Serial.print(predictions[i].label);

        Serial.print(" --> ");

        Serial.print(predictions[i].confidence * 100.0, 20);
        Serial.println("%");
    }

    Serial.println();

    Serial.print("Recommended Crop : ");
    Serial.println(bestCrop);

    Serial.print("Confidence       : ");
    Serial.print(bestScore * 100.0,2);
    Serial.println("%");

    Serial.print("DSP Time         : ");
    Serial.print(result.timing.dsp);
    Serial.println(" ms");

    Serial.print("Classification   : ");
    Serial.print(result.timing.classification);
    Serial.println(" ms");

    Serial.println("========================================");

    // Part 3:
    // Firebase upload (including Top 5 recommendations)
    // Delay(5000)
    // =====================================================
    // FIREBASE UPLOAD
    // =====================================================

    wifiConnected = (WiFi.status() == WL_CONNECTED);

    if (wifiConnected)
    {
        wifiRSSI = WiFi.RSSI();
    }
    else
    {
        wifiRSSI = 0;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;

        http.begin(FIREBASE_LIVE_URL);

        http.addHeader(
            "Content-Type",
            "application/json");

        StaticJsonDocument<1024> doc;

        // ===============================
        // Sensor Values
        // ===============================
        doc["nitrogen"] = simN;
        doc["phosphorus"] = simP;
        doc["potassium"] = simK;
        doc["ph"] = simPH;
        doc["rainfall"] = simRainfall;
        doc["airTemperature"]  = airTemp;
        doc["airHumidity"]     = airHum;
        doc["soilTemperature"] = soilTemp;
        doc["soilMoisture"]    = moisturePercent;

        // ===============================
        // Best Recommendation
        // ===============================

        doc["cropRecommendation"] = bestCrop;
        doc["confidence"] = bestScore * 100.0;

        // ===============================
        // Top 5 Recommendations
        // ===============================

        JsonArray top5 =
            doc.createNestedArray("top5");

        for (int i = 0; i < 5; i++)
        {
            JsonObject crop =
                top5.createNestedObject();

            crop["rank"] = i + 1;

            crop["name"] =
                predictions[i].label;

            crop["confidence"] =
                predictions[i].confidence * 100.0;
        }

        // ===============================
        // GPS
        // ===============================

        if (gps.location.isValid())
        {
            doc["latitude"] =
                gps.location.lat();

            doc["longitude"] =
                gps.location.lng();
        }
        else
        {
            doc["latitude"] = 0.0;
            doc["longitude"] = 0.0;
        }

        // ===============================
        // Device Status
        // ===============================

        doc["status"] = "active";
        doc["lastSeen"] = millis();

        JsonObject wifi =
            doc.createNestedObject("wifi");

        wifi["connected"] = wifiConnected;
        wifi["ssid"] = WIFI_SSID;
        wifi["rssi"] = wifiRSSI;
        
        doc["updatedAt"] =
            millis() / 1000;

        // ===============================
        // Serialize JSON
        // ===============================

        String jsonPayload;

        serializeJson(
            doc,
            jsonPayload);

        // ===============================
        // Upload
        // ===============================

        int httpResponseCode =
            http.PATCH(jsonPayload);

        Serial.println();

        Serial.print("Firebase HTTP Response : ");

        Serial.println(httpResponseCode);

        if (httpResponseCode > 0)
        {
            Serial.println("Firebase Upload Successful");
        }
        else
        {
            Serial.println("Firebase Upload Failed");
        }

        http.end();
    }
    else
    {
        Serial.println();
        Serial.println("WiFi Disconnected - Firebase Upload Skipped");
    }

    Serial.println();
    Serial.println("Next Reading In 10 Seconds...");
    Serial.println("========================================");

    delay(5000);
}
