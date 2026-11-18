# BACnet no Finder OPTA WiFi (BACnet/IP & BACnet MS/TP)

Repositório completo de integração, testes e documentação prática do protocolo **BACnet (ASHRAE 135)** no CLP industrial **Finder OPTA WiFi (STM32H747)** utilizando a biblioteca `BACnet-Arduino` e o cliente de engenharia **YABE (Yet Another BACnet Explorer)**.

---

## Estrutura do Projeto

| Caminho | Descrição |
| :--- | :--- |
| **[`Opta_BACnetIP_Test/`](Opta_BACnetIP_Test/Opta_BACnetIP_Test.ino)** | Sketch de teste como **Servidor BACnet/IP (Ethernet RJ45, UDP 47808)** (`DEVICE:2001`) |
| **[`Opta_BACnetMSTP_Test/`](Opta_BACnetMSTP_Test/Opta_BACnetMSTP_Test.ino)** | Sketch de teste como **Mestre/Servidor BACnet MS/TP (RS-485)** (`DEVICE:2002`) |
| **[`docs/BACnet_IP_Arduino_Opta.md`](docs/BACnet_IP_Arduino_Opta.md)** | Documentação técnica completa do modo **BACnet/IP (Ethernet)** |
| **[`docs/BACnet_MSTP_Arduino_Opta.md`](docs/BACnet_MSTP_Arduino_Opta.md)** | Documentação técnica completa do modo **BACnet MS/TP (RS-485)** |
| **`BACnet-Arduino/`** e **`BACnet-Arduino.zip`** | Biblioteca `BACnet-Arduino v0.7.0` pronta para uso com os exemplos do Opta |
| **`WeeklyAlarm/`** e **`WeeklyAlarm.zip`** | Biblioteca `WeeklyAlarm v5.2.0` com correção de detecção `__NEWLIB__` para o core `arduino:mbed_opta` |
| **`BACnet-OPTA-Pacote-Completo.zip`** | Pacote `.zip` unificado contendo bibliotecas, sketches e documentação |

---

## Resumo das Alterações Realizadas nas Bibliotecas

1. **`WeeklyAlarm/src/WeeklyAlarm.h` (linhas 33–39)**:
   Adicionada a inclusão condicional de `<_newlib_version.h>` / `<newlib.h>` via `__has_include` antes de `#ifdef __NEWLIB__`. Isso corrige a falha durante a fase de descoberta de bibliotecas da Arduino IDE (`-DARDUINO_LIBRARY_DISCOVERY_PHASE=1`) no core `arduino:mbed_opta`, evitando o erro `TimeLib.h: No such file or directory` e o conflito de *linker* (`multiple definition of breakTime / makeTime`).
2. **`BACnet-Arduino/src/ports/arduino/arduino_configs.h` (linha 12)**:
   - Para **BACnet/IP**: manter `//#define BACDL_MSTP` comentado (o modo `BACDL_BIP` é ativado por padrão).
   - Para **BACnet MS/TP**: descomentar `#define BACDL_MSTP`.

Consulte os guias detalhados em [`docs/BACnet_IP_Arduino_Opta.md`](docs/BACnet_IP_Arduino_Opta.md) e [`docs/BACnet_MSTP_Arduino_Opta.md`](docs/BACnet_MSTP_Arduino_Opta.md).

