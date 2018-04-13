Simple name server implementation to connect services that are executing in different machines. Service of UDP broadcasting with neighbour discovery:
  - zmq library is used to send the messages with the info about each service.
  - all data send by the network (excluding broadcast messages) is serialized using flatbuffers library.
  - broadcast messages are sent with WINSOCK library

