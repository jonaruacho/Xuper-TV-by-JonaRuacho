# Xuper TV

Cliente homebrew nativo para Nintendo Switch. Proyecto comunitario en C++17,
libnx, SDL2 y libmpv. Version del codigo: **0.14.3**.

## Descargar para usar en Switch

Ve a [**Releases**](https://github.com/samuel3mn2/Xuper-TV-by-JonaRuacho/releases/latest) y descarga el ZIP terminado en **_Switch.zip**. Copia su
carpeta `switch` a la SD y abre hbmenu con memoria completa. Consulta
[INSTALAR.md](INSTALAR.md). El primer inicio genera una identidad local propia;
no necesitas compilar ni configurar desde una APK para este paquete.

## Funciones

- Carga automatica del catalogo al iniciar.
- TV en vivo, peliculas, series y buscador unificado.
- Interfaz con categorias, logos y portadas PNG/JPEG/WebP en cache.
- Favoritos persistentes, continuar viendo y opciones de reproduccion.
- Audio, subtitulos, fuentes/calidades y navegacion de episodios segun los
  datos y formatos compatibles que entregue el servicio.
- Recientes: lo ultimo que viste y el capitulo y minuto donde te quedaste.

El usuario confirma reproduccion de TV, peliculas y series con la base 0.13.
La interfaz 0.14 paso pruebas locales; requiere validacion en consola. Consulta
[VALIDACION.md](VALIDACION.md) y [RANGER.md](RANGER.md) para los limites conocidos.

## Contenido del repositorio

Se publica el codigo del cliente, herramientas, pruebas sinteticas y avisos de
licencia. No incluye APK, bibliotecas Android propietarias, archivos de cuenta,
identidad de instalacion, registros ni catalogos del usuario. La plantilla
publica contiene parametros comunes de conexion, no datos de cuenta. Tampoco incorpora un servicio de television o una suscripcion.

## Compilar

Instala devkitPro/devkitA64, libnx y las dependencias descritas en
[BUILD.md](BUILD.md). Se necesitan SDL2, SDL2_ttf, libmpv con render API, FFmpeg,
cURL, mbedTLS, libpng, libjpeg y libwebp para Switch.

```sh
DEVKITPRO=/opt/devkitpro make -j2
```

El resultado es `xuperswitch.nro`. Las bibliotecas de Linux/Windows no sirven
para enlazar el ejecutable de Switch. Este repositorio no promete una compilacion
con cualquier variante de libmpv; BUILD.md documenta las versiones utilizadas.

## Configuracion local

La configuracion real se genera localmente, a partir de tu propia copia compatible:

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/configure_from_apk.py /ruta/a/tu.apk --output private/service.json
```

Tambien se incluye `service.example.json` como referencia de campos; sus valores
son ficticios y no conectan con ningun proveedor. No publiques el archivo generado.
Copia tu configuracion a `sdmc:/switch/xuperswitch/service.json` y proporciona
`font.ttf` y un paquete CA actualizado en `cacert.pem`. Puedes usar DejaVu Sans,
conservando su licencia incluida. Abre hbmenu con memoria completa: mantener R
al iniciar un juego. No borres `device_state.json` al actualizar.

## Pruebas locales

En Debian/Ubuntu instala compilador C++17, make, pkg-config, libcurl4-openssl-dev,
libmbedtls-dev, python3 y las dependencias Python de pruebas. Luego:

```sh
python3 -m pip install -r tests/requirements.txt
make -f Makefile.host test test-ranger test-errors test-state test-saved test-service
```

Las pruebas usan localhost y datos sinteticos. Los ensayos graficos requieren
ademas SDL2/SDL2_ttf, libmpv, PNG/JPEG/WebP, Pillow y una fuente; los archivos
`tests/ui_artwork_probe.cpp`, `tests/test_artwork_ui.py` y
`tests/test_playback_http.py` contienen las pruebas del renderer persistente.

## Colaborar

Los forks, issues y pull requests son bienvenidos. Consulta
[CONTRIBUTING.md](CONTRIBUTING.md), [SECURITY.md](SECURITY.md) y
[ROADMAP.md](ROADMAP.md). Mantén el credito original y los avisos de licencia.
No envies contrasenas, claves de sesion, URLs firmadas ni archivos personales.

## Licencias y creditos

El codigo original del cliente se publica bajo MIT, segun [LICENSE](LICENSE).
Las dependencias tienen sus propias licencias, que se conservan en `licenses/`,
`LICENSE.nlohmann-json`, `COPYRIGHT.mpv` y [DEPENDENCIAS.md](DEPENDENCIAS.md).
La licencia del cliente no cambia las obligaciones de esas bibliotecas. Antes
de distribuir un binario enlazado, revisa las licencias y fuentes de la variante
exacta de libmpv/FFmpeg y demas dependencias que hayas utilizado.

Fork comunitario, basado en el proyecto original de Jona Ruacho (licencia MIT). No es una aplicacion oficial de Nintendo ni del
proveedor Android. No se publica la APK ni sus bibliotecas propietarias.
