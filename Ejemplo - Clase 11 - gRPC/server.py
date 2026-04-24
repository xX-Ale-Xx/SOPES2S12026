import grpc
from concurrent import futures
import hello_pb2
import hello_pb2_grpc

class Greeter(hello_pb2_grpc.GreeterServicer):
    def SayHello(self, request, context):
        # Logica para manejar la solicitud de saludo
        return hello_pb2.HelloReply(message=f"Hola, {request.name} desde el servidor Python!")

def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    hello_pb2_grpc.add_GreeterServicer_to_server(Greeter(), server)
    server.add_insecure_port('[::]:50051') # Escuchar en el puerto 50051

    print("Servidor gRPC escuchando en el puerto 50051...")

    server.start()
    server.wait_for_termination() # Mantener el servidor en ejecución

if __name__ == '__main__':
    serve()