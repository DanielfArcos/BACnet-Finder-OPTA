/*
 * Teste de Comunicacao BACnet/IP no Arduino Opta WiFi (Ethernet)
 *
 * Objetivo:
 *   Expor o Opta como Servidor BACnet/IP (UDP porta 47808 / 0xBAC0) para
 *   testes com o cliente YABE (Yet Another BACnet Explorer) no PC.
 *
 * Mapeamento de Objetos BACnet:
 *   - BO:0 .. BO:3  -> Reles fisicos D0..D3 e LEDs frontais LED_D0..LED_D3
 *   - BI:0          -> Botao fisico frontal BTN_USER (pressionado = ACTIVE)
 *   - BI:1 .. BI:6  -> Entradas digitais fisicas I3..I8 (pinos A2..A7)
 *   - AI:0 .. AI:1  -> Entradas analogicas 0-10 V em I1..I2 (pinos A0..A1)
 *   - AV:0          -> Contador de teste (incrementa 1/s, util para COV)
 *
 * Bibliotecas necessarias na Arduino IDE:
 *   - BACnet-Arduino (0.7.0, pasta BACnet-Arduino neste workspace)
 *   - WeeklyAlarm (>= 5.2.0; pasta WeeklyAlarm neste workspace com ajuste de deteccao __NEWLIB__)
 *   - NTPClient (Library Manager)
 *
 * Configuracao da biblioteca:
 *   - Nao exige editar arduino_configs.h (BACDL_BIP e o datalink padrao).
 */

#include <Arduino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <mbed_mktime.h>
#include <time.h>

#include "BACnet-Arduino.h"

#if !defined(BACDL_BIP)
#error "Este sketch requer BACDL_BIP habilitado (padrao em arduino_configs.h)"
#endif
#if defined(BACDL_MULTIPLE) || defined(BACNET_ROUTER)
#error "Desabilite datalinks extras e BACNET_ROUTER em arduino_configs.h para este teste"
#endif

// -----------------------------------------------------------------------------
// Configuracao de Rede (ajuste para a mesma sub-rede do seu PC)
// -----------------------------------------------------------------------------
// 0 = IP estatico (recomendado para cabo direto PC <-> Opta ou bancada)
// 1 = DHCP (requer roteador/servidor DHCP na rede)
#define USE_DHCP 0

constexpr uint8_t DEVICE_IP[] = { 192, 168, 0, 150 };
constexpr uint16_t BACNET_UDP_PORT = 47808;  // 0xBAC0

// -----------------------------------------------------------------------------
// Identidade do Dispositivo BACnet
// -----------------------------------------------------------------------------
const deviceInfos_t deviceInfos {
    2001,                                       // Device Instance (unico na rede)
    "Opta_WiFi_BACnet",                         // Object Name
    "Arduino Opta WiFi BACnet/IP Test Server",  // Description
    "Bancada",                                  // Location
    "Arduino",                                  // Vendor Name
    "Opta WiFi",                                // Model Name
    "1.0",                                      // Firmware Revision
    "0.1"                                       // Application Software Version
};

#define DCC_PASSWORD    "1234"
#define REINIT_PASSWORD "4321"

// Epoch inicial para o relogio interno quando nao ha NTP (ex.: 2026-11-11 00:00:00 UTC)
constexpr time_t DEFAULT_EPOCH = 1794355200UL;

// -----------------------------------------------------------------------------
// Mapeamento de Hardware do Arduino Opta
// -----------------------------------------------------------------------------
struct RelayChannel {
    uint32_t instance;
    decltype(D0) relayPin;
    decltype(LED_D0) ledPin;
    const char *name;
    const char *description;
    BACNET_BINARY_PV lastState;
};

static RelayChannel relays[] = {
    { 0, D0, LED_D0, "RELAY_1", "Opta Relay D0", BINARY_INACTIVE },
    { 1, D1, LED_D1, "RELAY_2", "Opta Relay D1", BINARY_INACTIVE },
    { 2, D2, LED_D2, "RELAY_3", "Opta Relay D2", BINARY_INACTIVE },
    { 3, D3, LED_D3, "RELAY_4", "Opta Relay D3", BINARY_INACTIVE },
};

struct DigitalInputChannel {
    uint32_t instance;
    decltype(A2) pin;
    const char *name;
    const char *description;
    BACNET_BINARY_PV lastState;
};

// Entradas fisicas I3..I8 mapeadas como Binary Input (BI:1 .. BI:6)
static DigitalInputChannel digitalInputs[] = {
    { 1, A2, "I3_DI", "Opta Input I3 (A2)", BINARY_INACTIVE },
    { 2, A3, "I4_DI", "Opta Input I4 (A3)", BINARY_INACTIVE },
    { 3, A4, "I5_DI", "Opta Input I5 (A4)", BINARY_INACTIVE },
    { 4, A5, "I6_DI", "Opta Input I6 (A5)", BINARY_INACTIVE },
    { 5, A6, "I7_DI", "Opta Input I7 (A6)", BINARY_INACTIVE },
    { 6, A7, "I8_DI", "Opta Input I8 (A7)", BINARY_INACTIVE },
};

struct AnalogInputChannel {
    uint32_t instance;
    decltype(A0) pin;
    const char *name;
    const char *description;
};

// Entradas fisicas I1..I2 mapeadas como Analog Input 0-10 V (AI:0 .. AI:1)
static const AnalogInputChannel analogInputs[] = {
    { 0, A0, "I1_AI_V", "Opta Input I1 0-10V (A0)" },
    { 1, A1, "I2_AI_V", "Opta Input I2 0-10V (A1)" },
};

constexpr uint32_t BI_USER_BUTTON_INST = 0;
constexpr uint32_t AV_COUNTER_INST = 0;

// -----------------------------------------------------------------------------
// Instancias BACnet/IP
// -----------------------------------------------------------------------------
EthernetUDP Udp;
BIP_Peripheral<arduino::EthernetClass, EthernetUDP> BIP_Periph(&Ethernet, &Udp, BACNET_UDP_PORT);

static BACNET_BINARY_PV lastButtonState = BINARY_INACTIVE;

// -----------------------------------------------------------------------------
// Funcoes auxiliares
// -----------------------------------------------------------------------------
static float readOptaVoltage(decltype(A0) pin)
{
    // ADC em 16 bits (0..65535 para 0..3.3V) com divisor de entrada ~0.3034 (0..10V)
    const int raw = analogRead(pin);
    return (static_cast<float>(raw) * (3.3f / 65535.0f)) / 0.3034f;
}

static void applyRelayHardware(const RelayChannel &ch, BACNET_BINARY_PV state)
{
    const PinStatus level = (state == BINARY_ACTIVE) ? HIGH : LOW;
    digitalWrite(ch.relayPin, level);
    digitalWrite(ch.ledPin, level);
}

static void reinitColdStartCb()
{
    Serial.println("[BACnet] ReinitializeDevice (Coldstart) solicitado. Reiniciando...");
    delay(100);
    NVIC_SystemReset();
}

static void setupNetwork()
{
#if USE_DHCP
    Serial.println("[Net] Iniciando Ethernet via DHCP...");
    if (Ethernet.begin() == 0) {
        Serial.println("[Net] DHCP falhou. Aplicando IP estatico de fallback...");
        Ethernet.begin(DEVICE_IP);
    }
#else
    Serial.println("[Net] Iniciando Ethernet com IP estatico...");
    Ethernet.begin(DEVICE_IP);
#endif

    Udp.begin(BACNET_UDP_PORT);

    Serial.print("[Net] IP do Opta: ");
    Serial.println(Ethernet.localIP());
    Serial.print("[Net] Porta UDP BACnet/IP: ");
    Serial.println(BACNET_UDP_PORT);
    Serial.print("[Net] Link fisico: ");
    Serial.println(Ethernet.linkStatus() == LinkON ? "LinkON (Cabo OK)" : "LinkOFF (Verifique cabo)");

    // Inicializa relogio interno (sem depender de internet/NTP)
    set_time(DEFAULT_EPOCH);
}

static void setupHardware()
{
    analogReadResolution(16);
    pinMode(BTN_USER, INPUT_PULLUP);

    for (auto &ch : relays) {
        pinMode(ch.relayPin, OUTPUT);
        pinMode(ch.ledPin, OUTPUT);
        applyRelayHardware(ch, ch.lastState);
    }

    for (const auto &di : digitalInputs) {
        pinMode(di.pin, INPUT);
    }
}

static void setupBacnet()
{
    BIP_Periph.begin();
    bacnet_init(&deviceInfos);

    setTimeCallback_set(set_time);
    handler_dcc_password_set(DCC_PASSWORD);
    Device_Reinitialize_Password_Set(REINIT_PASSWORD);
    bacnet_reinitialize_callback_set(BACNET_REINIT_COLDSTART, reinitColdStartCb);

    // Reles D0..D3 (BO:0 .. BO:3)
    for (const auto &ch : relays) {
        createBinaryOutput(ch.instance, ch.name, ch.description, ch.lastState);
    }

    // Botao frontal (BI:0)
    lastButtonState = (digitalRead(BTN_USER) == LOW) ? BINARY_ACTIVE : BINARY_INACTIVE;
    createBinaryInput(BI_USER_BUTTON_INST, "BTN_USER", "Opta Front User Button", lastButtonState);

    // Entradas digitais I3..I8 (BI:1 .. BI:6)
    for (auto &di : digitalInputs) {
        di.lastState = (digitalRead(di.pin) == HIGH) ? BINARY_ACTIVE : BINARY_INACTIVE;
        createBinaryInput(di.instance, di.name, di.description, di.lastState);
    }

    // Entradas analogicas I1..I2 em Volts (AI:0 .. AI:1)
    for (const auto &ai : analogInputs) {
        createAnalogInput(ai.instance, ai.name, ai.description, UNITS_VOLTS);
        Analog_Input_Present_Value_Set(ai.instance, readOptaVoltage(ai.pin));
    }

    // Objeto de teste continuo para leitura/COV sem fiação externa (AV:0)
    createAnalogValue(AV_COUNTER_INST, "TEST_COUNTER", "1s Test Counter (for COV)", UNITS_NO_UNITS, 0.0f);
}

static void syncOutputsFromBacnet()
{
    for (auto &ch : relays) {
        const BACNET_BINARY_PV target = Binary_Output_Present_Value(ch.instance);
        if (target != ch.lastState) {
            ch.lastState = target;
            applyRelayHardware(ch, target);

            Serial.print("[BACnet -> Opta] ");
            Serial.print(ch.name);
            Serial.print(" = ");
            Serial.println(target == BINARY_ACTIVE ? "ACTIVE (ON)" : "INACTIVE (OFF)");
        }
    }
}

static void syncInputsToBacnet()
{
    const BACNET_BINARY_PV btnState = (digitalRead(BTN_USER) == LOW) ? BINARY_ACTIVE : BINARY_INACTIVE;
    if (btnState != lastButtonState) {
        lastButtonState = btnState;
        Binary_Input_Present_Value_Set(BI_USER_BUTTON_INST, btnState);

        Serial.print("[Opta -> BACnet] BTN_USER = ");
        Serial.println(btnState == BINARY_ACTIVE ? "ACTIVE (Pressionado)" : "INACTIVE (Solto)");
    }

    for (auto &di : digitalInputs) {
        const BACNET_BINARY_PV state = (digitalRead(di.pin) == HIGH) ? BINARY_ACTIVE : BINARY_INACTIVE;
        if (state != di.lastState) {
            di.lastState = state;
            Binary_Input_Present_Value_Set(di.instance, state);
        }
    }
}

// -----------------------------------------------------------------------------
// Arduino setup & loop
// -----------------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    // Aguarda ate 3 s pelo Monitor Serial sem travar se rodar autonomo
    const uint32_t startMs = millis();
    while (!Serial && (millis() - startMs < 3000)) {
    }

    Serial.println("\n=== Arduino Opta WiFi - Teste BACnet/IP ===");
    setupHardware();
    setupNetwork();
    setupBacnet();
    Serial.println("[BACnet] Servidor pronto! Abra o YABE e faca a descoberta (Who-Is).");
}

void loop()
{
    static uint32_t lastMs = millis();
    const uint32_t nowMs = millis();
    Device_Timer(nowMs - lastMs);
    lastMs = nowMs;

    static uint32_t lastSecMs = millis();
    if (millis() - lastSecMs >= 1000) {
        lastSecMs += 1000;
        dcc_timer_seconds(1);

        for (const auto &ai : analogInputs) {
            Analog_Input_Present_Value_Set(ai.instance, readOptaVoltage(ai.pin));
        }

        const float nextCounter = Analog_Value_Present_Value(AV_COUNTER_INST) + 1.0f;
        Analog_Value_Present_Value_Set(AV_COUNTER_INST, nextCounter, BACNET_PRIORITY_NORMAL);
    }

    bacnet_Poll();
    syncOutputsFromBacnet();
    syncInputsToBacnet();
}

