# minihttpd

Servidor HTTP mínimo en C para servir archivos estáticos desde `www/`.

## Uso

```sh
make 
./minihttpd 8080
```
## Pruebas

curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/style.css
curl -i http://127.0.0.1:8080/no-existe
curl -i -X POST http://127.0.0.1:8080/
curl -i --path-as-is http://127.0.0.1:8080/../../../../etc/passwd
```
