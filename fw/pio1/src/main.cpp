// Karl Palsson,
// Nov 2016
// Gross basic start at OTA+leds.
// missing: AP end user setup!
#include <FS.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
extern "C" {
#include "sha1/sha1.h"
}
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>
#include <WiFiManager.h>
#include <AsyncMqttClient.h>
#include <Ticker.h>

const uint16_t PixelCount = 4;
const uint8_t PixelPin = 2; // make sure to set this to the correct pin, ignored for Esp8266
const uint8_t AnimationChannels = 1; // we only need one as all the pixels are animated at once

// This is the pin we have wired up. (works with both nodemcu+arduino codebases)
NeoPixelBus<NeoGrbFeature, NeoEsp8266Uart800KbpsMethod> strip(PixelCount, PixelPin);

NeoPixelAnimator animations(AnimationChannels); // NeoPixel animation management object

uint16_t effectState = 0; // general purpose variable used to store effect state

char mqtt_host[60] = "...";
char mqtt_port[6] = "1883";

int id = ESP.getChipId();
String host = String("advent-" + String(id, HEX));
char password[20];

const char* update_path = "/firmware";
const char* update_username = "...";
const char* update_password = "...";

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater(true);

AsyncMqttClient mqttClient;
int mqttTicker;

Ticker ledDriver;

struct ledBlinkerState {
    int count;
    Ticker *ticker;
};

struct ledBlinkerState ledBlinker;

void tickLedBootup(int i)
{
    strip.RotateLeft(1);
    strip.Show();
}

void tickLedBlinker(struct ledBlinkerState *state)
{
    if (state->count % 2) {
        strip.SetPixelColor(0, RgbColor(0, 0, 0));
        strip.SetPixelColor(1, RgbColor(40, 40, 0));
    } else {
        strip.SetPixelColor(0, RgbColor(0, 40, 40));
        strip.SetPixelColor(1, RgbColor(0, 0, 0));
    }
    strip.Show();
    state->count--;
    if (state->count <= 0) {
        state->ticker->detach();
    }
}

void tickLedBlinkerError(struct ledBlinkerState *state)
{
    if (state->count % 3 == 0) {
        strip.SetPixelColor(0, RgbColor(40, 0, 0));
        strip.SetPixelColor(1, RgbColor(0, 0, 0));
        strip.SetPixelColor(2, RgbColor(0, 0, 0));
    } else if (state->count % 2 == 0) {
        strip.SetPixelColor(0, RgbColor(20, 0, 0));
        strip.SetPixelColor(1, RgbColor(40, 0, 0));
        strip.SetPixelColor(2, RgbColor(00, 0, 0));
    } else {
        strip.SetPixelColor(0, RgbColor(0, 0, 0));
        strip.SetPixelColor(1, RgbColor(20, 0, 0));
        strip.SetPixelColor(2, RgbColor(40, 0, 0));
    }
    strip.Show();
    state->count--;
    if (state->count <= 0) {
        state->ticker->detach();
    }
}

// what is stored for state is specific to the need, in this case, the colors.
// basically what ever you need inside the animation update function

struct MyAnimationState {
    RgbColor StartingColor;
    RgbColor EndingColor;
};

// one entry per pixel to match the animation timing manager
MyAnimationState animationState[AnimationChannels];

void SetRandomSeed()
{
    uint32_t seed;

    // random works best with a seed that can use 31 bits
    // analogRead on a unconnected pin tends toward less than four bits
    seed = analogRead(0);
    delay(1);

    for (int shifts = 3; shifts < 31; shifts += 3) {
        seed ^= analogRead(0) << shifts;
        delay(1);
    }
    Serial.printf("Randoms seed = %#x\n", seed);

    randomSeed(seed);
}

// simple blend function

void BlendAnimUpdate(const AnimationParam& param)
{
    // this gets called for each animation on every time step
    // progress will start at 0.0 and end at 1.0
    // we use the blend function on the RgbColor to mix
    // color based on the progress given to us in the animation
    RgbColor updatedColor = RgbColor::LinearBlend(
            animationState[param.index].StartingColor,
            animationState[param.index].EndingColor,
            param.progress);

    // apply the color to the strip
    for (uint16_t pixel = 0; pixel < PixelCount; pixel++) {
        strip.SetPixelColor(pixel, updatedColor);
    }
}

void LoopAnimUpdate(const AnimationParam& param)
{
    //Serial.printf("anim: %f\n", param.progress);
    if (int(param.progress * 1000) % 30 == 0) {
        strip.RotateRight(1);
    }
}

void FadeInFadeOutRinseRepeat(float luminance)
{
    if (effectState == 0) {
        // Fade upto a random color
        // we use HslColor object as it allows us to easily pick a hue
        // with the same saturation and luminance so the colors picked
        // will have similiar overall brightness
        RgbColor target = HslColor(random(360) / 360.0f, 1.0f, luminance);
        uint16_t time = random(800, 2000);

        animationState[0].StartingColor = strip.GetPixelColor(0);
        animationState[0].EndingColor = target;

        animations.StartAnimation(0, time, BlendAnimUpdate);
    } else if (effectState == 1) {
        // fade to black
        uint16_t time = random(600, 700);

        animationState[0].StartingColor = strip.GetPixelColor(0);
        animationState[0].EndingColor = RgbColor(0);

        animations.StartAnimation(0, time, BlendAnimUpdate);
    } else if (effectState == 2) {
        int batt = analogRead(A0);
        Serial.printf("'Battery' adc = %d\n", batt);
        String topicBase = "advent/" + host + "/s";
        String tb = topicBase + "/batt";
        mqttClient.publish(tb.c_str(), 0, false, String(batt).c_str());
        String tt = topicBase + "/tick";
        mqttClient.publish(tt.c_str(), 0, false, String(mqttTicker++).c_str());
        // Pick a random colour, and a few versions of it...
        RgbColor col = HslColor(random(360) / 360.0f, 1.0f, luminance);
        strip.ClearTo(RgbColor(0));
        strip.SetPixelColor(0, col);

        animations.StartAnimation(0, 2000, LoopAnimUpdate);
    }

    // toggle to the next effect state
    effectState = (effectState + 1) % 3;
}

void cbWiFiEvent(WiFiEvent_t event)
{
    Serial.printf("[WiFi-event] event: %d\n", event);

    switch (event) {
    case WIFI_EVENT_STAMODE_GOT_IP:
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
        break;
    case WIFI_EVENT_STAMODE_DISCONNECTED:
        Serial.println("WiFi lost connection");
        break;
    case WIFI_EVENT_STAMODE_CONNECTED:
        Serial.println("event stamode connected");
        break;
    case WIFI_EVENT_STAMODE_AUTHMODE_CHANGE:
        Serial.println("event stamode auth changed");
        break;
    case WIFI_EVENT_STAMODE_DHCP_TIMEOUT:
        Serial.println("stamode dhcp timeout");
        break;
    case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
        Serial.println("softap staconn");
        break;
    case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
        Serial.println("softap disconn");
        break;
    case WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED:
        Serial.println("softap probreq");
        break;
    }
}

void setup_dump_flashinfo()
{
    uint32_t realSize = ESP.getFlashChipRealSize();
    uint32_t ideSize = ESP.getFlashChipSize();
    FlashMode_t ideMode = ESP.getFlashChipMode();

    Serial.printf("Flash real id:   %08X\n", ESP.getFlashChipId());
    Serial.printf("Flash real size: %u\n\n", realSize);

    Serial.printf("Flash ide  size: %u\n", ideSize);
    Serial.printf("Flash ide speed: %u\n", ESP.getFlashChipSpeed());
    Serial.printf("Flash ide mode:  %s\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));

    if (ideSize != realSize) {
        Serial.println("Flash Chip configuration wrong!\n");
    } else {
        Serial.println("Flash Chip configuration ok.\n");
    }

}

void wifi_scan()
{
    // WiFi.scanNetworks will return the number of networks found
    int n = WiFi.scanNetworks();
    Serial.println("scan done");
    if (n == 0)
        Serial.println("no networks found");
    else {
        Serial.print(n);
        Serial.println(" networks found");
        for (int i = 0; i < n; ++i) {
            // Print SSID and RSSI for each network found
            Serial.print(i + 1);
            Serial.print(": ");
            Serial.print(WiFi.SSID(i));
            Serial.print(" (");
            Serial.print(WiFi.RSSI(i));
            Serial.print(")");
            Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
            delay(10);
        }
    }
    Serial.println("");

}

void ota_onStart()
{
    Serial.println("Starting Update");
    mqttClient.disconnect();
    strip.ClearTo(RgbColor(0));
    strip.SetPixelColor(0, RgbColor(0, 0, 30));
    strip.SetPixelColor(1, RgbColor(0, 0, 60));
    strip.SetPixelColor(2, RgbColor(0, 0, 120));
    strip.SetPixelColor(3, RgbColor(0, 0, 60));
}

void ota_onEnd()
{
    Serial.println("Update Finished!");
    for (int i = 0; i < 10; i++) {
        strip.ClearTo(RgbColor(0, 40, 0));
        strip.Show();
        delay(50);
        strip.ClearTo(RgbColor(0));
        strip.Show();
        delay(25);
    }
}

void ota_onError(int i)
{
    Serial.println(">>>OTA Failed?"); // FIXME - decode error!
    for (int i = 0; i < 10; i++) {
        strip.ClearTo(RgbColor(40, 0, 0));
        strip.Show();
        delay(25);
        strip.ClearTo(RgbColor(0));
        strip.Show();
        delay(25);
    }
    ESP.restart();
}

void ota_onProgress(unsigned int i, unsigned int j)
{
    Serial.printf("OTA Progress: %d/%d\n", i, j);
    strip.RotateRight(1);
    strip.Show();
}

/**
 * Entered on failed wifi conn or no config
 */
void handle_config_enter(WiFiManager *myWiFiManager)
{
    // BLink leds green?
    Serial.println("Failed to connect, or no settings");
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());
}

bool shouldSaveConfig;

void handle_config_save(void)
{
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

/// FIXME - make this use nice objects instead of global chars!

int load_config()
{
    if (!SPIFFS.exists("/config.json")) {
        return -1;
    }
    //file exists, reading and loading
    Serial.println("reading config file");
    File configFile = SPIFFS.open("/config.json", "r");
    if (!configFile) {
        return -2;
    }
    size_t size = configFile.size();
    Serial.printf("opened config file of size: %d\n", size);
    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[] > buf(new char[size]);

    configFile.readBytes(buf.get(), size);
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.parseObject(buf.get());
    json.printTo(Serial);
    if (json.success()) {
        Serial.println("\nparsed json");
        if (json.containsKey("mqtt_server")) {
            // fallback
            strcpy(mqtt_host, json["mqtt_server"]);
        }
        if (json.containsKey("mqtt_host")) {
            strcpy(mqtt_host, json["mqtt_host"]);
        }
        if (json.containsKey("mqtt_port")) {
            strcpy(mqtt_port, json["mqtt_port"]);
        }
    } else {
        Serial.println("failed to load json config");
        return -3;
    }
    return 0;
}

int save_config()
{
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_host"] = mqtt_host;
    json["mqtt_port"] = mqtt_port;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
        Serial.println("failed to open config file for writing");
        return -1;
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    return 0;
}

void onMqttConnect(bool sessionPresent)
{
    Serial.printf("MQTT: connected: %d\n", sessionPresent);
    String t = "advent/" + host + "/c";
    uint16_t packetIdSub = mqttClient.subscribe(t.c_str(), 0);
    t = "advent/" + host + "/w";
    mqttClient.publish(t.c_str(), 0, false, "ON");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    // FIXME - get a useful handler abstraction for blinking leds for status
    Serial.printf("MQTT: disconn: %d\n", reason);
    ledBlinker.count = 10;
    ledBlinker.ticker = &ledDriver;
    ledDriver.attach_ms(100, tickLedBlinkerError, &ledBlinker);
    mqttClient.connect();
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
    Serial.printf("MQTT: got msg on topic: %s\n", topic);
    // TODO - decode json, either treat as raw set commands, or mode switches to saved patterns
    // or commands like "add allowing this unit to communicate me and all that good jazz
    ledBlinker.count = 10;
    ledBlinker.ticker = &ledDriver;
    ledDriver.attach_ms(150, tickLedBlinker, &ledBlinker);
}

void setup_eus()
{
    WiFi.onEvent(cbWiFiEvent);
    WiFiManager wifiManager;

    wifiManager.setDebugOutput(true);
    wifiManager.setSaveConfigCallback(handle_config_save);
    wifiManager.setAPCallback(handle_config_enter);
    wifiManager.setConfigPortalTimeout(240);

    // The extra parameters to be configured (can be either global or just in the setup)
    // After connecting, parameter.getValue() will get you the configured value
    // id/name placeholder/prompt default length
    WiFiManagerParameter custom_mqtt_server("server", "mqtt host", mqtt_host, sizeof (mqtt_host));
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, sizeof (mqtt_port));

    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);

    // Only for testing, throws out everything
    //wifiManager.resetSettings();

    // FIXME - make this use a random seed to generate and save to config?
    String key = "";
    for (int i = 0; i < 4; i++) {
        key += String(password[i], HEX);
    }
    Serial.printf("key=%s\n", key.c_str());

    //bool up = wifiManager.autoConnect();    
    bool up = wifiManager.autoConnect(host.c_str(), key.c_str());
    Serial.print("wifi auto con returned: ");
    Serial.println(up);

    if (shouldSaveConfig) {
        shouldSaveConfig = false;
        Serial.println("Reached loop, need to save config!");
        Serial.printf("MQ details: %s:%s\n", custom_mqtt_server.getValue(), custom_mqtt_port.getValue());
        strcpy(mqtt_host, custom_mqtt_server.getValue());
        strcpy(mqtt_port, custom_mqtt_port.getValue());
        save_config();
    }

    if (!up) {
        // FIXME - restarting here means we _require_ a wifi uplink before you can even try updating firmware.
        // We probably want a way of offering firmware updates even without.  Perhaps
        // via a config option manager?
        Serial.println("Failed to connect, falling back to existing code?");
        return;
    }

}

void setup_ota()
{

    httpUpdater.onStart(ota_onStart);
    httpUpdater.onEnd(ota_onEnd);
    httpUpdater.onError(ota_onError);
    httpUpdater.onProgress(ota_onProgress);

    // Use our ssid as our hostname too
    MDNS.begin(host.c_str());

    httpUpdater.setup(&httpServer, update_path, update_username, update_password);
    httpServer.begin();

    MDNS.addService("http", "tcp", 80);
    Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", host.c_str(), update_path, update_username, update_password);
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

void setup_mqtt()
{
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.setServer(mqtt_host, String(mqtt_port).toInt());
    mqttClient.setKeepAlive(60).setCleanSession(true);
    mqttClient.setClientId(host.c_str());
    String topic = "advent/" + host + "/w";
    mqttClient.setWill(topic.c_str(), 1, true, "OFF", 0);
    Serial.println("Connecting to MQTT...");
    mqttClient.connect();
}

String getContentType(String filename)
{
    if (httpServer.hasArg("download")) return "application/octet-stream";
    else if (filename.endsWith(".htm")) return "text/html";
    else if (filename.endsWith(".html")) return "text/html";
    else if (filename.endsWith(".css")) return "text/css";
    else if (filename.endsWith(".js")) return "application/javascript";
    else if (filename.endsWith(".png")) return "image/png";
    else if (filename.endsWith(".gif")) return "image/gif";
    else if (filename.endsWith(".jpg")) return "image/jpeg";
    else if (filename.endsWith(".ico")) return "image/x-icon";
    else if (filename.endsWith(".xml")) return "text/xml";
    else if (filename.endsWith(".pdf")) return "application/x-pdf";
    else if (filename.endsWith(".zip")) return "application/x-zip";
    else if (filename.endsWith(".gz")) return "application/x-gzip";
    return "text/plain";
}

bool handleFileRead(String path)
{
    Serial.println("handleFileRead: " + path);
    if (path.endsWith("/")) path += "index.htm";
    String contentType = getContentType(path);
    String pathWithGz = path + ".gz";
    if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
        if (SPIFFS.exists(pathWithGz))
            path += ".gz";
        File file = SPIFFS.open(path, "r");
        size_t sent = httpServer.streamFile(file, contentType);
        file.close();
        return true;
    }
    return false;
}

void setup_webserver(void)
{
    httpServer.onNotFound([]() {
        if (!handleFileRead(httpServer.uri()))
            httpServer.send(404, "text/plain", "FileNotFound");
    });

}

/**
 * hash the serial number a few times to get a durable, but not entirely
 * predictable key.
 */
void setup_password() {
    SHA1_CTX ctx;
    SHA1Init(&ctx);
    SHA1Update(&ctx, (uint8_t*)host.c_str(), host.length());
    SHA1Update(&ctx, (uint8_t*)host.c_str(), host.length());
    SHA1Update(&ctx, (uint8_t*)host.c_str(), host.length());
    SHA1Update(&ctx, (uint8_t*)host.c_str(), host.length());
    SHA1Final((unsigned char*)password, &ctx);
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Booting Sketch...");
    SetRandomSeed();
    if (SPIFFS.begin()) {
        Serial.println("mounted file system");
        load_config();
    } else {
        Serial.println("failed to mount FS");
    }
    yield();
    setup_password();
    strip.Begin();
    strip.Show();
    strip.SetPixelColor(0, RgbColor(0, 40, 0));
    ledDriver.attach_ms(250, tickLedBootup, 0);
    setup_dump_flashinfo();
    Serial.print("Reset reason and info: ");
    Serial.println(ESP.getResetReason());
    Serial.println(ESP.getResetInfo());

    setup_eus();
    setup_ota();
    int batt = analogRead(A0);
    Serial.printf("'Battery' adc = %d\n", batt);

    setup_webserver();

    setup_mqtt();
    ledDriver.detach();
}

void loop()
{
    httpServer.handleClient();
    if (!ledDriver.active()) {
        if (animations.IsAnimating()) {
            // the normal loop just needs these two to run the active animations
            animations.UpdateAnimations();
            strip.Show();
        } else {
            // no animation runnning, start some
            //
            FadeInFadeOutRinseRepeat(0.15f); // 0.0 = black, 0.25 is normal, 0.5 is bright
        }
    }
}

