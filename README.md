# Minihttpd

Servidor HTTP mínimo en C para servir archivos estáticos desde `www/`.

# Funcionamiento
epoll_wait espera eventos. Si el socket de escucha está listo, se aceptan los clientes pendientes (accept), cada uno se pone en modo no bloqueante y se registra en epoll.
Cuando un cliente tiene datos, recv los guarda en su buffer. TCP no respeta límites de mensaje, así que los bytes se acumulan hasta encontrar \r\n\r\n.
http_parse_request analiza la petición. Si faltan datos, se espera a que lleguen más.
Si el método es GET, files_open resuelve y abre el archivo, y mime_type_for_path elige el Content-Type.
Se envía la cabecera con send y el archivo con sendfile. Si el socket se llena (EAGAIN), el cliente pasa a esperar EPOLLOUT y la respuesta continúa donde se quedó, sin bloquear a los demás.
Con conexión persistente el socket sigue abierto esperando otra petición; si no, se cierra.
epoll se usa en modo level-triggered (el predeterminado): epoll_wait avisa mientras haya datos por leer o espacio para escribir.

## Uso
```sh
make 
./minihttpd 8080
```
## Pruebas
U=http://127.0.0.1:8080

curl -i  $U/                     # 200 text/html
curl -i  $U/style.css            # 200 text/css
curl -I  $U/image.png            # 200 image/png
curl -i  $U/no-existe            # 404
curl -i -X POST $U/              # 405 con "Allow: GET"
# abrir 3 conexiones y pedir un archivo por una de ellas -> 500
(ulimit -n 8; ./minihttpd 8080 &)
# Directory traversal (--path-as-is evita que curl simplifique los "..")
curl -i --path-as-is $U/../../../../../../../../../../../../etc/passwd   # 403
curl -i --path-as-is $U/%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/%2e%2e/etc/passwd   # 403

# Peticiones inválidas -> 400
printf 'GET / HTTP/1.1\r\n\r\n' | nc 127.0.0.1 8080                      # falta Host
printf 'GET / HTTP/2.0\r\nHost: x\r\n\r\n' | nc 127.0.0.1 8080          # versión no soportada
curl -i "$U/%00"                                                          # byte nulo
curl -i "$U/$(python3 -c 'print("a"*3000)')"                              # URI demasiado larga

# Conexión persistente: segunda petición en la misma conexión
curl -v $U/ $U/style.css 2>&1 | grep "Re-using"

# Tipos MIME
Extensión	Content-Type
.html	text/html
.css	text/css
.js	application/javascript
.png	image/png
.jpg	image/jpeg
otra o sin extensión	application/octet-stream
```
