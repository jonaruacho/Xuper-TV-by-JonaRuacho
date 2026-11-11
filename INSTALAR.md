# Instalar Xuper TV

Entra a Releases y descarga **Xuper_TV_0.14.3_Switch.zip**.
Descomprimelo y copia la carpeta `switch` completa a la raiz de tu tarjeta SD.
Abre hbmenu con memoria completa, manteniendo R al iniciar un juego, y abre
XuperSwitch. No necesitas compilar ni instalar Python para usar este paquete.

La primera vez genera y guarda una identidad propia en `service.json`.
Necesita Internet y autorizacion del servicio; no incluye suscripcion ni omite
rechazos del servidor. Los parametros comunes van en `service.template.json`,
que no contiene identidad ni datos personales de otra instalacion.

Al actualizar, conserva `service.json`, `device_state.json`, `playback_state.json`,
`library_state.json` y las copias `.bak`. El ZIP no incluye esos archivos.
Si existe activacion pero falta `service.json`, restaura tu copia anterior: el
cliente no crea otra identidad para sustituir una activacion existente.

Los archivos llamados Source code y el ZIP Fuentes son para desarrolladores.
La interfaz y el primer arranque nuevos requieren validacion en consola.
