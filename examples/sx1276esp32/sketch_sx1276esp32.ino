#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <LoRa.h>

// Mapeamento exclusivo da Heltec WiFi LoRa 32 (V2)
#define OLED_SDA   4
#define OLED_SCL  15
#define OLED_RST  16
#define VEXT_PIN  21  // Pino de controle de energia da tela e sensores
// Pinos para Heltec WiFi LoRa 32 V2 (ajuste conforme sua placa)
#define LORA_SS   18
#define LORA_RST  14
#define LORA_DIO0 26

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

// Contador de pacotes enviados
int packetCounter = 0;

// Valores simulados de sensores (substitua pela leitura real se tiver sensor conectado)
float temperature = 0.0;
float humidity = 0.0;

void setup() {
  Serial.begin(115200);
  while (!Serial);

  // 1. Liga a alimentação da tela (Vext ativo em nível LOW)
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(100);

  // 2. Pulso de Reset físico no OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH);
  delay(50);

  // 3. Inicializa o barramento I2C nos pinos 4 e 15
  Wire.begin(OLED_SDA, OLED_SCL);

  // 4. Inicializa a tela no endereço I2C 0x3C
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {
    Serial.println("Erro: Não foi possível comunicar com o display SSD1306!");
    while (true);
  }

  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  // Frequência deve bater com um canal do gateway (904.3 MHz = chan_multiSF_2)
  if (!LoRa.begin(904.3E6)) {
    Serial.println("Falha ao iniciar LoRa!");
    while (1);
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x34);   // Sync word LoRaWAN (gateway espera 0x34)
  LoRa.enableCrc();

  // Tela inicial
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Heltec LoRa 32 V2");
  display.setCursor(0, 16);
  display.println("LoRa pronto!");
  display.setCursor(0, 32);
  display.println("Aguardando envio...");
  display.display();

  Serial.println("Display inicializado com sucesso!");
  Serial.println("LoRa pronto! Enviando...");
}

void loop() {
  // --- Leitura dos sensores (substitua por leitura real se tiver sensor conectado) ---
  // Exemplo com sensor DHT11/DHT22: temperature = dht.readTemperature(); humidity = dht.readHumidity();
  temperature = 20.0 + (random(0, 150) / 10.0);   // simula 20.0 a 35.0 °C
  humidity    = 40.0 + (random(0, 300) / 10.0);    // simula 40.0 a 70.0 %

  packetCounter++;

  // Monta o payload com os dados dos sensores
  String payload = "T:" + String(temperature, 1) + "C,H:" + String(humidity, 1) + "%,N:" + String(packetCounter);

  // Envia via LoRa
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  // Log no Serial Monitor
  Serial.println("Enviado: " + payload);

  // Atualiza o display com os dados enviados
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("SX1302 Gateway Test");
  display.setCursor(0, 16);
  display.println("Temp: " + String(temperature, 1) + " C");
  display.setCursor(0, 28);
  display.println("Hum:  " + String(humidity, 1) + " %");
  display.setCursor(0, 40);
  display.println("Pkt #" + String(packetCounter));
  display.setCursor(0, 52);
  display.println("Enviado OK");
  display.display();

  delay(10000);  // envia a cada 10 segundos
}
