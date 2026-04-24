# Semana 11: SISTEMAS ESPECIALIZADOS Y DISTRIBUIDOS
# Implementacion gRPC con Python para la comunicación entre un cliente y un servidor.

---

## Instalación de gRPC y python


* Instalar gRPC y herramientas necesarias para Python:

```bash
sudo apt update
sudo apt install python3-pip

# Crear un entorno virtual para el proyecto
python3  -m pip venv venv
source venv/bin/activate

pip install -r requirements.txt
```

* Generar el código de gRPC a partir del archivo .proto:

```bash
python -m grpc_tools.protoc -I. --python_out=. --grpc_python_out=. hello.proto
```

* Ejecutar el servidor gRPC:

```bash
python server.py
```

* Ejecutar el cliente gRPC:

```bash
python client.py
```

## Ventajas de gRPC

1. Python se encarga de la serialización y deserialización de los mensajes, lo que simplifica el desarrollo.
2. gRPC utiliza HTTP/2, lo que permite una comunicación eficiente y de baja latencia entre el cliente y el servidor.
3. Python soporta asyncio nativamente para gRPC, lo que permite manejar miles de conexiones simultáneas de forma eficiente sin complicar el código.
4. Es ideal para prototipos de Sistemas Distribuidos o microservicios donde el tiempo de desarrollo es crítico.