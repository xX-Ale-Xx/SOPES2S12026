import grpc
import hello_pb2
import hello_pb2_grpc

def run():
    # Crear el canal de comunicación
    with grpc.insecure_channel('localhost:50051') as channel:
        # Crear el stub (el cliente)
        stub = hello_pb2_grpc.GreeterStub(channel)
        
        # Realizar la llamada RPC
        response = stub.SayHello(hello_pb2.HelloRequest(name='Estudiante USAC'))
        
    print(f"Respuesta recibida: {response.message}")

if __name__ == '__main__':
    run()

    