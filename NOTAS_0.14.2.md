# Xuper TV 0.14.2 — corrección de lentitud en series

Fork comunitario basado en la 0.14.1 de JonaRuacho.

## Corregido
- El video ya no se pone lento poco a poco al ver capítulos largos. Antes había que cerrar y volver a abrir el capítulo.

## Cambios en el reproductor
- El decodificador ahora puede descartar cuadros para alcanzar al audio en vez de irse atrasando.
- Decodificación H.264 más rápida: 4 hilos, modo rápido y sin filtro de bloques. La diferencia no se nota a 640x360.
- El búfer hacia atrás queda limitado a 8 MiB (antes 50 MiB por defecto).
- Solo se dibuja cuando hay un cuadro nuevo, y los textos del menú se reutilizan.
- Recuperación automática: si el video se desfasa, se resincroniza. Si sigue lento o se queda cargando, pide de nuevo la fuente y continúa en el mismo punto.
- Opción nueva en el menú (X/+): **Recargar video (si va lento)**.

## Instalar
Copia la carpeta `switch` del ZIP a la SD (reemplaza `xuperswitch.nro`). No borres `device_state.json`. Abre hbmenu con memoria completa (mantén R sobre un juego).
