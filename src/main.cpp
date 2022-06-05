#include <Arduino.h>
#include "utils.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

#include <esp_wifi.h>   // For changing MAC-Address

#define LED               42
#define BLINK_INTERVAL    3000


const char* host = "esp32s2";
WebServer server(80);

/* Style */
String style =
"<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px}"
"input{background:#f1f1f1;border:0;padding:0 15px}body{background:#3498db;font-family:sans-serif;font-size:14px;color:#777}"
"#file-input{padding:0;border:1px solid #ddd;line-height:44px;text-align:left;display:block;cursor:pointer}"
"#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px}"
"form{background:#fff;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;text-align:center}"
".btn{background:#3498db;color:#fff;cursor:pointer}</style>";

/* Login page */
String loginIndex = 
"<form name=loginForm>"
"<h1>ESP32 Login</h1>"
"<input name=userid placeholder='User ID'> "
"<input name=pwd placeholder=Password type=Password> "
"<input type=submit onclick=check(this.form) class=btn value=Login></form>"
"<script>"
"function check(form) {"
"if(form.userid.value=='admin' && form.pwd.value=='admin')"
"{window.open('/serverIndex')}"
"else"
"{alert('Error Password or Username')}"
"}"
"</script>" + style;
 
/* Server Index Page */
String serverIndex = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
"<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
"<label id='file-input' for='file'>   Choose file...</label>"
"<input type='submit' class=btn value='Update'>"
"<br><br>"
"<div id='prg'></div>"
"<br><div id='prgbar'><div id='bar'></div></div><br></form>"
"<script>"
"function sub(obj){"
"var fileName = obj.value.split('\\\\');"
"document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
"};"
"$('form').submit(function(e){"
"e.preventDefault();"
"var form = $('#upload_form')[0];"
"var data = new FormData(form);"
"$.ajax({"
"url: '/update',"
"type: 'POST',"
"data: data,"
"contentType: false,"
"processData:false,"
"xhr: function() {"
"var xhr = new window.XMLHttpRequest();"
"xhr.upload.addEventListener('progress', function(evt) {"
"if (evt.lengthComputable) {"
"var per = evt.loaded / evt.total;"
"$('#prg').html('progress: ' + Math.round(per*100) + '%');"
"$('#bar').css('width',Math.round(per*100) + '%');"
"}"
"}, false);"
"return xhr;"
"},"
"success:function(d, s) {"
"console.log('success!') "
"},"
"error: function (a, b, c) {"
"}"
"});"
"});"
"</script>" + style;

Utils utils;

void setup()
{
  pinMode(LED, OUTPUT);
  utils.begin("TEST");
  
  while(!utils.isConnected()) delay(10);
  Serial.println("OK, Let's go");
  Serial.print("SSID: "); Serial.println(utils.getSsid());
  Serial.print("Password: "); Serial.println(utils.getPassword());

  WiFi.mode(WIFI_STA);
  const uint8_t newMACAddress[] = {0x32, 0xAE, 0xA4, 0x07, 0x0D, 0x66};
  Serial.print("[OLD] ESP32 Board MAC Address:  "); Serial.println(WiFi.macAddress());
  esp_wifi_set_mac(WIFI_IF_STA, &newMACAddress[0]);
  Serial.print("[NEW] ESP32 Board MAC Address:  "); Serial.println(WiFi.macAddress());


  // Connect to WiFi network
  WiFi.begin(utils.getSsid(), utils.getPassword());
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(utils.getSsid());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // use mdns for host name resolution
  if (!MDNS.begin(host))  // http://esp32.local
  {
    Serial.println("Error setting up MDNS responder!");
    while (1) delay(1000);
  }
  Serial.println("mDNS responder started");
  
  // return index page which is stored in serverIndex
  server.on("/", HTTP_GET, []()
  {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []()
  {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  // handling uploading firmware file
  server.on("/update", HTTP_POST, []()    
  {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []()
  {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START)
    {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN))   // Start with max available size
      {
        Update.printError(Serial);
      }
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
      upload.filename.toLowerCase();
      if(upload.filename.endsWith(".uf2"))
      {
        if(upload.currentSize < 512)
        {
          Serial.printf("Block size is smaller %d (smaller than 512)\n", upload.currentSize);
          return;
        }
        if(*(uint32_t*)&upload.buf[0] != 0x0A324655 || *(uint32_t*)&upload.buf[4] != 0x9E5D5157 || *(uint32_t*)&upload.buf[508] != 0x0AB16F30)
        {
          Serial.println("UF2 Magic Start or End Sequence not found!");
          return;
        }
        uint32_t flags = *(uint32_t*)&upload.buf[8];        // 0x00002000
        uint32_t offset = *(uint32_t*)&upload.buf[12];
        uint32_t blockSize = *(uint32_t*)&upload.buf[16];
        uint32_t blockNumber = *(uint32_t*)&upload.buf[20];
        uint32_t blockCount = *(uint32_t*)&upload.buf[24];  // 0x00002000 = 8192
        uint32_t familyId = *(uint32_t*)&upload.buf[28];    // 0xBFDD4EEE

        Serial.printf("flags=%08X, offset=%08X, blockSize=%08X, blockNumber=%08X, blockCount=%08X, familyId=%08X\n", flags, offset, blockSize, blockNumber, blockCount, familyId);

        /*if (Update.write(&upload.buf[32], blockSize) != blockSize)   // flashing firmware to ESP
        {
          Update.printError(Serial);
        }*/

        //uint32_t offset = upload.buf[]
        /*Serial.printf("upload.currentSize=%d\n", upload.currentSize);
        for(int i = 0; i < upload.currentSize; i++)
        {
          if(i % 16 == 0 && i > 0)
          {
            Serial.println();
          }
          Serial.printf("%02X ", upload.buf[i]);
        }
        Serial.println();*/
      }
      else
      {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)   // flashing firmware to ESP
        {
          Update.printError(Serial);
        }
      }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
      if (Update.end(true))   // True to set the size to the current progress
      { 
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      }
      else
      {
        Update.printError(Serial);
      }
    }
  });
  server.begin();
}

void loop()
{
  server.handleClient();
 
  static int t = 0;
  if(millis() - t > 5000)
  {
    t = millis();
    Serial.printf("Time: %d\n", t);
  }
  digitalWrite(LED, (millis() / BLINK_INTERVAL) & 1);
  delay(1);
}