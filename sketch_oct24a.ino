#include <SPI.h>
#include <MFRC522.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Servo.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

#define trigPin D1
#define echoPin D0
#define servoPin D2       // Pin untuk servo
#define redLed D8          // Pin LED merah
// Pin RFID
#define SS_PIN  D4 
#define RST_PIN D3

#define BLYNK_TEMPLATE_ID "TMPL6w1kUw4pa"
#define BLYNK_TEMPLATE_NAME "IOT Palang Pintu"

// WiFi Credentials
const char* ssid = "Ver";
const char* password = "100110101";

const char* botToken = "7147938622:AAEYdzbzd34zLQQc7lfqAoFQYBNBcuaMeYY";

const char* chat_id = "1881674219";
WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

// Google script Web_App_URL
String Web_App_URL = "https://script.google.com/macros/s/AKfycbx4aSp-cNu87onLWxw7myLbXmsnpr5Hk8VYeZ0DBgReeMKsQinhhd3C2gb6ozAmer3W/exec"; // Ganti URL ini dengan URL setelah redirect

String atc_Info = "";
String atc_Name = "";
String atc_Date = "";
String atc_Time_In = "";
String atc_Time_Out = "";

int readsuccess;
char str[32] = "";
String UID_Result = "--------";

MFRC522 mfrc522(SS_PIN, RST_PIN);
Servo gateServo;           // Objek servo
ESP8266WebServer server(80); // Membuat server di port 80
long duration;             // Durasi waktu pulse
int distance;              // Jarak dalam cm
int closedPosition = 0;    // Posisi servo saat gerbang tertutup
int openPosition = 90;     // Posisi servo saat gerbang terbuka
bool personDetected = false;  // Menandakan apakah ada orang di depan palang
bool gateOpen = false;        // Menandakan apakah gerbang terbuka

void setup() {
  client.setInsecure(); // Abaikan sertifikat SSL


  Serial.begin(9600);

  SPI.begin();      
  mfrc522.PCD_Init(); 

  // WiFi Connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  

  Serial.println("\nWiFi connected.");
  server.on("/", HTTP_GET, handleRoot); // Halaman utama
  server.on("/open_gate", HTTP_GET, openGate);
  server.on("/close_gate", HTTP_GET, closeGate);
  server.begin();
  Serial.println("Server started.");

  pinMode(redLed, OUTPUT);   // Set redLed sebagai output
  gateServo.attach(servoPin);
  gateServo.write(closedPosition); // Mulai dengan posisi gerbang tertutup
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(redLed, LOW); 

}

void loop() {
  Serial.print("ESP8266 IP Address: ");
  Serial.println(WiFi.localIP());
  
  int distance = getDistance(); // Cek jarak

  if (distance < 10 && !personDetected) { // Jika ada orang di depan sensor
    personDetected = true; // Tandai bahwa ada orang
    Serial.println("Orang terdeteksi di depan sensor, silakan scan RFID.");
  }

  if (personDetected) { // Tambahkan rfidProcessed agar tidak scan 2x
  readsuccess = getUID();
  if (readsuccess && !gateOpen) { 
    Serial.println("UID: " + UID_Result);
      
    if (http_Req(UID_Result)) {
      openGate();   
      gateOpen = true;  
    } else {
      denyAccess();
    }
  }
}


  // Jika gerbang sudah terbuka, tunggu sampai orang benar-benar masuk (jarak > 15 cm)
  if (gateOpen && getDistance() > 30) { 
    Serial.println("Orang sudah melewati gerbang, menutup gerbang...");
    delay(5000);
    closeGate(); 
    gateOpen = false;  
    personDetected = false;  
}
gateOpen = false;  
    personDetected = false;


  delay(500);
  server.handleClient();
}


bool http_Req(String str_uid) {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure();  // Abaikan sertifikat SSL

        HTTPClient http;
        String url = Web_App_URL + "?sts=atc&uid=" + str_uid;
        Serial.println("Requesting URL: " + url);

        http.begin(client, url);
        http.addHeader("Accept", "text/plain");
        http.addHeader("Accept-Charset", "UTF-8");
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

        int httpCode = http.GET();
        Serial.print("HTTP Status Code: ");
        Serial.println(httpCode);

        if (httpCode > 0) {
            String payload = http.getString();
            Serial.println("Payload received: " + payload);
            http.end();
            return processPayload(payload);  // Pastikan `processPayload` menangani respons dengan benar
        } else {
            Serial.println("Error: Failed to get a valid response.");
        }

        http.end();
    } else {
        Serial.println("Error: WiFi not connected.");
    }

    return false;  // Gagal
}


bool processPayload(String payload) {
    Serial.println("=====[ Parsing Payload ]=====");
    Serial.println("Raw Payload: " + payload);

    String sts_Res = getValue(payload, ',', 0);
    Serial.println("sts_Res: " + sts_Res);

    if (sts_Res == "OK") {
        atc_Info = getValue(payload, ',', 1);
        Serial.println("atc_Info: " + atc_Info);

        if (atc_Info == "TI_Successful" || atc_Info == "TO_Successful") {
            atc_Name = getValue(payload, ',', 2);
            atc_Date = getValue(payload, ',', 3);
            atc_Time_In = getValue(payload, ',', 4);
            atc_Time_Out = getValue(payload, ',', 5);

            Serial.println("=== Attendance Data ===");
            Serial.println("Name: " + atc_Name);
            Serial.println("Date: " + atc_Date);
            Serial.println("Time In: " + atc_Time_In);
            Serial.println("Time Out: " + atc_Time_Out);
            Serial.println("=======================");

            return true; // Akses valid
        } else if (atc_Info == "atcErr01") {
            Serial.println("Error: Card/keychain not registered.");
            return false;
        }
    } else {
        Serial.println("Error: Invalid response format.");
    }

    return false; // Akses tidak valid
}


String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

int getUID() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return 0;
  }
  if (!mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Card detected but failed to read.");
    return 0;
  }

  Serial.println("Card detected. Reading UID...");
  byteArray_to_string(mfrc522.uid.uidByte, mfrc522.uid.size, str);
  UID_Result = str;
  Serial.println("UID: " + UID_Result);

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  return 1;
  delay(1000); // Tunggu 1 detik sebelum bisa scan lagi

}

void byteArray_to_string(byte array[], unsigned int len, char buffer[]) {
  for (unsigned int i = 0; i < len; i++) {
    byte nib1 = (array[i] >> 4) & 0x0F;
    byte nib2 = (array[i] >> 0) & 0x0F;
    buffer[i * 2 + 0] = nib1 < 0xA ? '0' + nib1 : 'A' + nib1 - 0xA;
    buffer[i * 2 + 1] = nib2 < 0xA ? '0' + nib2 : 'A' + nib2 - 0xA;
  }
  buffer[len * 2] = '\0';
}


void openGate() {
  Serial.println("Access Granted. Gate opening...");
  gateServo.write(openPosition);
  digitalWrite(redLed, HIGH); // Matikan LED merah
  delay(5000); // Gerbang terbuka selama 5 detik
  gateServo.write(closedPosition);
  digitalWrite(redLed, LOW); // Nyalakan LED merah
  server.send(200, "text/html", "<html><body><h1>Gate Opened!</h1></body></html>");

}

void denyAccess() {
  Serial.println("Access Denied. Unauthorized card.");
  digitalWrite(redLed, HIGH); // LED merah menyala
  delay(1000);
  digitalWrite(redLed, LOW);
  String message = "Akses Ditolak: butuh bantuan";
  bot.sendMessage(chat_id, message, "Markdown");
}

int getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH);
  int distance = duration * 0.034 / 2; // Konversi durasi ke jarak (cm)

  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.println(" cm");

  return distance;
}

void handleRoot() {
  String html = "<html><head>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background-color: #f4f4f4; margin: 0; padding: 0; }";
  html += "h1, h3 { color: #333; text-align: center; }";
  html += "table { width: 60%; margin: 20px auto; border-collapse: collapse; }";
  html += "th, td { padding: 10px; text-align: center; border: 1px solid #ddd; }";
  html += "th { background-color: #4CAF50; color: white; }";
  html += "td { background-color: #f2f2f2; }";
  html += "p { text-align: center; font-size: 16px; }";
  html += ".btn { display: block; width: 200px; margin: 20px auto; padding: 10px; background-color: #4CAF50; color: white; text-align: center; text-decoration: none; border-radius: 5px; }";
  html += ".btn:hover { background-color: #45a049; }";
  html += "</style>";

  // Menambahkan JavaScript untuk AJAX
  html += "<script>";
  html += "function openGate() {";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('GET', '/open_gate', true);";
  html += "  xhr.onreadystatechange = function() {";
  html += "    if (xhr.readyState == 4 && xhr.status == 200) {";
  html += "      document.getElementById('gateStatus').innerHTML = 'Gate Opened!';";
  html += "    }";
  html += "  };";
  html += "  xhr.send();";
  html += "}";
  html += "function closeGate() {";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('GET', '/close_gate', true);";
  html += "  xhr.onreadystatechange = function() {";
  html += "    if (xhr.readyState == 4 && xhr.status == 200) {";
  html += "      document.getElementById('gateStatus').innerHTML = 'Gate Closed!';";
  html += "    }";
  html += "  };";
  html += "  xhr.send();";
  html += "}";
  html += "</script>";

  html += "</head><body>";
  html += "<h1>Attendance Gate Control</h1>";

  // Menampilkan data dari Google Sheets jika sudah diterima
  if (atc_Info != "") {
    html += "<h3>Attendance Information</h3>";
    html += "<table>";
    html += "<tr><th>Name</th><th>Date</th><th>Time In</th><th>Time Out</th></tr>";
    html += "<tr><td>" + atc_Name + "</td><td>" + atc_Date + "</td><td>" + atc_Time_In + "</td>";
    
    if (atc_Info == "TO_Successful") {
      html += "<td>" + atc_Time_Out + "</td>";
    } else {
      html += "<td>-</td>";
    }

    html += "</tr>";
    html += "</table>";
  } else {
    html += "<p>No attendance data available.</p>";
  }

  // Menampilkan data kartu RFID yang terdeteksi
  html += "<h3>RFID Information</h3>";
  if (UID_Result != "--------") {
    html += "<p><strong>UID: </strong>" + UID_Result + "</p>";
  } else {
    html += "<p>No RFID card detected.</p>";
  }

  // Kontrol untuk membuka dan menutup gerbang
  html += "<h3>Gate Control</h3>";
  html += "<a href='javascript:void(0)' class='btn' onclick='openGate()'>Open Gate</a>";
  html += "<a href='javascript:void(0)' class='btn' onclick='closeGate()'>Close Gate</a>";

  // Menampilkan status gerbang
  html += "<p id='gateStatus'></p>";

  html += "</body></html>";

  server.send(200, "text/html", html); // Kirim HTML sebagai respons
}


void closeGate() {
  Serial.println("Gate closed.");
  gateServo.write(closedPosition);
  server.send(200, "text/html", "<html><body><h1>Gate Closed!</h1></body></html>");
}

