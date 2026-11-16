# minihttpd

Servidor TCP minimo para estudiar el establecimiento de conexiones. En este
primer hito crea un socket IPv4, escucha en un puerto, acepta conexiones y las
cierra. Todavia no procesa HTTP ni usa `epoll`.

## Compilar

```sh
make
```

## Ejecutar

```sh
./minihttpd 8080
```

En otra terminal, conectate para comprobar que el servidor acepta al cliente:

```sh
nc 127.0.0.1 8080
```

El servidor imprimira `Cliente conectado` y cerrara esa conexion. El siguiente
hito sera registrar el socket de escucha en `epoll` y aceptar clientes desde el
bucle de eventos.