/*
 * License: GNU GPL v3
 *
 * Author: Mustafa Kuru
 * Created: 2022-09-11
 *
 * Description: Web Logic Analyzer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This program is based on the work of yoursunny (2019-01-06)
 *
 */

#define BAUDRATE 115200

#define WIFI_NAME "xxxxxxxx"
#define WIFI_PASS "xxxxxxxxxxxxxxxxxxx"
#define STATUS_LED LED_BUILTIN

static const int MAX_SAMPLE = 3000;

#ifdef ESP32 //       ========================= ESP 32 =========================

#define PIN_START_NAME "GPIO22"
#define PIN_INTERRUPT_NAME "GPIO5"
#define PIN0_NAME "GPIO19"
#define PIN1_NAME "GPIO18"
#define PIN2_NAME "GPIO17"
#define PIN3_NAME "GPIO16"

static const int PIN_START = 22;
static const int PIN_INTERRUPT = 5;
static const int PIN0 = 19;
static const int PIN1 = 18;
static const int PIN2 = 17;
static const int PIN3 = 16;

#include "soc/rtc_wdt.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#define RAW_READ() REG_READ(GPIO_IN_REG)

#define WTD_ENABLE(_)     \
  {                       \
    rtc_wdt_protect_on(); \
    rtc_wdt_enable();     \
  }

#define WTD_DISABLE(_)     \
  {                        \
    rtc_wdt_protect_off(); \
    rtc_wdt_disable();     \
  }
#define WTD_FEED(_) \
  {                 \
    rtc_wdt_feed(); \
  }

static_assert(PIN_START >= 0 && PIN_START < 29 && PIN0 != 20 && PIN0 != 24 && PIN0 != 28 && PIN0 != 29, "");
static_assert(PIN_INTERRUPT >= 0 && PIN_INTERRUPT <= 39, "");
static_assert(PIN0 >= 0 && PIN0 < 29 && PIN0 != 20 && PIN0 != 24 && PIN0 != 28 && PIN0 != 29, "");
static_assert(PIN1 >= 0 && PIN1 < 29 && PIN0 != 20 && PIN0 != 24 && PIN0 != 28 && PIN0 != 29, "");
static_assert(PIN2 >= 0 && PIN2 < 29 && PIN0 != 20 && PIN0 != 24 && PIN0 != 28 && PIN0 != 29, "");
static_assert(PIN3 >= 0 && PIN3 < 29 && PIN0 != 20 && PIN0 != 24 && PIN0 != 28 && PIN0 != 29, "");
#define L_LOW LOW
#define L_HIGH HIGH
#elif defined(ESP8266) // ==================== ESP 8266 ========================

#define PIN_START_NAME "D2"
#define PIN_INTERRUPT_NAME "D3"
#define PIN0_NAME "D5"
#define PIN1_NAME "D6"
#define PIN2_NAME "D7"
#define PIN3_NAME "S3"

static const int PIN_START = 15;
static const int PIN_INTERRUPT = 0;
static const int PIN0 = 14;
static const int PIN1 = 12;
static const int PIN2 = 13;
static const int PIN3 = 9;

#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#define RAW_READ() (GPI)

#define WTD_ENABLE(_)                          \
  {                                            \
    ESP.wdtEnable(WDTO_8S);                    \
    /* Hardware WDT ON */                      \
    (*((volatile uint32_t *)0x60000900) |= 1); \
  }
#define WTD_DISABLE(_)                            \
  {                                               \
    ESP.wdtDisable();                             \
    /* Hardware WDT OFF*/                         \
    (*((volatile uint32_t *)0x60000900) &= ~(1)); \
  }
#define WTD_FEED(_) \
  {                 \
    ESP.wdtFeed();  \
  }

static_assert(PIN_START >= 0 && PIN_START < 16, "");
static_assert(PIN_INTERRUPT >= 0 && PIN_INTERRUPT < 16, "");
static_assert(PIN0 >= 0 && PIN0 < 16, "");
static_assert(PIN1 >= 0 && PIN1 < 16, "");
static_assert(PIN2 >= 0 && PIN2 < 16, "");
static_assert(PIN3 >= 0 && PIN3 < 16, "");
#define L_LOW HIGH
#define L_HIGH LOW
#endif
#include <ESPAsyncWebServer.h>

// unused pins should be tied to the ground

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static volatile bool stopListen = false;
static volatile bool start = false;
static volatile bool startRemote = false;
static unsigned long times[MAX_SAMPLE]; // when did change happen
static uint32_t values[MAX_SAMPLE];     // GPI value at time
static constexpr uint32_t MASK = (1 << PIN0) | (1 << PIN1) | (1 << PIN2) | (1 << PIN3);
static int collect_i_temp;
IRAM_ATTR void stopListening()
{
  stopListen = true;
}

const char index_html[] PROGMEM = R"=="==(
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8" />
    <meta http-equiv="X-UA-Compatible" content="IE=edge" />
    <meta
      name="viewport"
      content="width=device-width, initial-scale=1, shrink-to-fit=no"
    />
    <link
      rel="stylesheet"
      href="https://cdn.jsdelivr.net/npm/bootstrap@4.0.0/dist/css/bootstrap.min.css"
    />
    <script src="https://cdnjs.cloudflare.com/ajax/libs/hammer.js/2.0.8/hammer.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/3.9.1/chart.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/chartjs-plugin-zoom/1.2.1/chartjs-plugin-zoom.min.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/FileSaver.js/2.0.0/FileSaver.min.js"></script>

    <title>Web Logic Analyzer</title>
    <style>
      body {
        background-color: #f5f5f5;
      }
      button {
        margin: 2px 0px;
      }
    </style>
  </head>

  <body>
    <div class="container-fluid" style="height: 70vh">
      <h2 style="text-align: center">Web Logic Analyzer</h2>
      <canvas id="chart"></canvas>
      <div
        class="row align-items-center justify-content-center justify-content-md-start"
      >
        <div class="col-md-auto">
          <div>Start PIN: <span id="start">DX</span></div>
          <div>Interrupt PIN: <span id="interrupt">DX</span></div>
        </div>
        <div class="col-md-auto">
          <div>Sample Rate: 1.000.000 Hz</div>
          <div style="display: flex">
            Status: &nbsp;
            <b><div id="status">Disconnected</div></b>
          </div>
        </div>
        <div class="col-md-auto">
          <button
            type="button"
            class="btn btn-primary btn-block"
            onclick="handleStart()"
          >
            Start
          </button>
        </div>
        <div class="col-md-auto">
          <button
            type="button"
            class="btn btn-primary btn-block"
            onclick="handleReset()"
          >
            Reset Table
          </button>
        </div>
        <div class="col-md-auto">
          <button
            type="button"
            class="btn btn-primary btn-block"
            onclick="handleDownload()"
          >
            Download for PulseView (.CSV)
          </button>
        </div>

        <div class="col-md-auto">
          <button
            type="button"
            class="btn btn-primary btn-block"
            onclick="handleDownloadRaw()"
          >
            Download for PulseView (RAW)
          </button>
        </div>
        <div class="col-md-auto">
          <div class="row align-items-center" style="flex-flow: nowrap">
            <div class="col-auto">Auto download after captured:</div>
            <div class="col">
              <select
                class="custom-select"
                onchange="handleSelectAutoDownload(this.value)"
              >
                <option value="0" selected>None</option>
                <option value="csv">PulseView (.CSV)</option>
                <option value="raw">PulseView (RAW)</option>
              </select>
            </div>
          </div>
        </div>
      </div>
    </div>

    <script>
      function saveStringAsFile(text, raw = false) {
        var blob = new Blob([text], {
          type: !raw ? "text/plain;charset=utf-8" : "octet/stream",
        });
        const unixTime = Math.round(new Date().getTime() / 1000);
        saveAs(blob, `pulseView_${unixTime}.${raw ? "raw" : "csv"}`);
      }
      function handleStart() {
        websocket.send("S");
        document.getElementById("status").innerHTML = "Capturing...";
      }
      function handleDownload() {
        let data = `${chart.data.datasets[0].label},${chart.data.datasets[1].label},${chart.data.datasets[2].label},${chart.data.datasets[3].label}\n`;
        let time = 0;
        for (const [t, [p1, p2, p3, p4]] of dataTemp) {
          if (!time) {
            data += `${p1},${p2},${p3},${p4}\n`;
            time = t;
          } else {
            while (time < t) {
              data += `${p1},${p2},${p3},${p4}\n`;
              time += 1;
            }
          }
        }
        saveStringAsFile(data);
      }
      let autoDownload = 0;
      function handleSelectAutoDownload(value) {
        autoDownload = value;
      }
      function handleDownloadRaw() {
        let data = "";
        let time = 0;
        for (const [t, value] of dataTempRaw) {
          if (!time) {
            time = t;
            data += String.fromCharCode(value);
          } else {
            while (time < t) {
              data += String.fromCharCode(value);
              time += 1;
            }
          }
        }
        saveStringAsFile(data, true);
      }
      function handleReset() {
        window.chart.data.labels = [];
        window.chart.data.datasets[0].data = [];
        window.chart.data.datasets[1].data = [];
        window.chart.data.datasets[2].data = [];
        window.chart.data.datasets[3].data = [];
        chart.update();
      }
      const decimation = {
        enabled: false,
        algorithm: "min-max",
      };

      const ctx = document.getElementById("chart").getContext("2d");
      const chart = new Chart(ctx, {
        type: "line",
        data: {
          labels: [] || fakeLabels,
          datasets: [
            {
              label: "PIN 1",
              data: [],
              borderColor: "red",
              backgroundColor: "red",
              stepped: true,
              yAxisID: "y4",
            },
            {
              label: "PIN 2",
              data: [],
              borderColor: "blue",
              backgroundColor: "blue",
              stepped: true,
              yAxisID: "y3",
            },
            {
              label: "PIN 3",
              data: [],
              borderColor: "purple",
              backgroundColor: "purple",
              stepped: true,
              yAxisID: "y2",
            },
            {
              label: "PIN 4",
              data: [],
              borderColor: "green",
              backgroundColor: "green",
              stepped: true,
              yAxisID: "y",
            },
          ],
        },
        options: {
          maintainAspectRatio: false,
          animation: {
            duration: 10,
          },
          hover: {
            animationDuration: 10,
          },
          responsiveAnimationDuration: 10,
          type: "line",
          responsive: true,
          interaction: {
            intersect: true,
            mode: "x",
            axis: "x",
          },
          plugins: {
            decimation: decimation,
            zoom: {
              zoom: {
                wheel: {
                  enabled: true,
                },
                pinch: {
                  enabled: true,
                },
                mode: "x",
              },
              pan: {
                enabled: true,
                mode: "x",
              },
            },
          },
          scales: {
            x: {
              type: "linear",
              beginAtZero: true,
              offset: true,
              min: 0,
            },
            y: {
              type: "category",
              labels: ["ON", "OFF"],
              offset: true,
              position: "left",
              stack: "demo",
              stackWeight: 1,
              grid: {
                borderColor: "green",
              },
            },
            y2: {
              type: "category",
              labels: ["ON", "OFF"],
              offset: true,
              position: "left",
              stack: "demo",
              stackWeight: 1,
              grid: {
                borderColor: "purple",
              },
            },
            y3: {
              type: "category",
              labels: ["ON", "OFF"],
              offset: true,
              position: "left",
              stack: "demo",
              stackWeight: 1,
              grid: {
                borderColor: "blue",
              },
            },
            y4: {
              type: "category",
              labels: ["ON", "OFF"],
              offset: true,
              position: "left",
              stack: "demo",
              stackWeight: 1,
              grid: {
                borderColor: "red",
              },
            },
          },
        },
      });

      window.chart = chart;

      var gateway = `ws://${window.location.hostname}/ws`;
      if (
        window.location.hostname === "localhost" ||
        window.location.hostname === ""
      ) {
        gateway = `ws://192.168.1.160/ws`;
      }
      var websocket;

      function addData(time, [pin1, pin2, pin3, pin4]) {
        window.chart.data.labels.push(time);
        window.chart.data.datasets[0].data.push({ y: pin1, x: time });
        window.chart.data.datasets[1].data.push({ y: pin2, x: time });
        window.chart.data.datasets[2].data.push({ y: pin3, x: time });
        window.chart.data.datasets[3].data.push({ y: pin4, x: time });
      }

      function initWebSocket() {
        console.log("Trying to open a WebSocket connection...");

        document.getElementById("status").innerHTML = "Reconnecting...";
        websocket = new WebSocket(gateway);
        websocket.onopen = onOpen;
        websocket.onclose = onClose;
        websocket.onmessage = onMessage; // <-- add this line
      }
      function onOpen(event) {
        console.log("Connection opened");

        document.getElementById("status").innerHTML = "Connected";
      }
      function onClose(event) {
        console.log("Connection closed");

        document.getElementById("status").innerHTML = "Closed";
        setTimeout(initWebSocket, 2000);
      }
      let dataTemp = [];
      let dataTempRaw = [];
      function onMessage(event) {
        if (event.data == "S" || event.data == "S-I") {
          previousSignalData = 0;
          currentTime = 0;
          dataTemp = [];
          dataTempRaw = [];
          document.getElementById("status").innerHTML = `Receiving data ${
            event.data === "S-I" ? " (Interrupt)" : ""
          }...`;
          handleReset();
        } else if (event.data == "F") {
          window.chart.resetZoom();
          window.chart.update();
          document.getElementById("status").innerHTML = "Capture Done!";

          if (autoDownload === "csv") {
            handleDownload();
          } else if (autoDownload === "raw") {
            handleDownloadRaw();
          }

          setTimeout(() => {
            document.getElementById("status").innerHTML = "Connected";
          }, 2000);
        } else if (event.data.startsWith("H")) {
          const payload = event.data.split("|")[1];
          const [pin1, pin2, pin3, pin4, interrupt, start] = payload.split(":");
          window.chart.data.datasets[0].label = `PIN 1 (${pin1})`;
          window.chart.data.datasets[1].label = `PIN 2 (${pin2})`;
          window.chart.data.datasets[2].label = `PIN 3 (${pin3})`;
          window.chart.data.datasets[3].label = `PIN 4 (${pin4})`;
          document.getElementById("interrupt").innerHTML = interrupt;
          document.getElementById("start").innerHTML = start;
          window.chart.update();
        } else {
          event.data.split("|").forEach((record) => {
            handleSignalData(record);
          });
        }
      }
      let previousSignalData = null;
      let currentTime = null;
      function handleSignalData(data) {
        const parts = data.split(":");
        let signal = parseInt(parts[0]);
        const micros = parseInt(parts[1]);
        if (previousSignalData === null) {
          previousSignalData = signal;
        } else {
          signal = signal - previousSignalData;
        }
        const pin1 = !!(signal & 0x01) ? 1 : 0;
        const pin2 = !!(signal & 0x02) ? 1 : 0;
        const pin3 = !!(signal & 0x04) ? 1 : 0;
        const pin4 = !!(signal & 0x08) ? 1 : 0;
        currentTime += micros;
        dataTemp.push([currentTime, [pin1, pin2, pin3, pin4]]);
        dataTempRaw.push([currentTime, signal]);
        addData(currentTime, [
          pin1 ? "ON" : "OFF",
          pin2 ? "ON" : "OFF",
          pin3 ? "ON" : "OFF",
          pin4 ? "ON" : "OFF",
        ]);
      }
      function onLoad(event) {
        initWebSocket();
      }
      window.addEventListener("load", onLoad);
    </script>
  </body>
</html>
)=="==";

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    if (strcmp((char *)data, "S") == 0)
    {
      startRemote = true;
    }
    Serial.print("Received --> ");
    Serial.println((char *)data);
  }
}
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    char msg[128];
    sprintf(msg, "H|%s:%s:%s:%s:%s:%s", PIN0_NAME, PIN1_NAME, PIN2_NAME, PIN3_NAME, PIN_INTERRUPT_NAME, PIN_START_NAME);
    ws.text(client->id(), msg);
    break;
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}
void initWebSocket()
{
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

extern void IRAM_ATTR collect()
{
  attachInterrupt(digitalPinToInterrupt(PIN_INTERRUPT), stopListening, CHANGE);
  times[0] = micros();
  values[0] = RAW_READ() & MASK;
  stopListen = false;
  collect_i_temp = MAX_SAMPLE;
  for (int i = 1; i < MAX_SAMPLE; ++i)
  {
    uint32_t value;
    do
    {
      value = RAW_READ() & MASK;
      yield();
    } while (value == values[i - 1] && !stopListen);
    times[i] = micros();
    values[i] = value;
    if (stopListen)
    {
      collect_i_temp = i + 1;
      break;
    }
  }
  detachInterrupt(digitalPinToInterrupt(PIN_INTERRUPT));
}

int compactValue(uint32_t value)
{
  int res = 0;
  if ((value & (1 << PIN0)) != 0)
  {
    res |= (1 << 0);
  }
  if ((value & (1 << PIN1)) != 0)
  {
    res |= (1 << 1);
  }
  if ((value & (1 << PIN2)) != 0)
  {
    res |= (1 << 2);
  }
  if ((value & (1 << PIN3)) != 0)
  {
    res |= (1 << 3);
  }
  return res;
}

void report()
{
  unsigned long elapsedTimeDueCapture = (micros() - times[0]);
  Serial.printf("Total capture seconds: %lu\n", elapsedTimeDueCapture / 1000000);

  bool isNeedWaitForWsReconnect = elapsedTimeDueCapture > 10 * 1000 * 1000;
  if (isNeedWaitForWsReconnect)
  {
    digitalWrite(STATUS_LED, L_HIGH);
    ws.closeAll();
    for (int i = 0; i < 50; i++)
    {
      ws.cleanupClients();
      if (i % 5 == 0)
      {
        digitalWrite(STATUS_LED, L_LOW);
      }
      else if ((i - 1) % 5 == 0)
      {
        digitalWrite(STATUS_LED, L_HIGH);
      }
      delay(100);
    }
  }
  if (stopListen)
  {
    Serial.println("S-I");
    ws.textAll("S-I");
  }
  else
  {
    Serial.println("S");
    ws.textAll("S");
  }

  char msg0[32];
  sprintf(msg0, "%d:%d", compactValue(values[0]), collect_i_temp - 1);

  ws.textAll(msg0);
  unsigned long previousTime = 0;

  int chunk = 0;
  String wsMsg = "";
  Serial.printf("Total sample: %d", collect_i_temp);
  for (int i = 1; i < collect_i_temp; ++i)
  {
    char msg[32];
    int value = compactValue(values[i]);
    unsigned long diff = times[i] - times[0] - previousTime;
    sprintf(msg, "%i:%lu", value, diff);

    while (!ws.availableForWriteAll())
    {
      WTD_FEED();
      delay(1);
    }
    wsMsg += msg;
    if (chunk > 30 || (chunk > 0 && i == collect_i_temp - 1))
    {
      ws.textAll(wsMsg);
      Serial.println(wsMsg);
      wsMsg = "";
      chunk = 0;
    }
    else
    {
      wsMsg += "|";
    }

    previousTime += diff;
    chunk++;
  }
  for (int i = 0; i < 3; i++)
  {
    digitalWrite(STATUS_LED, L_HIGH);
    delay(100);
    digitalWrite(STATUS_LED, L_LOW);
    delay(200);
  }
  Serial.println("F");
  ws.textAll("F");
}

void setup()
{
#ifdef ESP32
  setCpuFrequencyMhz(240);
#endif
  Serial.begin(BAUDRATE);
  delay(1500);

  pinMode(STATUS_LED, OUTPUT);
  pinMode(PIN_START, INPUT);
  pinMode(PIN0, INPUT_PULLUP);
  pinMode(PIN1, INPUT_PULLUP);
  pinMode(PIN2, INPUT_PULLUP);
  pinMode(PIN3, INPUT_PULLUP);

  for (int i = 0; i < 2; i++)
  {
    digitalWrite(STATUS_LED, L_HIGH);
    delay(300);
    digitalWrite(STATUS_LED, L_LOW);
    delay(300);
  }

  WiFi.begin(WIFI_NAME, WIFI_PASS);
  Serial.print("Connecting to WiFi..");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.print("\nLocal IP: ");
  Serial.println(WiFi.localIP());

  for (int i = 0; i < 2; i++)
  {
    digitalWrite(STATUS_LED, L_HIGH);
    delay(100);
    digitalWrite(STATUS_LED, L_LOW);
    delay(100);
  }

  initWebSocket();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send_P(200, "text/html", index_html); });

  server.begin();
}
void loop()
{

  ws.cleanupClients();
  while (!start && !startRemote)
  {
    ws.cleanupClients();
    start = digitalRead(PIN_START);
    delay(1);
  }
  Serial.printf("Logic analyzer started. Button start: %d, Web Start: %d\n", start, startRemote);
  start = false;
  startRemote = false;

  digitalWrite(STATUS_LED, L_HIGH);
  WTD_DISABLE();
  collect();
  WTD_ENABLE();
  digitalWrite(STATUS_LED, L_LOW);
  report();
}