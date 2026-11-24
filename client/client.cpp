
#include <iostream>
#include <string>
#include <thread>
#include <atomic>

#include <winsock2.h>
#include <ws2tcpip.h>

using namespace std;

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_MESSAGE_SIZE 4096

atomic<bool> serverDisconnected(false);

// Send all bytes, handling partial sends.
bool sendAll(SOCKET sock, const string &message)
{
    int totalSent = 0;
    int length = static_cast<int>(message.size());

    while (totalSent < length)
    {
        int sent = send(
            sock,
            message.c_str() + totalSent,
            length - totalSent,
            0);

        if (sent == SOCKET_ERROR || sent == 0)
            return false;

        totalSent += sent;
    }

    return true;
}

// Receive and reconstruct complete newline-delimited messages.
void receiveMessages(SOCKET sock)
{
    char buffer[BUFFER_SIZE];
    string pendingData;

    while (true)
    {
        int received = recv(
            sock,
            buffer,
            BUFFER_SIZE,
            0);

        if (received > 0)
        {
            pendingData.append(buffer, received);

            size_t newlinePosition;

            while ((newlinePosition =
                        pendingData.find('\n')) != string::npos)
            {
                if (newlinePosition > MAX_MESSAGE_SIZE)
                {
                    cout << "\nServer sent an oversized message."
                         << endl;

                    serverDisconnected.store(true);
                    return;
                }

                string message =
                    pendingData.substr(0, newlinePosition);

                pendingData.erase(0, newlinePosition + 1);

                if (!message.empty() && message.back() == '\r')
                    message.pop_back();

                if (!message.empty())
                {
                    cout << "\n"
                         << message << endl;
                    cout << "> " << flush;
                }
            }

            if (pendingData.size() > MAX_MESSAGE_SIZE)
            {
                cout << "\nIncoming message exceeded the size limit."
                     << endl;

                serverDisconnected.store(true);
                return;
            }
        }
        else if (received == 0)
        {
            cout << "\nServer closed the connection."
                 << endl;

            serverDisconnected.store(true);
            return;
        }
        else
        {
            if (!serverDisconnected.load())
            {
                cout << "\nReceive error: "
                     << WSAGetLastError() << endl;
            }

            serverDisconnected.store(true);
            return;
        }
    }
}

int main()
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cout << "Winsock initialization failed."
             << endl;
        return 1;
    }

    SOCKET clientSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP);

    if (clientSocket == INVALID_SOCKET)
    {
        cout << "Socket creation failed: "
             << WSAGetLastError() << endl;

        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress = {};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT);

    inet_pton(
        AF_INET,
        "127.0.0.1",
        &serverAddress.sin_addr);

    if (connect(
            clientSocket,
            (sockaddr *)&serverAddress,
            sizeof(serverAddress)) == SOCKET_ERROR)
    {
        cout << "Connection failed: "
             << WSAGetLastError() << endl;

        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    cout << "Connected to chat server."
         << endl;

    cout << "Type /quit to disconnect."
         << endl;

    thread receiver(
        receiveMessages,
        clientSocket);

    string message;

    while (!serverDisconnected.load())
    {
        cout << "> ";

        if (!getline(cin, message))
            break;

        if (serverDisconnected.load())
            break;

        if (message == "/quit")
            break;

        if (message.empty())
            continue;

        if (message.size() > MAX_MESSAGE_SIZE)
        {
            cout << "Message too long. Maximum: "
                 << MAX_MESSAGE_SIZE << " bytes."
                 << endl;
            continue;
        }

        // Add newline delimiter.
        message += "\n";

        if (!sendAll(clientSocket, message))
        {
            cout << "Failed to send message."
                 << endl;

            break;
        }
    }

    // Interrupt the receiver thread if it is blocked in recv().
    shutdown(clientSocket, SD_BOTH);

    if (receiver.joinable())
        receiver.join();

    closesocket(clientSocket);
    WSACleanup();

    cout << "Client disconnected."
         << endl;

    return 0;
}
