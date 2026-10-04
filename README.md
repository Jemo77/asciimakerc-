# ascii-maker

Convierte una imagen —o un GIF, o un video— en arte ASCII con color RGBA, en
una interfaz gráfica de Qt6. Cada píxel produce un carácter; se puede retocar
celda a celda y exportar a **WebP** (fijo o animado), **PNG**, **TXT** o
**HTML**.

## Requisitos

- CMake ≥ 3.16
- Un compilador C++17
- Qt6 con los módulos `Widgets` y `Multimedia`, y su herramienta `moc`
- `ffmpeg` (solo para escribir el WebP animado)

En Debian/Ubuntu:

```bash
sudo apt install cmake g++ qt6-base-dev qt6-multimedia-dev ffmpeg
```

Para exportar a WebP fijo hace falta además el plugin de formatos de imagen:

```bash
sudo apt install qt6-image-formats-plugins
```

Sin él, la exportación a WebP avisa y guarda PNG en su lugar.

`ffmpeg` se detecta en tiempo de compilación. Si no está, el binario sigue
funcionando para imagen fija y GIF, pero avisa de que no puede escribir
animaciones.

## Compilar

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
./ascii-maker
```

O en un solo paso, con el script `build.sh`.

## Uso

| Acción | Atajo |
|---|---|
| Abrir imagen, GIF o video | `Ctrl+O` |
| Exportar | `Ctrl+E` |
| Encajar la vista | botón *Encajar* |
| Zoom | rueda del ratón, o `+` / `−` / `1:1` |
| Reproducir / pausar | botón del panel *Animación* |
| Salir | `Ctrl+Q` |

`Ctrl+O` acepta imágenes, `.gif` y video (`mp4`, `m4v`, `mov`, `webm`,
`mkv`, `avi`, `wmv`, `ogv`…). Se abre lo que sea y el arte aparece a la
derecha, con el original a la izquierda para comparar. El dock **Ajustes**
lleva los controles:

- **Rampa** — los caracteres, de más oscuro a más claro. Por defecto
  `" .:-=+*#%@"`. El primer carácter suele ser el espacio, para que los
  negros no se pinten. Es editable en caliente: cualquier texto vale.
- **Invertir luminancia** — invierte la rampa, útil para texto claro sobre
  fondo oscuro.
- **Auto-niveles** — estira el rango tonal útil (percentiles 0,5 / 99,5).
- **Brillo / Contraste** — ajustes directos sobre el valor.
- **Alfa** — «Transparente → espacio» convierte en espacio todo lo que esté
  por debajo del umbral.
- **Escala** — reescala la imagen antes de convertirla.
- **Fuente** — tamaño en puntos. Afecta al preview, al PNG/WebP y al HTML.
- **Aspecto** — cuánto más alto es un carácter que ancho (ver abajo).
- **Fondo** — el color de fondo del render.
- **Compresión** — sin pérdida y calidad, solo para exportar (ver abajo).
- **Topes de tamaño** — columnas, filas, proporción y frames (ver abajo).
- **Animación** — solo aparece con un GIF o un video cargado.

### Compresión

Dos controles que solo afectan a la exportación; la vista previa siempre
muestra el arte sin comprimir.

- **Sin pérdida** — *lossless*: el WebP guarda los píxeles tal cual y no
  difumina los caracteres. Desmarcado: WebP con pérdida, bastante más
  pequeño pero con los bordes degradados.
- **Calidad** — de 1 a 100. En modo con pérdida es la calidad de verdad. En
  modo sin pérdida la calidad de imagen no existe, pero en la animación el
  valor sí se usa como **esfuerzo de compresión**: más alto, archivo más
  pequeño.

El PNG ya es siempre sin pérdida, así que el interruptor no le afecta; se
comprime a tope siempre.

Medido con una imagen de 736×552 a 200 columnas:

| Modo | Calidad | Tamaño | Chunk RIFF |
|---|---|---|---|
| con pérdida | 92 % | 63.412 B | `VP8` |
| con pérdida | 50 % | 12.300 B | `VP8` |
| con pérdida | 10 % | 3.406 B | `VP8` |
| sin pérdida | — | 61.182 B | `VP8L` |
| PNG | — | 108.542 B | — |

Dos rarezas de Qt que condicionan el código:

- El handler de PNG de Qt 6.11 **ignora `setCompression()`**: del 0 al 9 da
  el mismo archivo. Lo que funciona es `setQuality()`, con la escala
  **invertida** (más calidad = menos compresión). Antes se usaba calidad 92
  para todo, y por eso los PNG salían de 5,2 MB en vez de 108 KB.
- El writer de WebP solo cambia a `VP8L` (sin pérdida) con calidad
  **exactamente 100**; con 99 sigue escribiendo `VP8` con pérdida. Por eso
  el interruptor marca 100 y no el valor del deslizador.

Y una trampa en la parte de ffmpeg: **no se pasa `-preset`**. Un preset
reescribe la configuración y deja `-lossless 1` en 0 sin avisar; el archivo
sale con pérdida aunque se pida sin pérdida. Está comprobado en un comentario
del código, porque es muy fácil de "mejorar" sin querer y romper.

Curiosidad sobre el tamaño en la animación: para este tipo de arte el modo
sin pérdida sale **ligeramente más pequeño** que el con pérdida (1.100.218 B
frente a 1.124.982 B). El fondo negro plano se comprime muchísimo, mientras
que la compresión con pérdida mete ruido en los bordes de los caracteres, y
ese ruido es justo lo caro de codificar. En una foto, al revés: la sin
pérdida pesa del orden de 4× más.

### Topes de tamaño

Sin topes el arte es **1 carácter por píxel, exacto**. Un tope reescala la
imagen, así que a partir de ahí el arte deja de ser 1:1 con la original:

| Tope | Efecto |
|---|---|
| **Columnas** | ancho máximo del arte, en caracteres |
| **Filas** | alto máximo de la rejilla 1:1 |
| **Mantener proporción** | cómo se combinan los dos topes anteriores |
| **Frames** | frames máximos de la animación (ver abajo) |

El **tope de frames recorta la vista previa al vuelo**: el deslizador, la
reproducción, la barra de estado y la exportación usan solo los frames que
quedan dentro del tope, así que se ve el efecto sin reabrir el video. El
panel de animación dice «Usando 8 de 30 frames» y avisa si el recorte está
activo. Si subes el tope por encima de los frames capturados no hay nada que
estirar, así que avisa de que hay que reabrir el archivo. Durante la captura
del video el tope también corta, que es la forma de no esperar minutos.

**Mantener proporción** (marcado por defecto) hace que los topes se
linkeen por el más restrictivo y la imagen solo se encoge, sin deformarse.
Desmarcado, cada tope manda por su lado y el arte se estira o se aplasta
hasta clavarse en la rejilla pedida:

| Tope | Marcado | Desmarcado |
|---|---|---|
| ninguno | 736×552 | 736×552 |
| 200 columnas | 200×150 | 200×552 |
| 100 filas | 133×100 | 736×100 |
| 200 × 100 | 133×100 | 200×100 |
| 120 × 60 | 80×60 | 120×60 |
| 80 × 24 | 32×24 | 80×24 |

Lo de 80×24 es el caso útil: clavas el arte en una rejilla de terminal,
aunque la imagen original se deforme. Ojo con dejar un tope sin poner: ese
eje no se restringe, así que desmarcado deforma igualmente (por eso
«columnas = 200» sin filas sale 200×552).

Los topes de columnas y filas valen igual para imágenes, GIF y video.

Esto importa sobre todo con el video, porque sin topes un clip de 1080p son
2 millones de celdas por frame, unos 16 MB de RAM cada una.

### GIF y video

Un **GIF** se lee entero con `QMovie` y se convierte frame a frame; cada
frame conserva su propio retardo, así que la animación mantiene el ritmo
del original. Es instantáneo: 30 frames de 240×180 se convierten en unos
150 ms.

Un **video** se abre con `QMediaPlayer` y se captura en **tiempo real**: el
player va soltando frames y cada uno se congela mientras se convierte, para
que no se pierda ninguno. La contrapartida es que un clip de 2 minutos tarda
2 minutos en convertirse. Para ceñirte a un trozo corto, pon el tope de
frames antes de abrirlo.

El panel **Animación** muestra cuántos frames se usan (y cuántos se
capturaron, si hay recorte por el tope), el retardo medio por frame, cuántas
celdas ocupa cada uno y cuánta memoria ocupan en total. Desde ahí se
reproduce, se pausa y se recorre frame a frame con el deslizador.

Las ediciones de celda se aplican al frame que esté en pantalla, no a toda
la animación.

### Exportar

`Ctrl+E` y se elige la extensión. Con una imagen fija:

- **`.webp` / `.png`** — render del arte con la fuente elegida. Respeta el
  fondo y el tamaño de fuente.
- **`.txt`** — solo los caracteres, un píxel por carácter y una línea por
  fila de la imagen. Sin color.
- **`.html`** — documento autocontenido con el arte coloreado, en la misma
  fuente y tamaño que el preview.

Con una animación cargada:

- **`.webp`** — **WebP animado**. Cada frame se renderiza a PNG en un
  temporal y `ffmpeg` los ensambla con `libwebp_anim`. Conserva el canal
  alfa y no pierde color.
- **`.png`** — solo el frame que esté en pantalla.

Qt 6.11 no tiene API de escritura multiframe (`QImageWriter` solo escribe
una imagen), que es exactamente por lo que hace falta `ffmpeg` aquí.

## El aspecto del carácter

Un carácter monoespaciado **no es un cuadrado**: ocupa bastante más alto que
ancho. En la Noto Sans Mono de un sistema Debian habitual la celda mide
`6,59 × 14,97 px`, o sea **2,27× más alta que ancha**.

Por eso hay dos rejillas distintas:

| | Qué es | Para qué |
|---|---|---|
| **1:1** | un carácter por píxel, `ancho × alto` | exportación a `.txt`, y donde se guardan las ediciones |
| **de dibujo** | la 1:1 con las filas promediadas en bloques de `aspecto` | preview, PNG/WebP y HTML |

Sin esta corrección el arte sale estirado verticalmente. El campo **Aspecto**
es el divisor de filas, y por defecto se inicializa midiendo la fuente real
en vez de poner un valor a ciegas. Con `2,00` se aplica el valor clásico del
género; con `1,00` se desactiva la corrección.

Con una imagen de 736×552 a 8 pt:

| Aspecto | Rejilla de dibujo | PNG | Proporción | Δ |
|---|---|---|---|---|
| 1,00 | 736×552 | 4853×8263 | 0,587 | −55,9% |
| 2,00 | 736×276 | 4853×4132 | 1,175 | −11,9% |
| **2,27** | **736×243** | **4853×3638** | **1,334** | **+0,05%** |

Las ediciones manuales nunca se promedian: si un píxel del bloque está
editado a mano, ese carácter gana tal cual.

Ojo con el **tamaño absoluto**: el PNG exportado sigue siendo más grande que
la imagen original (4853 px de ancho contra 736), porque a 8 pt cada carácter
ocupa 6,6 px reales. Eso es independiente del aspecto; para reducirlo, baja
el tamaño de fuente o la escala.

## Estructura

```
main.cpp        todo el programa: AsciiMaker, AsciiItem, AsciiView, MainWindow
CMakeLists.txt  proyecto de un solo ejecutable
build.sh        script de compilación
```

Dentro de `main.cpp`:

- **`AsciiCell`** — un carácter con su RGBA y un flag de edición manual.
- **`AsciiGrid`** — una rejilla de celdas: la 1:1 y la de dibujo.
- **`AsciiMaker`** — la conversión y los ajustes. `rebuild()` produce la
  rejilla 1:1 aplicando rampa, brillo, contraste, auto-niveles, alfa,
  escala y topes. `construirVista(base)` produce la de dibujo a partir de
  cualquier rejilla 1:1, promediando filas. `viewToSource()` /
  `sourceToView()` traducen coordenadas entre ambas.
- **`AsciiItem`** — el `QGraphicsItem` que pinta el arte. Recibe un
  `const AsciiGrid*`, no el `AsciiMaker`, así el dibujo es una instantánea.
- **`AsciiView`** — el `QGraphicsView` con zoom y clic por celda.
- **`MainWindow`** — la interfaz, los ajustes y la exportación. Lleva un
  `QVector<Frame>`: una imagen fija es un vector de un frame, y un GIF o un
  video son N. Todas las operaciones (dibujar, editar, exportar) trabajan
  sobre `baseActual()`, así que no hay dos caminos de código distintos para
  imagen fija y animación.

## Notas

- La rampa por defecto es `" .:-=+*#%@"`, de más oscuro a más claro.
- El prototipo original en Python ya no está en el repo; este proyecto es
  autónomo y no depende de él.
- En una animación el retardo por frame se promedia al exportar, porque
  `ffmpeg` no admite delays distintos por frame en la entrada. Con GIF, que
  sí los tiene, el ritmo se pierde un poco al exportarlo a WebP.
- Ninguna salida lleva metadatos: ni EXIF, ni XMP, ni perfil ICC. No es que
  se descarten al exportar, es que el render parte de un `QImage` nuevo y
  nunca se copian.
