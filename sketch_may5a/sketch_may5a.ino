#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

// --- Wi-Fi Credentials ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- Pin Definitions ---
const int trigPin = 5;
const int echoPin = 18;
const int servoPin = 19;
const int buttonPin = 4;

// --- Component Initialization ---
WebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C address 0x27 is common, change to 0x3F if it doesn't work
Servo feederServo;

// --- Variables ---
long duration;
int distance;
int foodPercentage = 0;
const int containerDepth = 20; // Depth of empty container in cm (Adjust to your tank)

void setup() {
  Serial.begin(115200);

  // Initialize Pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  // Initialize Servo
  feederServo.attach(servoPin);
  feederServo.write(0); // Initial position

  // Initialize LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Connecting Wi-Fi");

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  // Display IP on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP:");
  lcd.print(WiFi.localIP());

  // Define Web Server Routes
  server.on("/", handleRoot);
  server.on("/feed", handleFeed);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient(); // Listen for web requests
  
  // 1. Get Ultrasonic Distance & Calculate %
  measureFoodLevel();

  // 2. Update LCD (Level % & IP)
  updateLCD();

  // 3. Check for Manual Button Press
  if (digitalRead(buttonPin) == LOW) {
    triggerFeeding();
    delay(500); // Debounce delay
  }
  
  delay(100); // Small loop delay for stability
}

// --- Helper Functions ---

void measureFoodLevel() {
  // Clear the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  // Trigger sensor
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Read echo duration
  duration = pulseIn(echoPin, HIGH);
  
  // Calculate distance in cm
  distance = duration * 0.034 / 2;
  
  // Calculate Percentage (Assuming 0cm is 100% full, and containerDepth is 0% empty)
  foodPercentage = map(distance, 0, containerDepth, 100, 0);
  
  // Constrain between 0 and 100
  if(foodPercentage > 100) foodPercentage = 100;
  if(foodPercentage < 0) foodPercentage = 0;
}

void updateLCD() {
  lcd.setCursor(0, 1);
  lcd.print("Food Level: ");
  lcd.print(foodPercentage);
  lcd.print("%   "); // Extra spaces to clear old digits
}

void triggerFeeding() {
  Serial.println("Feeding Triggered!");
  lcd.setCursor(0, 1);
  lcd.print("Feeding Now...  ");
  
  // Activate Servo: Rotate 90 deg, Wait 1s, Return 0 deg
  feederServo.write(90);
  delay(1000);
  feederServo.write(0);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP:");
  lcd.print(WiFi.localIP());
}

// --- Web Server Functions ---

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><title>Smart Fish Feeder</title>";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<style>body { font-family: Arial; text-align: center; margin-top: 50px; }";
  html += ".btn { background-color: #4CAF50; color: white; padding: 15px 32px; text-decoration: none; font-size: 20px; border-radius: 10px; border: none; cursor: pointer; }</style></head>";
  html += "<body><h1>Smart Fish Feeder</h1>";
  html += "<h2>Food Level: " + String(foodPercentage) + "%</h2>";
  html += "<form action=\"/feed\" method=\"POST\"><button class=\"btn\" type=\"submit\">Feed Now</button></form>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleFeed() {
  triggerFeeding(); // Trigger the motor logic
  
  // Redirect back to the main page after feeding
  server.sendHeader("Location", "/");
  server.send(303);
}