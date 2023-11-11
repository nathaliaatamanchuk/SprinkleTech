
#include "http_env.h"

Http_env::Http_env(const char* host, int port) : host(host), port(port) {}

void Http_env::postHttp(char **p_dados)
{
  bufferSize = snprintf(NULL, 0, "http://%s:%d/api/dados/", host, port);
  url = (char*)malloc((bufferSize + 1) * sizeof(char));
  if(url != NULL){
    sprintf(url, "http://%s:%d/api/dados/", host, port);
    Serial.println(url);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.POST(*p_dados);
    http.end();
    free(url);
  }
  else
  {
    Serial.println("Erro ao alocar memória para a url");
    delay(1000);
    ESP.restart();
  }
}

void Http_env::getHttp(void)
{
  bufferSize = snprintf(NULL, 0, "http://%s:%d/api/dados/", host, port);
  url = (char*)malloc((bufferSize + 1) * sizeof(char));
  if(url != NULL)
  {
    sprintf(url, "http://%s:%d/api/dados/", host, port);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.GET();
    
    if(httpCode > 0)
    {
      payload = http.getString();
      Serial.println(payload);
    }
    else
    {
      Serial.println("Falha na requisição");
    }
    http.end();
    free(url);
  }
  else
  {
    Serial.println("Erro ao alocar memória para a url");
    delay(1000);
    ESP.restart();
  }
}

int Http_env::parseToIntJson(const char *key)
{
  getHttp();
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if(error)
  {
    Serial.println("Falha ao desserializar o json");
    return 0;
  }
  else
  {
    return doc[key];
  }

}