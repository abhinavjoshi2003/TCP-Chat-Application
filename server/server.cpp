
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <memory>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <deque>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

using namespace std;

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_MESSAGE_SIZE 4096

struct Client
{
    SOCKET socket;
    int id;
    string username;

    mutex sendMutex;
    mutex usernameMutex;

    atomic<bool> closing{false};
};

// Function forward declarations
bool sendAll(
    const shared_ptr<Client> &client,
    const string &message);

void broadcastMessage(const string &message);

void sendPrivateMessage(
    const shared_ptr<Client> &sender,
    const string &targetUsername,
    const string &message);

vector<shared_ptr<Client>> clients;
mutex clientsMutex;

vector<thread> clientThreads;

atomic<bool> stopRequested(false);

atomic<long long> totalConnections(0);
atomic<long long> messagesReceived(0);
atomic<long long> messagesBroadcast(0);
atomic<long long> bytesReceived(0);
atomic<long long> bytesSent(0);
atomic<int> activeClients(0);
atomic<int> privateMessagesSent{0};

int nextClientId = 1;

const size_t MAX_HISTORY_SIZE = 20;

deque<string> messageHistory;
mutex historyMutex;
void storeMessage(const string &message)
{
    lock_guard<mutex> lock(historyMutex);

    messageHistory.push_back(message);

    if (messageHistory.size() > MAX_HISTORY_SIZE)
    {
        messageHistory.pop_front();
    }
}

// Windows console handler: request shutdown on Ctrl+C.
BOOL WINAPI consoleHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT ||
        signal == CTRL_BREAK_EVENT ||
        signal == CTRL_CLOSE_EVENT)
    {
        stopRequested.store(true);
        return TRUE;
    }

    return FALSE;
}

bool isValidUsername(const string &username)
{
    if (username.empty() || username.size() > 16)
        return false;

    for (unsigned char ch : username)
    {
        if (!isalnum(ch) && ch != '_')
            return false;
    }

    return true;
}

string toLowerCase(string value)
{
    transform(value.begin(), value.end(), value.begin(),
              [](unsigned char ch)
              {
                  return static_cast<char>(tolower(ch));
              });

    return value;
}

string getUsername(const shared_ptr<Client> &client)
{
    lock_guard<mutex> lock(client->usernameMutex);
    return client->username;
}

bool changeUsername(
    const shared_ptr<Client> &currentClient,
    const string &newUsername)
{
    if (!isValidUsername(newUsername))
        return false;

    lock_guard<mutex> clientsLock(clientsMutex);

    string normalized = toLowerCase(newUsername);

    for (const auto &client : clients)
    {
        if (client.get() == currentClient.get())
            continue;

        if (client->closing.load())
            continue;

        if (toLowerCase(getUsername(client)) == normalized)
            return false;
    }

    {
        lock_guard<mutex> usernameLock(
            currentClient->usernameMutex);

        currentClient->username = newUsername;
    }

    return true;
}

// Send every byte, accounting for partial sends.
bool sendAll(const shared_ptr<Client> &client,
             const string &message)
{
    lock_guard<mutex> lock(client->sendMutex);

    if (client->closing.load() ||
        client->socket == INVALID_SOCKET)
    {
        return false;
    }

    int totalSent = 0;
    int length = static_cast<int>(message.size());

    while (totalSent < length)
    {
        int sent = send(
            client->socket,
            message.c_str() + totalSent,
            length - totalSent,
            0);

        if (sent == SOCKET_ERROR || sent == 0)
            return false;

        totalSent += sent;
        bytesSent.fetch_add(sent);
    }

    return true;
}

void sendMessageHistory(const shared_ptr<Client> &client)
{
    string history = "\n========== MESSAGE HISTORY ==========\n";

    {
        lock_guard<mutex> lock(historyMutex);

        if (messageHistory.empty())
        {
            history += "No previous messages.\n";
        }
        else
        {
            for (const auto &message : messageHistory)
            {
                history += message + "\n";
            }
        }
    }

    history += "=====================================\n";

    sendAll(client, history);
}

void listConnectedUsers(const shared_ptr<Client> &requester)
{
    vector<string> usernames;

    // Take a snapshot of currently connected users.
    {
        lock_guard<mutex> lock(clientsMutex);

        for (const auto &client : clients)
        {
            if (!client->closing.load())
            {
                usernames.push_back(getUsername(client));
            }
        }
    }

    // Build the response after releasing the mutex.
    string response = "\n[ONLINE USERS]\n";

    for (const auto &username : usernames)
    {
        response += "- " + username + "\n";
    }

    response += "Total connected: " +
                to_string(usernames.size()) + "\n";

    sendAll(requester, response);
}

void processMessage(
    const shared_ptr<Client> &client,
    const string &message)
{
    // Username change command
    if (message.rfind("/name ", 0) == 0)
    {
        string newUsername = message.substr(6);

        string oldUsername = getUsername(client);

        if (!isValidUsername(newUsername))
        {
            sendAll(
                client,
                "Invalid username. Use 1-16 letters, "
                "numbers or underscores.\n");
            return;
        }

        if (toLowerCase(oldUsername) ==
            toLowerCase(newUsername))
        {
            sendAll(client, "You already have this username.\n");
            return;
        }

        if (changeUsername(client, newUsername))
        {
            string notification =
                oldUsername + " is now known as " +
                newUsername + ".";

            broadcastMessage(notification);
        }
        else
        {
            sendAll(
                client,
                "Username already taken. Choose another.\n");
        }

        return;
    }

    if (message.rfind("/msg ", 0) == 0)
    {
        string arguments = message.substr(5);

        // Extract the recipient username.
        size_t separator = arguments.find_first_of(" \t");

        if (separator == string::npos)
        {
            sendAll(client,
                    "[ERROR] Usage: /msg <username> <message>\n");
            return;
        }

        string targetUsername = arguments.substr(0, separator);

        // Extract the actual message, skipping extra spaces.
        size_t messageStart = arguments.find_first_not_of(" \t", separator);

        if (messageStart == string::npos)
        {
            sendAll(client,
                    "[ERROR] Private message cannot be empty.\n");
            return;
        }

        string privateText = arguments.substr(messageStart);

        sendPrivateMessage(client, targetUsername, privateText);
        return;
    }
    if (message == "/users")
    {
        listConnectedUsers(client);
        return;
    }

    // Ordinary chat message
    string formattedMessage = getUsername(client) + ": " + message;

    storeMessage(formattedMessage);
    broadcastMessage(formattedMessage);
}

void sendPrivateMessage(
    const shared_ptr<Client> &sender,
    const string &targetUsername,
    const string &message)
{
    shared_ptr<Client> recipient = nullptr;

    // Find the recipient while protecting the shared clients list.
    {
        lock_guard<mutex> lock(clientsMutex);

        for (const auto &client : clients)
        {
            if (toLowerCase(getUsername(client)) ==
                toLowerCase(targetUsername))
            {
                recipient = client;
                break;
            }
        }
    }

    if (!recipient)
    {
        sendAll(sender,
                "[ERROR] User '" + targetUsername + "' is not connected.\n");
        return;
    }

    string senderName = getUsername(sender);
    string recipientName = getUsername(recipient);

    // Deliver only to the intended recipient.
    string privateMessage =
        "[PRIVATE] " + senderName + ": " + message + "\n";

    if (!sendAll(recipient, privateMessage))
    {
        sendAll(sender,
                "[ERROR] Could not deliver the message to " +
                    recipientName + ".\n");
        return;
    }

    // Confirm successful delivery to the sender.
    sendAll(sender,
            "[PRIVATE] Message sent to " + recipientName + ".\n");

    privateMessagesSent++;
}

void requestClientStop(const shared_ptr<Client> &client)
{
    if (client->closing.exchange(true))
        return;

    lock_guard<mutex> lock(client->sendMutex);

    if (client->socket != INVALID_SOCKET)
    {
        shutdown(client->socket, SD_BOTH);
    }
}

void closeClientSocket(const shared_ptr<Client> &client)
{
    lock_guard<mutex> lock(client->sendMutex);

    if (client->socket != INVALID_SOCKET)
    {
        closesocket(client->socket);
        client->socket = INVALID_SOCKET;
    }
}

// Remove a client from the shared list.
void removeClient(const shared_ptr<Client> &client)
{
    client->closing.store(true);

    bool removed = false;

    {
        lock_guard<mutex> lock(clientsMutex);

        for (auto it = clients.begin();
             it != clients.end(); ++it)
        {
            if (it->get() == client.get())
            {
                clients.erase(it);
                removed = true;
                break;
            }
        }
    }

    if (removed)
        activeClients.fetch_sub(1);

    // Called by the handler thread.
    closeClientSocket(client);
}

// Broadcast without holding the shared client-list mutex
// during potentially slow network operations.
void broadcastMessage(const string &message)
{
    vector<shared_ptr<Client>> snapshot;

    {
        lock_guard<mutex> lock(clientsMutex);
        snapshot = clients;
    }

    string framedMessage = message + "\n";

    messagesBroadcast.fetch_add(1);

    for (const auto &client : snapshot)
    {
        if (!sendAll(client, framedMessage))
        {
            cout << "Send failed for Client "
                 << client->id << ". Disconnecting."
                 << endl;

            requestClientStop(client);
        }
    }
}

// Handle one connected client.
void handleClient(shared_ptr<Client> client)
{
    cout << "Client " << client->id
         << " handler started. Thread ID: "
         << this_thread::get_id() << endl;

    char buffer[BUFFER_SIZE];
    string pendingData;

    bool disconnected = false;

    while (!stopRequested.load())
    {
        SOCKET sock = client->socket;

        if (sock == INVALID_SOCKET)
            break;

        int received = recv(
            sock,
            buffer,
            BUFFER_SIZE,
            0);

        if (received > 0)
        {
            bytesReceived.fetch_add(received);

            pendingData.append(buffer, received);

            size_t newlinePosition;

            while ((newlinePosition =
                        pendingData.find('\n')) != string::npos)
            {
                if (newlinePosition > MAX_MESSAGE_SIZE)
                {
                    cout << "Client " << client->id
                         << " sent an oversized message."
                         << endl;

                    disconnected = true;
                    break;
                }

                string message =
                    pendingData.substr(0, newlinePosition);

                pendingData.erase(0, newlinePosition + 1);

                // Remove carriage return from CRLF messages.
                if (!message.empty() && message.back() == '\r')
                    message.pop_back();

                if (message.empty())
                    continue;

                messagesReceived.fetch_add(1);

                processMessage(client, message);
            }

            if (disconnected)
                break;

            if (pendingData.size() > MAX_MESSAGE_SIZE)
            {
                cout << "Client " << client->id
                     << " exceeded the message size limit."
                     << endl;

                disconnected = true;
                break;
            }
        }
        else if (received == 0)
        {
            cout << "Client " << client->id
                 << " disconnected gracefully."
                 << endl;
            break;
        }
        else
        {
            int error = WSAGetLastError();

            if (!stopRequested.load())
            {
                cout << "Receive error for Client "
                     << client->id << ": "
                     << error << endl;
            }

            break;
        }
    }

    removeClient(client);

    if (!stopRequested.load())
    {
        broadcastMessage(
            getUsername(client) + " has disconnected.");
    }

    cout << "Client " << client->id
         << " handler stopped." << endl;
}

// Print current network statistics.
void printStatistics()
{
    cout << "\n========== NETWORK STATISTICS =========="
         << endl;

    cout << "Total connections : "
         << totalConnections.load() << endl;

    cout << "Active clients    : "
         << activeClients.load() << endl;

    cout << "Messages received : "
         << messagesReceived.load() << endl;

    cout << "Broadcasts        : "
         << messagesBroadcast.load() << endl;

    cout << "Bytes received    : "
         << bytesReceived.load() << endl;

    cout << "Bytes sent        : "
         << bytesSent.load() << endl;

    cout << "=========================================\n"
         << endl;
}

int main()
{
    // STEP 1: Initialize Winsock.
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cout << "Winsock initialization failed."
             << endl;
        return 1;
    }

    cout << "Winsock initialized." << endl;

    // STEP 2: Create server socket.
    SOCKET serverSocket = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP);

    if (serverSocket == INVALID_SOCKET)
    {
        cout << "Socket creation failed: "
             << WSAGetLastError() << endl;

        WSACleanup();
        return 1;
    }

    // Allow quick reuse of the address after shutdown.
    BOOL reuseAddress = TRUE;

    setsockopt(
        serverSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char *)&reuseAddress,
        sizeof(reuseAddress));

    // STEP 3: Configure server address.
    sockaddr_in serverAddress = {};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(PORT);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    // STEP 4: Bind.
    if (bind(
            serverSocket,
            (sockaddr *)&serverAddress,
            sizeof(serverAddress)) == SOCKET_ERROR)
    {
        cout << "Bind failed: "
             << WSAGetLastError() << endl;

        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // STEP 5: Listen.
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        cout << "Listen failed: "
             << WSAGetLastError() << endl;

        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // Make listening socket nonblocking so the main
    // thread can periodically check the shutdown flag.
    u_long nonBlocking = 1;

    if (ioctlsocket(
            serverSocket,
            FIONBIO,
            &nonBlocking) == SOCKET_ERROR)
    {
        cout << "Could not configure nonblocking socket."
             << endl;

        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    cout << "Server listening on port "
         << PORT << endl;

    cout << "Press Ctrl+C to shut down the server."
         << endl;

    auto lastStatsTime = chrono::steady_clock::now();

    // STEP 6: Accept clients.
    while (!stopRequested.load())
    {
        sockaddr_in clientAddress = {};
        int clientLength = sizeof(clientAddress);

        SOCKET clientSocket = accept(
            serverSocket,
            (sockaddr *)&clientAddress,
            &clientLength);

        if (clientSocket != INVALID_SOCKET)
        {
            // Ensure accepted sockets are blocking.
            u_long blocking = 0;

            ioctlsocket(
                clientSocket,
                FIONBIO,
                &blocking);

            int clientId = nextClientId++;

            auto client = make_shared<Client>();

            client->socket = clientSocket;
            client->id = clientId;
            client->username = "Guest" + to_string(clientId);

            {
                lock_guard<mutex> lock(clientsMutex);

                clients.push_back(client);
                activeClients.fetch_add(1);
                sendMessageHistory(client);
            }

            totalConnections.fetch_add(1);

            cout << "Client " << clientId
                 << " connected. Active clients: "
                 << activeClients.load() << endl;

            broadcastMessage(
                getUsername(client) + " has joined.");

            clientThreads.emplace_back(
                handleClient,
                client);
        }
        else
        {
            int error = WSAGetLastError();

            if (error != WSAEWOULDBLOCK &&
                !stopRequested.load())
            {
                cout << "Accept failed: "
                     << error << endl;
            }

            Sleep(100);
        }

        // Display statistics every 10 seconds.
        auto now = chrono::steady_clock::now();

        if (chrono::duration_cast<chrono::seconds>(
                now - lastStatsTime)
                .count() >= 10)
        {
            printStatistics();
            lastStatsTime = now;
        }
    }

    // STEP 7: Graceful server shutdown.
    cout << "\nServer shutdown requested."
         << endl;

    closesocket(serverSocket);

    // Stop all connected clients.
    vector<shared_ptr<Client>> remainingClients;

    {
        lock_guard<mutex> lock(clientsMutex);
        remainingClients = clients;
    }

    // Request disconnection. Do not close sockets here.
    for (const auto &client : remainingClients)
    {
        requestClientStop(client);
    }

    // Wait for every handler to perform its cleanup.
    for (thread &clientThread : clientThreads)
    {
        if (clientThread.joinable())
            clientThread.join();
    }

    printStatistics();

    SetConsoleCtrlHandler(consoleHandler, FALSE);

    WSACleanup();

    cout << "Private messages sent: "
         << privateMessagesSent.load() << endl;

    cout << "Server shut down successfully."
         << endl;

    return 0;
}
