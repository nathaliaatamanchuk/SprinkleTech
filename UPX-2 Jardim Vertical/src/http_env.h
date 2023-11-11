#include "Arduino.h"
#include "HTTPClient.h"
#include "ArduinoJson.h"

class Http_env
{
  private: 
    String payload;
    const char *host;
    const int port;
    int bufferSize;
    char *url;

    HTTPClient http;


  public:
    Http_env(const char* host, int port);

    void postHttp(char **p_dados);

    void getHttp(void);

    int parseToIntJson(const char *key);


};
