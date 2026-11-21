# MiniHTTPd

Servidor HTTP básico desarrollado en C para Linux. MiniHTTPd atiende múltiples
clientes mediante `epoll`, sirve archivos estáticos desde `www/` y procesa
solicitudes `GET` de HTTP/1.1 sin utilizar bibliotecas HTTP externas.

## Características

- Servidor TCP con sockets no bloqueantes.
- Multiplexación de conexiones mediante `epoll`.
- Conexiones HTTP persistentes.
- Procesamiento del método `GET`.
- Encabezados `Host`, `Connection` y `User-Agent`.
- Respuestas `200`, `400`, `403`, `404`, `405` y `500`.
- Tipos MIME para HTML, CSS, JavaScript, PNG y JPG.
- Protección de rutas con percent-decoding y `realpath()`.
- Límites de tamaño para solicitudes, URI y encabezados.
- Envío de archivos con `sendfile()` y manejo de escrituras parciales.

## Requisitos

- Ubuntu 24.04 o una distribución Linux compatible.
- Compilador C compatible con C11.
- Utilidades básicas de compilación.

## Estructura

```text
minihttpd/
├── Makefile
├── README.md
├── include/
│   ├── files.h
│   ├── http.h
│   ├── mime.h
│   └── server.h
├── src/
│   ├── files.c
│   ├── http.c
│   ├── main.c
│   ├── mime.c
│   └── server.c
└── www/
    ├── index.html
    ├── style.css
    └── image.png
```

- `main.c`: bucle de eventos `epoll`, clientes y envío de respuestas.
- `server.c`: creación del socket TCP y configuración no bloqueante.
- `http.c`: análisis de solicitudes y construcción de respuestas HTTP.
- `files.c`: resolución segura y apertura de archivos estáticos.
- `mime.c`: selección del tipo de contenido según la extensión.

## Compilación

Desde la carpeta del proyecto:

```sh
make
```

Para recompilar todos los archivos:

```sh
make -B
```

## Ejecución

```sh
./minihttpd 8080
```
## Pruebas

```sh
curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/style.css
curl -i http://127.0.0.1:8080/image.png
curl -i http://127.0.0.1:8080/archivo-inexistente
curl -i -X POST http://127.0.0.1:8080/
```

Respuestas esperadas:

| Solicitud | Respuesta |
| --- | --- |
| Página principal | `200 OK` |
| CSS | `200 OK`, `text/css` |
| PNG | `200 OK`, `image/png` |
| Archivo inexistente | `404 Not Found` |
| Método distinto de `GET` | `405 Method Not Allowed` |

```sh
curl -i --path-as-is \
  http://127.0.0.1:8080/../../../../etc/passwd
```

Una ruta que intenta salir de `www/` debe responder `403 Forbidden`.

Para probar una solicitud incompleta:

```sh
printf 'GET / HTTP/1.1\r\n\r\n' | nc 127.0.0.1 8080
```

Una solicitud HTTP/1.1 sin `Host` debe responder `400 Bad Request`.

## Tipos MIME

| Extensión | Tipo de contenido |
| --- | --- |
| `.html` | `text/html` |
| `.css` | `text/css` |
| `.js` | `application/javascript` |
| `.png` | `image/png` |
| `.jpg` | `image/jpeg` |
| Otra o sin extensión | `application/octet-stream` |

## Funcionamiento

El socket de escucha se registra en `epoll` cuando hay conexiones pendientes,
el servidor las acepta y las convierte en sockets no bloqueantes.

Cada cliente conserva su propio estado y buffer de entrada. Los bytes recibidos
se mantienen hasta encontrar una solicitud completa, el parser verifica la línea
de solicitud, los encabezados básicos, el método y los límites configurados.

Las respuestas se guardan en buffers por cliente, los encabezados y los archivos
se pueden enviar en varias operaciones si el socket no admite todos los bytes de
una vez, una respuesta puede continuar cuando epoll informa disponibilidad de
escritura, sin bloquear a los demás clientes.

## Seguridad

El servidor limita el tamaño de las solicitudes y rechaza URI, encabezados o
solicitudes mal formadas. Solo acepta `GET`.

Las rutas públicas se resuelven desde `www/` y se comprueba que el archivo
final permanezca dentro de esa raíz mediante `realpath()`. 
