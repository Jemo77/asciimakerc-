# ascii-maker

Convierte una imagen en arte ASCII con color RGBA, en una interfaz gráfica de
Qt6. Cada píxel de la imagen produce un carácter; se puede retocar celda a
celda y exportar a **WebP**, **PNG**, **TXT** o **HTML**.

## Requisitos

- CMake ≥ 3.16
- Un compilador C++17
- Qt6 con el módulo `Widgets` y su herramienta `moc`

En Debian/Ubuntu:

```bash
sudo apt install cmake g++ qt6-base-dev
```

Para exportar a WebP hace falta además el plugin de formatos de imagen:

```bash
sudo apt install qt6-image-formats-plugins
```

Sin él, la exportación a WebP avisa y guarda PNG en su lugar.

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
| Abrir imagen | `Ctrl+O` |
| Exportar | `Ctrl+E` |
| Encajar la vista | botón *Encajar* |
| Zoom | rueda del ratón, o `+` / `−` / `1:1` |
| Salir | `Ctrl+Q` |

Se abre una imagen y el arte aparece a la derecha, con el original a la
izquierda para comparar. El dock **Ajustes** lleva los controles:

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

### Editar celdas

Clic en cualquier carácter del arte para seleccionarlo y abrir el editor
**Celda seleccionada**: carácter, R, G, B y A. *Aplicar* fija el cambio.
*Restaurar* deshace la edición de esa celda y *Limpiar* las deshace todas.

Las ediciones se guardan por índice de píxel, así que sobreviven a los
cambios de brillo, rampa, etc., mientras la imagen no cambie de tamaño.

### Exportar

`Ctrl+E` y se elige la extensión:

- **`.webp` / `.png`** — render del arte con la fuente elegida. Respeta el
  fondo y el tamaño de fuente.
- **`.txt`** — solo los caracteres, un píxel por carácter y una línea por
  fila de la imagen. Sin color.
- **`.html`** — documento autocontenido con el arte coloreado, en la misma
  fuente y tamaño que el preview.

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
- **`AsciiMaker`** — la conversión. `rebuild()` produce la rejilla 1:1;
  `viewGrid()` produce la de dibujo, cacheada, promediando filas.
  `viewToSource()` / `sourceToView()` traducen coordenadas entre ambas.
- **`AsciiItem`** — el `QGraphicsItem` que pinta el arte. Recibe un
  `const AsciiGrid*`, no el `AsciiMaker`, así el dibujo es una instantánea.
- **`AsciiView`** — el `QGraphicsView` con zoom y clic por celda.
- **`MainWindow`** — la interfaz, los ajustes y la exportación.

## Notas

- La rampa por defecto es `" .:-=+*#%@"`, de más oscuro a más claro.
- El prototipo original en Python ya no está en el repo; este proyecto es
  autónomo y no depende de él.
