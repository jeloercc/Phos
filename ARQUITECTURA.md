# Proyecto: Criatura de Computación (CYD Buddy)

> Documento maestro del proyecto. Esta es la referencia compartida entre el
> usuario (Julian), Claude (copiloto técnico) y el agente de CLI (ejecutor).
> Cualquier cambio de rumbo se anota aquí primero.

> **Plataforma de desarrollo de Julian: macOS.** (Puertos `cu.usbserial-*`.)
> Esto importa: toda la lógica del PUENTE y las acciones del sistema se escriben
> para macOS, no para Windows.

---

## 1. Visión

Un **asistente de escritorio con cara viva**: una **criatura de computación**
que vive en la pantalla de una placa CYD. No es una cara con ojos, es una
**presencia** orgánica y abstracta, con estética **Matrix minimalista**: fondo
negro, verde fósforo, lluvia de código y partículas que respiran y reaccionan.

La criatura es la **cara** de un asistente útil. Su inteligencia vive en un
LLM local (Ollama) en la PC. El asistente:
- Da **info en vivo** (clima, hora, recordatorios, temporizadores).
- Puede **controlar la PC** (abrir apps, acciones del sistema) — con lista blanca.
- Puede **controlar la casa** (luces, enchufes vía Home Assistant u similar).
- Se interactúa por **botones táctiles + teclado para notas cortas** en la
  pantalla, y por texto largo desde la PC.

En esencia: un "Jarvis minimalista" personal, construido por fases.

Inspiración conceptual: Physical Taby (carita + comandos + estética propia).
Pero NO se construye desde cero: se parte de una base probada y se le trasplanta
el alma (ver sección 11).

### Meta: hiperextrapolar, superar a Muse/Taby
El diferenciador frente a Muse/Taby/decks existentes son TRES cosas que ninguno
reúne a la vez:
1. Una **criatura generativa viva** (no una pantalla de stats ni una carita por video).
2. **Conversación real con Ollama** (no "abrir ChatGPT en el navegador").
3. La criatura **fusiona IA + estado del sistema**: piensa cuando el LLM procesa,
   se altera si la CPU se dispara, etc. Cuerpo y cerebro como una sola presencia.

---

## 2. Hardware objetivo

- **Placa:** Inland 2.8" ESP32-32E (variante de la "Cheap Yellow Display" /
  ESP32-2432S028).
- **Chip:** ESP32-WROOM-32 (doble núcleo 240 MHz). **Sin PSRAM.**
- **Flash:** 4 MB.
- **Pantalla:** 2.8", 240x320, controlador **ILI9341** (SPI).
- **Táctil:** resistivo, controlador **XPT2046**.
- **Extra:** ranura microSD, LED RGB, sensor de luz (LDR), botón BOOT.
- **Nota:** por ser la variante "32E", algunos pines pueden diferir de la CYD
  clásica. VERIFICAR pines reales antes de dar nada por hecho.

### Restricciones que mandan en el diseño
- **Sin PSRAM** → nada de buffers de pantalla completa en RAM. Render parcial.
- **4 MB de flash** → firmware + assets deben caber. Los assets pesados van a la SD.
- GIF a pantalla completa es lento en esta placa. Para video se usa **MJPEG desde la SD**.

---

## 3. Decisiones tomadas

| Tema | Decisión | Estado |
|------|----------|--------|
| Entorno de desarrollo | **PlatformIO** | Fijo |
| Comunicación (v1) | **USB serie** primero; WiFi en fase posterior | Fijo |
| Personalidad | Criatura de computación, orgánica-abstracta, **Matrix minimalista** | Fijo |
| Técnica visual | **Mezcla**: base generativa en tiempo real + momentos MJPEG desde SD | Fijo |
| Cerebro IA | **Ollama local** en la PC (privado, gratis, offline) | Fijo |
| Rol | **Asistente de tareas** con cara viva (no solo mascota) | Fijo |
| Tipos de tarea (v1) | Info en vivo · Controlar PC · Controlar casa | Fijo |
| Entrada táctil | **Mezcla**: botones de tareas rápidas + teclado para notas cortas | Fijo |
| Momento IA | Después de que el cuerpo funcione (tras Fases 0-2) | Fijo |
| Librería gráfica | A decidir (candidata: LVGL 9.x o LovyanGFX/TFT_eSPI directo) | Pendiente |

### Regla de seguridad FIJA
Para "controlar la PC" y "controlar la casa": la criatura **solo** ejecuta
acciones de una **lista blanca** definida por Julian. El LLM elige entre
acciones permitidas; **nunca** se ejecuta texto libre del modelo directo en el
sistema. Toda acción potencialmente destructiva requiere confirmación.

---

## 4. Estética (el norte visual)

- **Paleta estricta:**
  - Fondo: negro profundo (`#000000`)
  - Vivo: verde fósforo Matrix (`#00FF41`)
  - Profundidad: verdes más tenues (`#008F11`, `#003B00`)
  - La restricción de color ES el minimalismo. No se añaden más colores sin decisión explícita.
- **La criatura:** líneas / partículas / lluvia de código que se reorganizan
  según el estado. Siempre en movimiento sutil (nunca totalmente quieta = "viva").

---

## 5. Estados de ánimo (la "API" de la criatura)

Cada estado es un comportamiento visual. El cerebro del firmware es una
máquina de estados que transiciona entre ellos.

| Estado | Comando serie | Comportamiento visual |
|--------|---------------|-----------------------|
| Reposo | `IDLE` | Respira lento, partículas flotando suaves |
| Pensando | `THINKING` | La lluvia de código se acelera y concentra |
| Activa / hablando | `SPEAKING` | Pulsos de verde que laten con ritmo |
| Durmiendo | `SLEEP` | Todo se atenúa, una línea tenue que late como latido |
| Alerta / tocada | `ALERT` | Se dispersa y se recompone (también al tocar la pantalla) |

Protocolo serie (borrador, inspirado en Taby):
- Comandos UTF-8 terminados en newline, 115200 baud.
- `PING` -> `OK:PONG`
- `<ESTADO>` (ej. `THINKING`) -> cambia de estado, responde `OK:<ESTADO>`
- Comando desconocido -> `ERR:<texto>`

---

## 6. Arquitectura de software (dos mundos: PC y placa)

Hay dos mitades: el **CEREBRO** corre en la PC; el **CUERPO** corre en el CYD.
Se hablan por USB serie (luego WiFi).

```
╔══════════════ CEREBRO (tu PC, Python) ══════════════╗
║  ┌─────────┐   ┌──────────────────────────────────┐ ║
║  │ Ollama   │──▶│ PUENTE                            │ ║
║  │ (LLM)    │   │ - manda tu mensaje al LLM         │ ║
║  └─────────┘   │ - interpreta respuesta            │ ║
║                │ - ejecuta tareas (lista blanca):  │ ║
║                │     · info en vivo (clima/hora)   │ ║
║                │     · controlar PC                │ ║
║                │     · controlar casa              │ ║
║                │ - manda ESTADO + datos a la placa │ ║
║                └───────────────┬──────────────────┘ ║
╚════════════════════════════════┼════════════════════╝
                                 │ USB serie (115200)
                                 ▼
╔══════════════ CUERPO (tu CYD) ══════════════════════╗
║  CAPA 4 — Comunicación: recibe comandos             ║
║  CAPA 3 — Cerebro local: máquina de estados de ánimo║
║           + lógica de la UI táctil (botones/teclado)║
║  CAPA 2 — Render:                                   ║
║     A) Motor generativo (dibuja la criatura en vivo)║
║     B) Reproductor MJPEG desde SD (momentos esp.)   ║
║  CAPA 1 — Hardware: ILI9341 · XPT2046 · SD · LED    ║
╚══════════════════════════════════════════════════════╝
```

Principio: cada capa solo habla con la de abajo. Si cambiamos de placa algún
día, solo se toca la Capa 1. Y el CUERPO no sabe nada de IA: solo recibe
estados y datos. Toda la inteligencia vive en el CEREBRO (PC). Eso mantiene al
ESP32 simple y rápido.

---

## 7. Plan por fases

> Regla de oro: cada fase termina con algo que FUNCIONA y se puede ver en la
> pantalla antes de pasar a la siguiente. Nada de construir 4 capas a ciegas.

- **FASE 0 — Cimientos (ADAPTAR la base)**
  - Clonar ESP32-PC-Control-Deck. Abrir en PlatformIO.
  - Ajustar pines/driver a la variante Inland 32E y compilar.
  - Flashear y confirmar que la pantalla enciende y el táctil calibra.
  - Correr el puente en macOS (run_linux_macos.sh) y ver stats llegando.
  - *Resultado visible:* la base original funciona en tu placa y tu Mac.

- **FASE 1 — Trasplante del alma, parte 1: la criatura respira**
  - Reemplazar la UI de stats por el motor generativo Matrix (IDLE).
  - *Resultado visible:* en vez de números, una criatura viva en reposo.

- **FASE 2 — El cerebro y los comandos (ya existe el canal)**
  - Usar el protocolo serie existente para mandar ESTADOS a la criatura.
  - Máquina de estados: IDLE / THINKING / SPEAKING / SLEEP / ALERT.
  - *Resultado visible:* un comando desde la Mac cambia el ánimo de la criatura.

- **FASE 3 — El tacto**
  - Táctil XPT2046 funcionando.
  - Tocar la pantalla dispara el estado `ALERT`.
  - *Resultado visible:* tocas la criatura y reacciona.

- **FASE 4 — El cerebro habla (Ollama + puente)** ⭐ aquí entra la IA
  - Programa puente en Python en la PC, conectado a Ollama.
  - Le escribes desde la PC → la criatura muestra THINKING mientras piensa,
    SPEAKING al responder, IDLE al terminar.
  - *Resultado visible:* conversas con la criatura y su cara refleja lo que hace.

- **FASE 5 — Tareas útiles (lista blanca)**
  - El puente ejecuta acciones permitidas y devuelve resultados a la pantalla.
  - Empezar por **info en vivo** (clima/hora/temporizador) por ser la más segura.
  - Luego **controlar PC** y **controlar casa**, cada una con su lista blanca.
  - *Resultado visible:* le pides algo y lo hace; la criatura muestra el resultado.

- **FASE 6 — Panel táctil de tareas**
  - UI en la pantalla: botones de tareas rápidas + teclado para notas cortas.
  - Tocar un botón dispara una tarea sin pasar por la PC.
  - *Resultado visible:* operas el asistente desde la propia criatura.

- **FASE 7 — Momentos especiales (MJPEG desde SD)**
  - Reproductor MJPEG leyendo de la tarjeta SD.
  - Un comando dispara una animación pre-renderizada cinematográfica.
  - *Resultado visible:* un momento especial se reproduce y vuelve al modo generativo.

- **FASE 8 — WiFi (opcional, futuro)**
  - Control por red: el puente habla con la placa por WiFi, no solo USB.
  - Permite que la criatura viva lejos de la PC.

---

## 8. Cómo trabajamos los tres

- **Julian (tú):** dueño de la visión. Decides estética, prioridades, qué se
  siente bien. Flasheas y pruebas en el hardware real (Claude no ve tu placa).
- **Claude (yo):** copiloto técnico. Investigo, propongo arquitectura, explico,
  reviso el código que produce el agente, detecto problemas.
- **Agente de CLI:** ejecutor. Escribe y edita el código en tu máquina, corre
  los builds, flashea cuando se lo pides.
- **Decisiones:** se toman entre Julian y Claude. El agente ejecuta lo acordado.

---

## 9. Riesgos conocidos / pendientes de resolver

- [ ] Confirmar pines exactos de la variante Inland 32E (pantalla, táctil, SD).
- [ ] Elegir librería gráfica (LVGL vs LovyanGFX/TFT_eSPI directo). Para render
      generativo propio, dibujar directo con LovyanGFX puede ser más simple y
      rápido que LVGL. A investigar en Fase 0/1.
- [ ] Rendimiento del motor generativo sin PSRAM: cuántas partículas aguanta
      a buen framerate. Se mide en Fase 1.
- [ ] Coexistencia pantalla + SD en el bus SPI (comparten o no bus). Afecta Fase 7.
- [ ] Rotación / colores invertidos de la pantalla en la variante 32E.
- [ ] Elegir modelo de Ollama que corra bien en la PC de Julian (depende de su RAM/GPU).
- [ ] Definir la lista blanca concreta de acciones de PC y de casa (Fase 5).
- [ ] Teclado táctil en resistivo 2.8": confirmar que es usable para notas cortas (Fase 6).
- [ ] Tensión: escribir mucho texto en pantalla resistiva es incómodo → texto largo desde PC.

---

## 10. Estado actual

**Fase activa: FASE 0 (cimientos), ahora partiendo de la base reusada.**
Próximo paso: clonar la base, portarla a macOS, encender la pantalla.

---

## 11. Estrategia de reuso (NO construimos desde cero)

### Base principal
**ESP32-PC-Control-Deck** — https://github.com/lepczynski-cloud/ESP32-PC-Control-Deck
Licencia **MIT** (podemos usarla y modificarla libremente, dando crédito).

Hardware casi idéntico al nuestro: ESP32-WROOM-32, ILI9341 320x240, XPT2046, CH340.
PlatformIO + TFT_eSPI. Arquitectura ya separada en cuerpo/puente/config, igual que
la nuestra.

Qué nos da ya hecho:
- `src/main.cpp` → firmware del CUERPO (pantalla + táctil + protocolo serie).
- `host/control_deck_bridge.py` → el PUENTE en Python (pyserial).
- `host/config.json` → acciones configurables = nuestra LISTA BLANCA (ya implementada).
- Calibración táctil guardada en el ESP32 (resuelve un riesgo).
- Ollama ya integrado (comprueba `127.0.0.1:11434`, lo arranca si hace falta).
- Protocolo serie documentado (JSON con prefijo `DD:`). Ver `docs/PROTOCOL.md`.
- Modelo de seguridad correcto: el ESP32 solo manda un ID numérico; el puente
  ejecuta solo lo de config.json. El ESP32 no guarda secretos.

Lo que hay que ADAPTAR:
- **Portar el puente a macOS.** El original es Windows (nvidia-smi, LibreHardwareMonitor,
  Terminal de Windows, .bat/.ps1). Trae `run_linux_macos.sh`, pero las *acciones*
  concretas (abrir apps, bloquear, leer temps) hay que reescribirlas para macOS.

### Piezas de apoyo (referencia / ideas, no base)
- **CYD-Smart-Dashboard-for-Home-Assistant** (drrcastro) — control de casa (Home
  Assistant) y calibración táctil resistiva de referencia. Para Fase 5 (casa).
- **ESP32-AI-Chatbot** (Den-Sec) y **ESP32_AI_Connect** (AvantMaker) — ejemplos
  de hablar con la API de Ollama. Referencia para el puente/conversación.
- **RobotStudyCompanion/CYD** — reproductor de "emociones" MJPEG en CYD. Referencia
  para Fase 7 (momentos MJPEG desde SD).

### Lo que construimos NOSOTROS (el alma, nuestra pieza original)
1. **Motor generativo de la criatura Matrix** — reemplaza la UI de stats estática
   del deck por una presencia viva. Nadie lo tiene; es tuyo.
2. **Conversación real con Ollama** — más allá del botón "abrir ChatGPT".
3. **Fusión IA + sistema** — la criatura refleja lo que piensa el LLM y el estado
   del PC a la vez.

### Flujo de trabajo revisado
Las fases 0-2 ya no se construyen: se **adaptan** de la base. El esfuerzo real se
concentra en portar a macOS y en trasplantar el alma (criatura + conversación).
