#include "esp_log.h"

#include "logging.h"

#include <FS.h>
#include <LittleFS.h> 
#include <SPIFFS.h> 

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define HTML_FOLDER "/html/"

AsyncWebServer server(80);	
AsyncWebSocket ws("/ws");

const char* TAG = "HTTP Server";
const char* PARAM_MESSAGE = "message";



class AddAnyHeader: public AsyncWebHandler {
  public:
    virtual ~AddAnyHeader(){}
    virtual bool canHandle(AsyncWebServerRequest *request) override {
      request->addInterestingHeader("ANY");
      return false;
    }
    virtual void handleRequest(AsyncWebServerRequest *request __attribute__((unused))){}
    virtual void handleUpload(AsyncWebServerRequest *request  __attribute__((unused)), const String& filename __attribute__((unused)), size_t index __attribute__((unused)), uint8_t *data __attribute__((unused)), size_t len __attribute__((unused)), bool final  __attribute__((unused))){}
    virtual void handleBody(AsyncWebServerRequest *request __attribute__((unused)), uint8_t *data __attribute__((unused)), size_t len __attribute__((unused)), size_t index __attribute__((unused)), size_t total __attribute__((unused))){}
    virtual bool isRequestHandlerTrivial(){return true;}
};
AddAnyHeader any;

void notFound(AsyncWebServerRequest *request) {
    auto uri = request->url() == "/" ? "/index.html" : request->url();
    if (LittleFS.exists(HTML_FOLDER + uri)) {
        request->send(LittleFS, HTML_FOLDER + uri);
    } else {
        request->send(404, "text/plain", "Not found " + uri);
    }
}
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
// handling code
}

esp_err_t http_server_init(void) {
    LittleFS.begin(true, "/littlefs", 10, "factory_data");
    // Prevent crash in remove interesting header function
    server.addHandler(&any);
    // Send a GET request to <IP>/get?message=<message>
    server.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
        String message;
        if (request->hasParam(PARAM_MESSAGE)) {
            message = request->getParam(PARAM_MESSAGE)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, GET: " + message);
    });
    // Send a GET request to <IP>/get?message=<message>
    server.on("/log", HTTP_GET, [] (AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("text/html");
        auto entries = logging_get_all_entries();
        response->print("<pre>");
        for(auto &i : entries) {
            response->print(i.second.c_str());
        }
        response->print("</pre>");
        request->send(response);
    });

    server.on("/tasks", HTTP_GET, [] (AsyncWebServerRequest *request) {
        char msg[1024];
        vTaskGetRunTimeStats(msg);
        request->send(200, "text/plain", msg);
    });

    // Send a POST request to <IP>/post with a form field message set to <message>
    server.on("/post", HTTP_POST, [](AsyncWebServerRequest *request){
        String message;
        if (request->hasParam(PARAM_MESSAGE, true)) {
            message = request->getParam(PARAM_MESSAGE, true)->value();
        } else {
            message = "No message sent";
        }
        request->send(200, "text/plain", "Hello, POST: " + message);
    });
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.onNotFound(notFound);
    server.begin();
    ESP_LOGI(TAG, "HTTP Server initialized");
    return ESP_OK;
}