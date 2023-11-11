#include <Arduino.h>
#include <PubSubClient.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "WiFi.h"
#include "NTPClient.h"
#include "ESPAsyncWebServer.h"
#include "AsyncTCP.h"
#include "SPIFFS.h"
#include "RTClib.h"
#include "http_env.h"

#define SENSOR 35
#define RELE 4
#define PIN_BUTTON 39
#define LED_WIFI 2
#define LED_AP 16
#define SOLO 34

volatile byte pulseCount;
volatile bool config = false;
volatile int modoFuncionamento = 1;

const char *PARAM_INPUT_1 = "ssid";
const char *PARAM_INPUT_2 = "pass";

const char *ssidPath = "/ssid.txt";
const char *passPath = "/pass.txt";

struct Data
{
  unsigned int dSemana;
  unsigned int dia;
  unsigned int mes;
  unsigned int ano;
  unsigned int hora;
  unsigned int minuto;
  unsigned int segundo;
};

String ssid;
String pass;

Adafruit_BME280 bme;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "0.br.pool.ntp.org", -2 * 3600, 60000);
AsyncWebServer server(80);
RTC_DS3231 rtc;
Http_env http("34.234.92.215", 8014);

void inicioSPIFFS(void);
void escritaArquivo(fs::FS &fs, const char *path, const char *message);
void configAP(void);
void setupNTP(void);
void setupRTC(void);
void acionaSolenoide(bool *p_solenoide, float *p_umid_solo, byte *p_segundos, byte *p_minutos, byte *p_horas);
void acionaSolenoideRTC(bool *p_solenoide, float *p_umid_solo, byte *p_segundos, byte *p_minutos, byte *p_horas);
void displayInfo(float *p_temp, float *p_umid, float *p_press, unsigned long *p_totalMilliLitres, float *p_flowRate, float *p_umid_solo, bool *p_solenoide, byte *p_hora, byte *p_minuto, byte *p_segundo, char **p_dados);
void liquidQty(byte *p_pulse1Sec, unsigned int *p_flowMilliLitres, unsigned long *p_totalMilliLitres, float *p_flowRate, unsigned long *p_previousMillis);
void pegaHorarioRTC(byte *p_segundos, byte *p_minutos, byte *p_horas);
void pegaValores(float *p_temp, float *p_umid, float *p_press, float *p_umid_solo);
void IRAM_ATTR pulseCounter(void);
bool inicioWiFi(unsigned long *p_previousMillis, const long *p_interval);
String leituraArquivo(fs::FS &fs, const char *path);
Data getData(void);

void setup(void)
{
  unsigned long previousMillis = 0;
  const long interval = 10000;

  Serial.begin(115200);
  Serial.println("\nInicio do programa!");

  pinMode(SENSOR, INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT_PULLDOWN);
  pinMode(RELE, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_AP, OUTPUT);

  

  inicioSPIFFS();

  ssid = leituraArquivo(SPIFFS, ssidPath);
  pass = leituraArquivo(SPIFFS, passPath);

  Serial.println("SSID: " + ssid);
  Serial.println("PASS: " + pass);

  if (inicioWiFi(&previousMillis, &interval) == false)
  {
    configAP();
  }
  else
  {
    digitalWrite(LED_WIFI, HIGH);
    digitalWrite(LED_AP, LOW);
  }

  if (!bme.begin())
  {
    Serial.println("Não foi possivel achar o sensor BME280, olhe as conexões, Endereço, ID do sensor!");
    Serial.print("Endereço é: 0x");
    Serial.println(bme.sensorID(), 16);
    Serial.print("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n");
    Serial.print("   ID of 0x56-0x58 represents a BMP 280,\n");
    Serial.print("        ID of 0x60 represents a BME 280.\n");
    Serial.print("        ID of 0x61 represents a BME 680.\n");
    while (1)
      delay(10);
  }

  if (!rtc.begin())
  {
    Serial.println("Não foi possivel achar o sensor RTC, olhe os fios, endereço, ID do sensor!");
    while (1)
      ;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    setupNTP();
    setupRTC();
    attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);
  }
}

void loop(void)
{
  float temperatura = 0, umidade = 0, pressao = 0, calibrationFactor = 4.5, flowRate = 0, umidade_solo = 0;
  long currentMillis = 0, previousMillis2 = 0, currentMillis2 = 0, previousMillis3 = 0;
  unsigned long previousMillis = 0;
  int interval = 1000, qnt = 0;
  byte pulse1Sec = 0;
  unsigned int flowMilliLitres = 0;
  unsigned long totalMilliLitres = 0;
  bool solenoide = false;
  byte segundos = 0, minutos = 0, horas = 0;
  char *p_dados;

  while (1)
  {

    pegaValores(&temperatura, &umidade, &pressao, &umidade_solo);
    currentMillis = millis();
    digitalWrite(RELE, HIGH);

    switch (config)
    {
    case false:
      while (WiFi.status() == WL_CONNECTED)
      {
        pegaValores(&temperatura, &umidade, &pressao, &umidade_solo);
        pegaHorarioRTC(&segundos, &minutos, &horas);
        currentMillis = millis();
        if (currentMillis - previousMillis > interval)
        {
          liquidQty(&pulse1Sec, &flowMilliLitres, &totalMilliLitres, &flowRate, &previousMillis);
          acionaSolenoide(&solenoide, &umidade_solo, &segundos, &minutos, &horas);

          currentMillis2 = millis();
          if (flowRate == 0 && currentMillis2 - previousMillis3 > 100000)
          {
            totalMilliLitres = 0;
            previousMillis3 = currentMillis2;
          }
        }

        if (currentMillis - previousMillis2 > 60000)
        {
          previousMillis2 = millis();
          displayInfo(&temperatura, &umidade, &pressao, &totalMilliLitres, &flowRate, &umidade_solo, &solenoide, &horas, &minutos, &segundos, &p_dados);
          http.postHttp(&p_dados);
          
          free(p_dados);
        }
      }
      ESP.restart();

    case true:
      currentMillis = millis();
      if (currentMillis - previousMillis2 > 1000)
      {
        previousMillis2 = currentMillis;
        pegaHorarioRTC(&segundos, &minutos, &horas);
        pegaValores(&temperatura, &umidade, &pressao, &umidade_solo);
        acionaSolenoide(&solenoide, &umidade_solo, &segundos, &minutos, &horas);
        Serial.printf("%d:%d:%d\n", horas, minutos, segundos);
      }
      if (solenoide == false)
      {
        if (currentMillis - previousMillis > 120000)
        {
          previousMillis = currentMillis;
          digitalWrite(LED_AP, LOW);
          digitalWrite(LED_WIFI, LOW);
          ESP.restart();
        }
      }
    };
  }
}

void inicioSPIFFS(void)
{
  if (!SPIFFS.begin(true))
  {
    Serial.println("Ocorreu um erro ao montar o SPIFFS");
  }
  Serial.println("SPIFFS montado com sucesso!");
}

String leituraArquivo(fs::FS &fs, const char *path)
{
  Serial.printf("Lendo arquivo: %s\r\n", path);
  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- Falha ao abrir arquivo para leitura");
    digitalWrite(LED_WIFI, HIGH);
    digitalWrite(LED_AP, HIGH);
    delay(1000);
    digitalWrite(LED_WIFI, LOW);
    digitalWrite(LED_AP, LOW);
    delay(1000);
    return String();
  }

  String fileContent;
  while (file.available())
  {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}

void escritaArquivo(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Escrevendo arquivo: %s\r\n", path);
  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("- Falha ao abrir arquivo para escrita");
    return;
  }
  if (file.print(message))
  {
    Serial.println("- Arquivo escrito com sucesso");
  }
  else
  {
    Serial.println("- Falha ao escrever no arquivo");
  }
}

bool inicioWiFi(unsigned long *p_previousMillis, const long *p_interval)
{
  bool bPress = digitalRead(PIN_BUTTON), modoConfig = true;
  unsigned long tempoAnt = 0;
  while (modoConfig == true)
  {
    unsigned long tempoAtual = millis();
    if (bPress == true && tempoAtual - tempoAnt >= 3000)
    {
      tempoAnt = tempoAtual;
      Serial.println("Entrando no mode de configuração do WIFI");
      return false;
    }
    else if (bPress == false && tempoAtual - tempoAnt >= 4000)
    {
      Serial.println("Modo de configuração de WIFI não ativado");
      modoConfig = false;
      break;
    }
  }
  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid.c_str(), pass.c_str());
  Serial.println("Conectando ao WiFi");

  unsigned long currentMillis = millis();
  *p_previousMillis = currentMillis;

  while (WiFi.status() != WL_CONNECTED)
  {
    currentMillis = millis();
    if (currentMillis - *p_previousMillis >= *p_interval)
    {
      Serial.println("Falha na conexão");
      return false;
    }
  }

  Serial.println("Conectado com sucesso");
  Serial.println("Conectado no WiFi: " + ssid + " | No IP: " + WiFi.localIP().toString());
  return true;
}

void configAP(void)
{
  config = true;
  digitalWrite(LED_AP, HIGH);
  digitalWrite(LED_WIFI, LOW);
  Serial.println("Configurando o ponto de acesso");
  WiFi.softAP("JardimVertical", "12345678");

  IPAddress IP = WiFi.softAPIP();
  Serial.println("Endereço de IP do ponto de acesso: " + IP.toString());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", "text/html"); });

  server.serveStatic("/", SPIFFS, "/");

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    int params = request->params();
    for(int i=0; i<params; i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost())
      {
        if(p->name() == PARAM_INPUT_1)
        {
          ssid = p->value();
          Serial.print("SSID: ");
          Serial.println(ssid);
          escritaArquivo(SPIFFS, ssidPath, ssid.c_str());
        }
        if(p->name() == PARAM_INPUT_2)
        {
          pass = p->value().c_str();
          Serial.print("PASS: ");
          Serial.println(pass);
          escritaArquivo(SPIFFS, passPath, pass.c_str());
        }

      }
    }
    request->send(200, "text/plain", "Pronto. O ESP ira reiniciar em 3 segundos");
    delay(3000);
    ESP.restart(); });

  server.begin();
}

void setupNTP(void)
{
  timeClient.begin();

  Serial.print("Aguardando a primeira atualização de horario...\n");
  while (!timeClient.update())
  {
    Serial.print(".");
    timeClient.forceUpdate();
    delay(500);
  }
  Serial.print("\nDados Recebidos!");
  timeClient.setTimeOffset(-10800);
}

void setupRTC(void)
{
  Data data = getData();
  rtc.adjust(DateTime(data.ano, data.mes, data.dia, data.hora, data.minuto, data.segundo));
  delay(100);
}

void pegaValores(float *p_temp, float *p_umid, float *p_press, float *p_umid_solo)
{
  *p_temp = bme.readTemperature();
  *p_umid = bme.readHumidity();
  *p_press = bme.readPressure() / 100.0;
  *p_umid_solo = map(analogRead(SOLO), 0, 4096, 100, 0);
}

void acionaSolenoide(bool *p_solenoide, float *p_umid_solo, byte *p_segundos, byte *p_minutos, byte *p_horas)
{
  Data data = getData();
  if (WiFi.status() == WL_CONNECTED)
  {
    if (data.minuto == 0 || data.minuto == 10 || data.minuto == 20 || data.minuto == 30 || data.minuto == 40 || data.minuto == 50 || *p_umid_solo < 30)
    {
      digitalWrite(RELE, HIGH);
      *p_solenoide = true;
    }
    else
    {
      digitalWrite(RELE, LOW);
      *p_solenoide = false;
    }
  }
  else
  {
    if (*p_minutos == 0 || *p_minutos == 10 || *p_minutos == 20 || *p_minutos == 30 || *p_minutos == 40 || *p_minutos == 50 || *p_umid_solo < 30)
    {
      digitalWrite(RELE, HIGH);
      *p_solenoide = true;
    }
    else
    {
      digitalWrite(RELE, LOW);
      *p_solenoide = false;
    }
  }
}

void acionaSolenoideRTC(bool *p_solenoide, float *p_umid_solo, byte *p_segundos, byte *p_minutos, byte *p_horas)
{
  if (*p_minutos == 0 || *p_minutos == 10 || *p_minutos == 20 || *p_minutos == 30 || *p_minutos == 40 || *p_minutos == 50 || *p_umid_solo < 40)
  {
    digitalWrite(RELE, HIGH);
    *p_solenoide = true;
  }
  else
  {
    digitalWrite(RELE, LOW);
    *p_solenoide = false;
  }
}

void displayInfo(float *p_temp, float *p_umid, float *p_press, unsigned long *p_totalMilliLitres, float *p_flowRate, float *p_umid_solo, bool *p_solenoide, byte *p_hora, byte *p_minuto, byte *p_segundo, char **p_dados)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    Data data = getData();

    int tamanho_string = snprintf(NULL, 0, "{\"temp\":%.2f, \"umid\":%.2f, \"qnt\":%i, \"umid_solo\":%.2f}", *p_temp, *p_umid, *p_totalMilliLitres, *p_umid_solo);

    *p_dados = (char *)malloc((tamanho_string + 1) * sizeof(char));

    if (*p_dados != NULL)
    {
      sprintf(*p_dados, "{\"temp\":%.2f, \"umid\":%.2f, \"qnt\":%i, \"umid_solo\":%.2f}", *p_temp, *p_umid, *p_totalMilliLitres, *p_umid_solo);
      Serial.println(*p_dados);
    }
    else
    {
      Serial.println("Erro ao alocar memoria");
      delay(1000);
      ESP.restart();
    }
  }
  else
  {
    int tamanho_string = snprintf(NULL, 0, "{\"temp\":%.2f, \"umid\":%.2f, \"press\":%.2f, \"qnt\":%i, \"umid_solo\":%.2f}", *p_temp, *p_umid, *p_press, *p_totalMilliLitres, *p_umid_solo);

    *p_dados = (char *)malloc((tamanho_string + 1) * sizeof(char));

    if (*p_dados != NULL)
    {
      sprintf(*p_dados, "{\"temp\":%.2f, \"umid\":%.2f, \"qnt\":%i, \"umid_solo\":%.2f}", *p_temp, *p_umid, *p_totalMilliLitres, *p_umid_solo);
      Serial.println(*p_dados);
    }
    else
    {
      Serial.println("Erro ao alocar memoria");
      delay(1000);
      ESP.restart();
    }
  }
}

Data getData(void)
{
  char *strData = (char *)timeClient.getFormattedDate().c_str();

  Data data;
  sscanf(strData, "%d-%d-%dT%d:%d:%dZ", &data.ano, &data.mes, &data.dia, &data.hora, &data.minuto, &data.segundo);

  data.dSemana = timeClient.getDay();
  return data;
}

void liquidQty(byte *p_pulse1Sec, unsigned int *p_flowMilliLitres, unsigned long *p_totalMilliLitres, float *p_flowRate, unsigned long *p_previousMillis)
{
  float calibrationFactor = 4.5;

  *p_pulse1Sec = pulseCount;
  pulseCount = 0;

  *p_flowRate = ((1000.0 / (millis() - *p_previousMillis)) * *p_pulse1Sec) / calibrationFactor;
  *p_previousMillis = millis();

  *p_flowMilliLitres = (*p_flowRate / 60) * 1000;

  *p_totalMilliLitres += *p_flowMilliLitres;
}

void pegaHorarioRTC(byte *p_segundos, byte *p_minutos, byte *p_horas)
{
  DateTime now = rtc.now();
  *p_segundos = now.second();
  *p_minutos = now.minute();
  *p_horas = now.hour();
}

void IRAM_ATTR pulseCounter(void)
{
  pulseCount++;
}
