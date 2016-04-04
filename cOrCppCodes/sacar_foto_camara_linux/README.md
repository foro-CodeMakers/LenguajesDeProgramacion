## Descripcion

Programa para Linux que saca una foto con una camara conectada a la pc.

## Compilar
Necesitan tener la libreria de ffmpeg (http://www.ffmpeg.org), yo use la version 3.0

Para compilar, ejecutar

make clean all

## Uso

./camara -v dispositivo -w ancho -h alto -o salida.jpg

Ejemplo

./camara -v /dev/video0 -w 800 -h 699 -o foto.jpg

siendo todos los parametros opcionales, en caso de no usar los parametros, por defecto usa el dispositivo /dev/video0, ancho 1280, alto 720, y salida salida_camara.jpg

## Post en foro.code-makers.es

El post con toda la explicacion esta en http://foro.code-makers.es/viewtopic.php?f=11&p=235#p235

