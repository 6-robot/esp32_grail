#include <TFT_eSPI.h>
#include <WiFi.h>

const char* ssid = "xxxx";
const char* password = "xxxx";

const int blue_led_pin = 48;

TFT_eSPI tft = TFT_eSPI(320,480);
WiFiServer server(80);

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(1);
  tft.fillScreen(TFT_BLACK);
	tft.setTextSize(2);
  tft.setCursor(0,0);
  tft.print("Connecting to ");
  tft.print(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
  }

  tft.setCursor(0,30);
  tft.println("WiFi connected.");
  tft.println("IP address: ");
  tft.println(WiFi.localIP());

  pinMode(blue_led_pin, OUTPUT);
  pinMode(blue_led_pin, HIGH);

  server.begin();
}

void loop() {
  WiFiClient client = server.accept();

  if (client) { 
    tft.println("New Client.");
    String currentLine = "";
    while (client.connected()) { 
      if (client.available()) {
        char c = client.read();
        if (c == '\n') {

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html; charset=utf-8");
            client.println();
            client.print("点击 <a href=\"/H\">这里</a> 熄灭蓝色LED<br>");
            client.print("点击 <a href=\"/L\">这里</a> 点亮蓝色LED<br>");

            // The HTTP response ends with another blank line:
            client.println();
            break;
          } else {  // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // Check to see if the client request was "GET /H" or "GET /L":
        if (currentLine.endsWith("GET /H")) {
          digitalWrite(blue_led_pin, HIGH);  // GET /H turns the LED on
        }
        if (currentLine.endsWith("GET /L")) {
          digitalWrite(blue_led_pin, LOW);  // GET /L turns the LED off
        }
      }
    }
    
    
    client.stop();
    tft.println("Client Disconnected.");
  }
}