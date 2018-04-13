#include <iostream>
#include <string>
#include "flatbuffers/flatbuffers.h"
#include "server_generated.h"
#include <list>
#include <algorithm>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif // _WIN32

#include "zmq.hpp"

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif // _WIN32

#define PING_PORT_NUMBER    9999
#define REQUEST_PORT_NUMBER 2222
#define PING_MSG_SIZE       2
#define PING_INTERVAL       500
#define SOCKET_POLL_TIMEOUT 3000

#define INFO_OUT(MSG)  std::cout << "[INFO]   " << " " << (MSG) << std::endl
#define ERROR_OUT(MSG) std::cerr << "[ERROR]  " << " " << (MSG) << std::endl

#define LOCALHOST_EXECUTING 1

#ifndef _WIN32
#define SOCKET int
#define INVALID_SOCKET (SOCKET)(~0)
#define SOCKET_ERROR (SOCKET)(~1)
#define NO_ERROR 0
#endif // _WIN32

#include <windows.h>
#define sleep(n)    Sleep(n)
#define THREADCOUNT 2

struct service{
    std::string name;
    std::vector<std::string> endpoints;
};

struct nameServer{
    std::string name;
    std::string address;
    std::vector<service> mainList;

};


/* Simple name server implementation to connect services that are executing in different machines. Service of UDP broadcasting with neighbour discovery:
*
*	- zmq library is used to send the messages with the info about each service.
*   - all data send by the network (excluding broadcast messages) is serialized using flatbuffers library.
*   - broadcast messages are sent with WINSOCK library
*
*/

std::vector<std::string> detectedNodes;
std::vector<std::string> connectedNodes;
std::vector<nameServer> serverList;
nameServer mainServer;  /* TODO think about the posibility of including the own server in the serverList */

int getHostAddress(std::string *hostAddress, std::string *hostName)
{

    char ac[80];
    if (gethostname(ac, sizeof(ac)) == SOCKET_ERROR) {
        std::cerr << "Error " << WSAGetLastError() <<
            " when getting local host name." << std::endl;
        return -1;
    }
    *hostName = ac;

    struct hostent *phe = gethostbyname(ac);
    if (phe == 0) {
        ERROR_OUT("Bad host lookup.");
        return -1;
    }
    for (int i = 0; phe->h_addr_list[i] != 0; ++i) {
        struct in_addr addr;
        memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
        //std::cout << "Address " << i << ": " << inet_ntoa(addr) << std::endl;
        *hostAddress = inet_ntoa(addr);
    }

    return 0;
}

int openSocket(zmq::socket_t* socket_p, char* inputAddress)
{

    int64_t timeout = 10;
    zmq_setsockopt(socket_p, ZMQ_RCVTIMEO, &timeout, sizeof(timeout)); 
    socket_p->bind(inputAddress);

    return 0;
}

SOCKET initBroadcastSocket()
{
    
    WSADATA wsaData;
    int nResult = 0;

    // Initialize Winsock
    nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (nResult != NO_ERROR)
    {
        ERROR_OUT("WSAStartup failed");
        return -1;
    }

    // Create UDP socket
    SOCKET fdSocket;
    fdSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fdSocket == INVALID_SOCKET)
    {
        ERROR_OUT("Socket creation failed");
        return -1;
    }

    // Set up the sockaddr structure
    struct sockaddr_in saListen = {0};
    saListen.sin_family = AF_INET;
    saListen.sin_port = htons(PING_PORT_NUMBER);
    saListen.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the socket
    nResult = bind(fdSocket, (sockaddr*)&saListen, sizeof(saListen));
    if (nResult != NO_ERROR)
    {
        ERROR_OUT("Socket bind failed");
        return -1;
    }

    return fdSocket;
}

char* buildIpAddress(std::string ip_address)
{
    char* input = "tcp://";
    char* port_s = ":1111";  /*TODO*/

    std::string address_s = input + ip_address + port_s;
    char* final_address = new char[address_s.size()];

    std::strcpy(final_address, address_s.c_str());

    return final_address;
}

int doBroadcast()
{
    WSADATA wsaData;
    int nResult = 0;
    int nOptOffVal = 0;
    int nOptOnVal = 1;
    int nOptLen = sizeof(int);

    // Initialize Winsock
    nResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (nResult != NO_ERROR)
    {
        ERROR_OUT("broadcast : WSAStartup failed");
    }

    // Create UDP socket
    SOCKET fdSocket;
    fdSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fdSocket == INVALID_SOCKET)
    {
        ERROR_OUT("broadcast : socket creation failed");
    }

    // Ask operating system to let us do broadcasts from socket
    nResult = setsockopt(fdSocket, SOL_SOCKET, SO_BROADCAST, (char *)&nOptOnVal, nOptLen);
    if (nResult != NO_ERROR)
    {
        ERROR_OUT("broadcast : setsockopt SO_BROADCAST failed");
    }

    // Set up the sockaddr structure
    struct sockaddr_in saBroadcast = {0};
    saBroadcast.sin_family = AF_INET;
    saBroadcast.sin_port = htons(PING_PORT_NUMBER);
    saBroadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    // Broadcast 3 beacon messages
    for(int i = 0; i < 3; i++)
    {
        char buffer[PING_MSG_SIZE] = {0};
        strcpy(&buffer[0], "!");
        int bytes = sendto(fdSocket, buffer, PING_MSG_SIZE + 1, 0, (sockaddr*)&saBroadcast, sizeof(struct sockaddr_in));
        if (bytes == SOCKET_ERROR)
        {
            ERROR_OUT("broadcast : sendto failed");
            return -1;
        }
        //std::cout << "Sent broadcast N"<< i << " to the network, port " << PING_PORT_NUMBER << " successfully.\n";
        sleep(300);
    }
    closesocket(fdSocket);
    WSACleanup();

	INFO_OUT("Broadcasting 3 beacon messages to the network... done!");

    return 0;
}

int cleanExit(SOCKET socket_p)
{
    closesocket(socket_p);
    WSACleanup();

    return 0;
}

int handleServerInfo(serverbuffer::ServerInfo *serverInfoReceived)
{

    if( (std::find(detectedNodes.begin(), detectedNodes.end(), serverInfoReceived->address()->data()) == detectedNodes.end()))
    {
        detectedNodes.push_back(serverInfoReceived->address()->data());
    }

    nameServer auxServer;

    /* Create the new server */

    auxServer.name = serverInfoReceived->name()->data();
    auxServer.address = serverInfoReceived->address()->data();
    auto auxList = serverInfoReceived->serviceslist();

    for(auto it = auxList->begin(); it != auxList->end(); it++) /* Create the list of services */
    {
        service auxService;
        auto auxSimService = *it;

        auxService.name = auxSimService->name()->data();

         /*for(auto it2 = auxSimService->endpoint()->begin(); it2 != auxSimService->endpoint()->end(); it2++)
        {
            auxService.endpoints.push_back(it2->str());
        }
        auxServer.mainList.push_back(auxService);  */
    }
    serverList.push_back(auxServer);

    return 0;
}

int handleServiceRequest(serverbuffer::ServiceRequest *serviceRequestReceived)
{

    std::string keyService = serviceRequestReceived->serviceName()->data();

    for(auto it = serverList.begin(); it != serverList.end(); it++)
    {
        nameServer auxServer = *it;

        for(auto it2 = auxServer.mainList.begin(); it2 != auxServer.mainList.end(); it2++)
        {
            service auxService = *it2;
            if(auxService.name == keyService) std::cout << "Service requested was found in the database: " << keyService << std::endl;
        }
    }
    return 0;
}

int handleMessage(void *buffer_pointer)
{
    auto deserializedMessage = serverbuffer::GetRequest(buffer_pointer);

    if(deserializedMessage->value_type() == serverbuffer::RequestMessage::RequestMessage_ServerInfo) 
    {
       std::cout << "Just received a message with server info: ";
       auto serverInfoReceived = (serverbuffer::ServerInfo *) deserializedMessage->value();
       std::cout << serverInfoReceived->name()->data() << std::endl << std::endl;
       return handleServerInfo(serverInfoReceived);
    }
    if(deserializedMessage->value_type() == serverbuffer::RequestMessage::RequestMessage_ServiceRequest)
    {
        std::cout << "Just received a message with a service request from: ";
        auto requestMessageReceived = (serverbuffer::ServiceRequest *) deserializedMessage->value();
        std::cout << requestMessageReceived->serviceName()->data() << std::endl << std::endl;
        return handleServiceRequest(requestMessageReceived);
    }
    return -1;
}

int handleBroadcast(SOCKET socket_broadcast, sockaddr_in receiveFrom)
{
    char recvBuf[160] = {0};  /* TODO: Justify why we have this initialization of the vector. ¿Size of the UDP datagram? */
    int saSize = sizeof(struct sockaddr_in);

    size_t size = recvfrom(socket_broadcast, recvBuf, 1000, 0, (sockaddr*)&receiveFrom, &saSize);
    {
        std::string ip(inet_ntoa(receiveFrom.sin_addr));
        std::cout << "\n [INFO] received broadcast from: " + ip;

        if( (std::find(detectedNodes.begin(), detectedNodes.end(), ip) == detectedNodes.end()))
        {
            detectedNodes.push_back(ip);
            std::cout << "New peer detected: " << ip << std::endl;
        }
    }
    return 0;
}

/* Aquí se realiza la request al nodo nuevo recien detectado para que nos conteste con su lista de servicios */

int meetNewNode(std::string address)
{
    for(int i = 0; i<10; i++)
    {
        zmq::context_t context (1);
        zmq::socket_t socket (context, ZMQ_REQ);
        auto connectAddress = buildIpAddress(address);
        //socket.connect(address);
        socket.connect (connectAddress);

        flatbuffers::FlatBufferBuilder builder;

        std::string hostAddress, hostName;
        getHostAddress(&hostAddress, &hostName);

        auto serverAddress = builder.CreateString(hostAddress);
        auto serverPort = builder.CreateString("4444");
        auto value_t = serverbuffer::CreateServiceRequest(builder, builder.CreateString("Server_"), serverAddress, serverPort, 0);

        auto request_t = serverbuffer::CreateRequest(builder, serverbuffer::RequestMessage_ServiceRequest, value_t.Union());
        builder.Finish(request_t);

        std::cout << "\nSending own server info to address " << connectAddress;
        int buffersize = builder.GetSize();
        zmq::message_t request(buffersize);

        memcpy((void *)request.data(), builder.GetBufferPointer(), buffersize);
        socket.send(request);

        std::cout << "...done!\n" << std::endl;
    }

    return 0;
}

/*
 *  Este método compara la lista de nodos conocidos con la de nodos detectados (que se actualiza tras cada broadcast). Si existe algun nodo nuevo en la lista de 
 *  detectados que no existe en la de conocidos, significa que se ha conectado recientemente a la red, por lo que se procede a mandar una request, para que obtener su lista de servicios.
 *  Se espera un timeout y si el nodo responde con la lista de servicios, esta se añade a la lista de listas (lista de nodos para cada cual hay una lista de servicios) y 
 *  el nodo en cuestión se añade a la lista de nodos conocidos.
 *  
 *  Si el nodo en cuestión no responde, NO se añade a la lista de nodos conocidos, y se mantiene en la lista de nodos detectados, para que en la siguiente iteración se vuelva a intentar contactar con él.
 *  
 *  Si las dos listas son iguales, return, siginifica que no hay ningún nodo nuevo en la infraestructura.
 * 
 */

int updateServicesList()
{
    bool knownNode;

    for(auto it = detectedNodes.begin(); it != detectedNodes.end(); it++)
    {
        knownNode = false;
        auto nodeAddress = *it;

        for(auto it2 = connectedNodes.begin(); it2 != connectedNodes.end(); it2++)
        {
            if(*it == *it2)
                knownNode = true;
        }
        if(knownNode == false)
        {
            meetNewNode(nodeAddress);
            connectedNodes.push_back(nodeAddress);
        }
    }
    return 0;
}

void printStats()
{
    std::cout << "\n\n[INFO] Active connections:\n--------------------------\n";
    
    std::cout << "Detected nodes: ";

   /* for(auto it = serverList.begin(); it != serverList.end(); it ++)
    {
        auto auxServer = *it;
        std::cout << auxServer.name << " with address: " << auxServer.address << std::endl;   -----> FOR DEBUGGING 
        std::cout << "   - Available services: \n";
        for(auto it2 = auxServer.mainList.begin(); it2 != auxServer.mainList.end(); it2++)
        {
            auto auxValue = *it2;
            std::cout << "      - " << auxValue.name << std::endl;
        }
    }*/
    for(auto it = detectedNodes.begin(); it != detectedNodes.end(); it++)
    {
        auto auxNode = *it;
        std::cout << auxNode << ", ";
    }

    std::cout << "\nConnected nodes: ";
    for(auto it = connectedNodes.begin(); it != connectedNodes.end(); it++)
    {
        auto auxNode = *it;
        std::cout << auxNode << ", ";
    }
}

int mainFlow(SOCKET socket_broadcast) /* TODO inicialización del zmq::socket fuera de la función */
{

		int ERROR_CODE = 0;
        zmq::context_t context = zmq::context_t(1);
        zmq::socket_t socket_p = zmq::socket_t(context, ZMQ_REP);  /* Socket for receiving REQUEST */ 
        socket_p.bind("tcp://*:1111");
            
        // Set up the sockaddr structure

        struct sockaddr_in saListen = {0};
        saListen.sin_family = AF_INET;
        saListen.sin_port = htons(PING_PORT_NUMBER);
        saListen.sin_addr.s_addr = htonl(INADDR_ANY);

        zmq::pollitem_t items[] = { {socket_p, 0, ZMQ_POLLIN, 0}, {NULL, socket_broadcast, ZMQ_POLLIN, 0} };

        while(ERROR_CODE == 0)
        {
            zmq::message_t receive_t;
            zmq::poll(&items[0], 2, 0);

            if (items[0].revents & ZMQ_POLLIN)  /* Received a REQUEST*/
            {
                socket_p.recv(&receive_t); 
                handleMessage(receive_t.data());
                socket_p.send(receive_t);
            }
            else if (items[1].revents & ZMQ_POLLIN) /* Received a BROADCAST */
            {
                handleBroadcast(socket_broadcast, saListen);
            }
            else
            {
                updateServicesList();
                printStats();
            }
            sleep(600);
        }

    return 0;
}

int main()
{

    SOCKET broadcastSocket;
    int ERROR_CODE;
    LPDWORD dwThreadID = 0;
    std::string hostName, hostAddress;

    broadcastSocket = initBroadcastSocket(); /* Socket for receiving the BROADCAST */
    ERROR_CODE = getHostAddress(&hostAddress, &hostName);
    ERROR_CODE = doBroadcast();

    std::cout << std::endl << "Server " << hostName << " is now online and listening\n";

    ERROR_CODE = mainFlow(broadcastSocket);

    return 0;

}