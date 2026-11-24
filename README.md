# plumemu

Emulador de las consolas de 8 y 16 bits de Sega y Nintendo. Junta en un solo repositorio
los dos emuladores que antes vivían aparte, con toda su historia:

| Carpeta | Programa | Consolas | Antes en |
|---|---|---|---|
| [`sega/`](sega/README.md) | plumsg | Mega Drive, 32X, Mega CD, Master System y Game Gear | [cochimbo/plumsg](https://github.com/cochimbo/plumsg) |
| [`nintendo/`](nintendo/README.md) | plumnin | Game Boy, Game Boy Color, NES y Super Nintendo | [cochimbo/plumnin](https://github.com/cochimbo/plumnin) |

Siguen siendo dos programas, cada uno con su instalador, sus ajustes y sus carpetas de
datos de siempre (`plumsg/plumsg` y `plumnin/plumnin`), pero el código que comparten
está una sola vez en [`common/`](common/CMakeLists.txt) (namespace `plum`): la ventana y
su bucle (`common/src/frontend/main.cpp`), buena parte de la interfaz, los ajustes, la
red, los logros, el sonido General MIDI, las grabaciones, el rebobinado, la vista 3D,
los textos y sus traducciones, y el runner headless con sus tests (`tests/common`).

En `sega/` y `nintendo/` queda lo de cada uno: los núcleos de sus consolas, su consola
para la ventana (`frontend/app_console.h`) y lo que solo él hace en ella
(`frontend/app_main.cpp`: el sonido Roland, la Partitura y las pistolas de plumsg; el
sintetizador y el firmware de plumnin), su biblioteca con ScreenScraper, su editor de
instrumentos y sus textos propios. Lo que cada programa pone de suyo (nombre, versión,
carpeta de datos) lo dice su `AppInfo` (`sega/src/app_info.cpp`,
`nintendo/src/app_info.cpp`). Las bibliotecas de terceros que usan los dos están en
`third_party/`.

## Compilar

Los dos a la vez, desde la raíz:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Los ejecutables salen en `build/sega/` (`plumsg`, `plumsg-headless`...) y en
`build/nintendo/` (`plumnin`, `plumnin-headless`...). Opciones:

| Opción | Por defecto | Qué hace |
|---|---|---|
| `PLUM_BUILD_SEGA` | `ON` | Compila plumsg |
| `PLUM_BUILD_NINTENDO` | `ON` | Compila plumnin |
| `PLUM_BUILD_FRONTEND` | `ON` | Los programas con ventana (necesitan SDL2 y OpenGL); en `OFF`, solo los núcleos, los headless y los tests |
| `PLUM_BUILD_TESTS` | `ON` | Los programas de prueba |
| `PLUM_FETCH_SDL2` | `ON` | Descarga y compila SDL2 si no está instalado |

Cada carpeta se sigue pudiendo compilar sola (`cmake -S sega -B build-sega`), con sus
opciones `PLUMSG_*` o `PLUMNIN_*` de siempre.

## Versiones

Cada programa se publica con sus propias etiquetas: `plumsg-v1.2.0` publica plumsg y
`plumnin-v0.4.0-alpha` publica plumnin (instalador de Windows y AppImage).

## Comprobar que nada cambia

[`tools/reference`](tools/reference/README.md) guarda cómo se ven y cómo suenan una
serie de ejecuciones de los dos headless antes de juntarlos, y la CI lo compara en cada PR.
