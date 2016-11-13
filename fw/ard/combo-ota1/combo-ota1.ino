// Karl Palsson,
// Nov 2016
// Gross basic start at OTA+leds.
// missing: AP end user setup!
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

const uint16_t PixelCount = 7;
const uint8_t PixelPin = 2;  // make sure to set this to the correct pin, ignored for Esp8266
const uint8_t AnimationChannels = 1; // we only need one as all the pixels are animated at once

// This is the pin we have wired up. (works with both nodemcu+arduino codebases)
NeoPixelBus<NeoGrbFeature, NeoEsp8266Uart800KbpsMethod> strip(PixelCount, PixelPin);

NeoPixelAnimator animations(AnimationChannels); // NeoPixel animation management object

uint16_t effectState = 0;  // general purpose variable used to store effect state
const char* ssid = "....";
const char *password = "....";
const char *host = "advent";
const char* update_path = "/firmware";
const char* update_username = "....";
const char* update_password = "....";

ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;


// what is stored for state is specific to the need, in this case, the colors.
// basically what ever you need inside the animation update function
struct MyAnimationState
{
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

  for (int shifts = 3; shifts < 31; shifts += 3)
  {
    seed ^= analogRead(0) << shifts;
    delay(1);
  }

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
  for (uint16_t pixel = 0; pixel < PixelCount; pixel++)
  {
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
  if (effectState == 0)
  {
    // Fade upto a random color
    // we use HslColor object as it allows us to easily pick a hue
    // with the same saturation and luminance so the colors picked
    // will have similiar overall brightness
    RgbColor target = HslColor(random(360) / 360.0f, 1.0f, luminance);
    uint16_t time = random(800, 2000);

    animationState[0].StartingColor = strip.GetPixelColor(0);
    animationState[0].EndingColor = target;

    animations.StartAnimation(0, time, BlendAnimUpdate);
  }
  else if (effectState == 1)
  {
    // fade to black
    uint16_t time = random(600, 700);

    animationState[0].StartingColor = strip.GetPixelColor(0);
    animationState[0].EndingColor = RgbColor(0);

    animations.StartAnimation(0, time, BlendAnimUpdate);
  }
  else if (effectState == 2)
  {
    Serial.println("effect 2");
    // Pick a random colour, and a few versions of it...
    RgbColor col = HslColor(random(360) / 360.0f, 1.0f, luminance);
    strip.ClearTo(RgbColor(0));
    strip.SetPixelColor(0, col);

    animations.StartAnimation(0, 2000, LoopAnimUpdate);
  }

  // toggle to the next effect state
  effectState = (effectState + 1) % 3;
}

void WiFiEvent(WiFiEvent_t event) {
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
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
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

void setup_ota()
{

  // delete old config
  WiFi.disconnect(true);

  delay(100);

  WiFi.onEvent(WiFiEvent);

  // quick scan on boot to help check what's going on....
  wifi_scan();
  delay(100);
  // delete old config
  WiFi.disconnect(true);

  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    Serial.println("WiFi failed, retrying.");
  }

  MDNS.begin(host);

  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and password '%s'\n", host, update_path, update_username, update_password);
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());


}

void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println("Booting Sketch...");
  setup_dump_flashinfo();
  strip.Begin();
  strip.Show();

  SetRandomSeed();
  setup_ota();
}

void loop()
{
  httpServer.handleClient();
  if (animations.IsAnimating())
  {
    // the normal loop just needs these two to run the active animations
    animations.UpdateAnimations();
    strip.Show();
  }
  else
  {
    // no animation runnning, start some
    //
    FadeInFadeOutRinseRepeat(0.15f); // 0.0 = black, 0.25 is normal, 0.5 is bright
  }
}



