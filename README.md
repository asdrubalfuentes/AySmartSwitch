# aySmartSwitch v2 (ESP32-WROOM-32 y ESP32-C3 SuperMini)

Firmware de los instrumentos conectados de AYSAFI: relés (portones, puertas, cargas), entradas digitales
(contactos, PIR), temperatura/humedad (DHT11/22) y lecturas analógicas, con alarmas configurables.
Se administra desde la app **aySmartSwitch** (`monitor-api`): el equipo no lleva ninguna configuración
compilada; todo se la entrega el servidor al adherirse.

| Placa | Entorno PlatformIO | Archivos del release |
|---|---|---|
| ESP32-WROOM-32 | `esp32dev` | `firmware-esp32.{bin,sha256,sig}` |
| ESP32-C3 SuperMini | `c3mini` | `firmware-esp32c3.{bin,sha256,sig}` |

**ESP32-C3 SuperMini para abrir puertas y portones:** esquema de conexión, pines, pruebas y puesta en marcha en
[`docs/ESP32-C3-SuperMini.md`](docs/ESP32-C3-SuperMini.md). Cada equipo trae una consola de diagnóstico por el puerto
serie (escribir `ayuda`).

> La versión anterior (ESP8266/ESP32 con credenciales en el código) sigue en la rama `main`.

## Primera vez (equipo nuevo)

1. Flashear por USB una sola vez: `pio run -e c3mini -t upload` (C3 SuperMini) o `pio run -e esp32dev -t upload` (WROOM).
2. En la app, el administrador crea el instrumento (con una plantilla o con sus funciones y pines)
   y pulsa **Adherir equipo**: se genera un **código `ABCD-EFGH`** (un solo uso, vence en 24 h).
3. Encender el equipo y conectarse desde el celular a su red **`AySmartSwitch-XXXX`**
   (clave = últimos 8 dígitos de su MAC, impresa en la etiqueta). Se abre el portal cautivo
   (o `http://192.168.4.1`): elegir el WiFi del sitio, escribir su clave y el código.
4. El equipo se reinicia, se conecta, canjea el código por HTTPS y recibe sus credenciales MQTT y la
   configuración de sus canales. Desde ahí aparece **en línea** en la app.

**Botón BOOT (GPIO0 en WROOM, GPIO9 en C3):** 3 s = abrir el portal · 10 s = restaurar de fábrica.
LED: parpadeo rápido = portal abierto · lento = sin WiFi · muy lento = sin servidor · apagado = todo bien.

## Pines (ESP32-WROOM-32)

La app solo deja asignar pines seguros: salidas 4, 5, 13, 14, 16–19, 21–23, 25–27, 32, 33; entradas esas
más 34, 35, 36, 39 (solo entrada, sin pull-up); analógicas en ADC1 (32–36, 39). Se evitan 0, 2, 12, 15 y 6–11.
Los relés arrancan siempre en reposo y el fin de un pulso lo corta un temporizador de hardware,
independiente del programa principal.

## Protocolo MQTT (`aysafi/<org>/<deviceId>/…`)

| Tópico | Sentido | Contenido |
|---|---|---|
| `status` (retenido, LWT) | equipo → | `{online, fw, ip, rssi, uptime_s, cfgv}`; el LWT publica `{online:false}` |
| `metrics` | equipo → | `{fw, uptime_s, rssi, heap_free, heap_min, reset_reason, wifi_reconnects, mqtt_reconnects, ciclos}` |
| `ch/<id>/state` (retenido) | equipo → | `{v}` (`on`/`off`, `activo`/`inactivo`, o el valor medido) |
| `ch/<id>/evt` | equipo → | `{evt:'alarma'\|'normal', nombre, cond, valor}` |
| `ch/<id>/ack` | equipo → | `{id, ok, detail}` |
| `ch/<id>/cmd` | → equipo | `{id, v:'pulso'\|'on'\|'off', by, exp}`; si `exp` ya pasó, no se ejecuta |
| `cfg` (retenido) | → equipo | `{cfgv}`: hay configuración nueva, el equipo la pide por HTTPS |

`<id>` es el id de la función en el servidor. El `deviceId` son los 12 dígitos hex de la MAC.

## Actualizaciones (OTA)

Cada 6 h (y desde el portal) el equipo consulta `releases/latest/download/version.txt` del repositorio;
si hay una versión mayor descarga `firmware-<placa>.bin`, verifica su **SHA-256** y su **firma ECDSA P-256**
(`firmware-<placa>.sig`) con la clave pública de `src/certs.h`, y recién entonces lo instala. Sin firma válida no se instala.

Publicar una versión: poner el tag `vX.Y.Z` sobre `main`; el workflow compila, firma y publica el release.
Requiere el secreto de repositorio **`FW_SIGN_KEY`** (clave privada PEM; su huella debe coincidir con la de `src/certs.h`).
Guardar una copia de esa clave fuera del repositorio: si se pierde, hay que reflashear los equipos por USB con una clave nueva.

## Seguridad

- HTTPS y MQTT/TLS validan el certificado contra ISRG Root X1 (Let's Encrypt).
- Por equipo hay usuario y clave MQTT propios; la clave solo se guarda en el equipo y como hash en el servidor.
- El servidor ya incluye los ganchos `/api/mqtt/auth` y `/api/mqtt/acl` para cerrar el broker EMQX
  (cada equipo solo publica y escucha dentro de su propio árbol).
