# TCP Chat Application in C++

A multithreaded, command-line chat application built using **C++ and TCP sockets (Winsock2)**. The application supports multiple simultaneous clients, public messaging, private messaging, username management, and in-memory message history.

## Features

- **Multi-client communication:** Supports multiple concurrent client connections using a thread-per-client model.
- **Public messaging:** Broadcast messages to all connected clients.
- **Private messaging:** Send direct messages to a specific user.
- **Username management:** Assign and change usernames with case-insensitive uniqueness validation.
- **Online user listing:** View all currently connected users.
- **Message history:** Stores and displays the latest 20 public messages to newly connected clients.
- **TCP message framing:** Uses newline-delimited messages to identify complete messages over the TCP byte stream.
- **Graceful disconnection:** Handles client disconnections and server shutdown.
- **Network statistics:** Tracks connections, messages, and network traffic.
- **Thread synchronization:** Uses mutexes and atomic variables to protect shared resources.
- **Input validation:** Enforces message-size limits to handle oversized input.

## Technology Stack

- **Language:** C++17
- **Networking:** TCP/IP, Winsock2
- **Concurrency:** std::thread, std::mutex, std::atomic
- **Build system:** GNU Make
- **Compiler:** MinGW-w64 GCC
- **Platform:** Windows

## Project Structure

```text
TCP-Chat-Application/
│
├── client/
│   └── client.cpp
│
├── server/
│   └── server.cpp
│
├── Makefile
├── .gitignore
├── README.md
└── LICENSE
```

## Prerequisites

- Windows operating system
- MSYS2 UCRT64 environment
- MinGW-w64 GCC compiler
- GNU Make

Ensure that `g++` and `make` are available in your terminal.

## Build Instructions

Clone the repository:

```bash
git clone <YOUR_GITHUB_REPOSITORY_URL>
cd TCP-Chat-Application
```

Compile both server and client:

```bash
make
```

Clean generated executables:

```bash
make clean
```

## Running the Application

**1. Start the server**

Open a terminal in the project directory:

```bash
./server.exe
```

The server starts listening on the configured port (8080).

**2. Start clients**

Open separate terminals and execute:

```bash
./client.exe
```

Launch multiple client instances to simulate multiple users.

The current implementation is configured for localhost connections.

## Available Chat Commands

| Command                     | Description                 |
| --------------------------- | --------------------------- |
| `/name <username>`          | Set or change your username |
| `/msg <username> <message>` | Send a private message      |
| `/users`                    | Display connected users     |
| `/quit`                     | Disconnect from the chat    |

Any message that does not begin with a recognized command is broadcast publicly.

## Architecture

The application follows a multithreaded client-server architecture.

### Server

- Creates a TCP listening socket using Winsock2.
- Accepts incoming client connections.
- Creates a dedicated handler thread for each connected client.
- Receives and processes client messages.
- Broadcasts public messages and routes private messages.
- Maintains shared client information and message history.
- Coordinates graceful shutdown and collects network statistics.

### Client

- Establishes a TCP connection with the server.
- Uses a separate receiver thread to continuously receive incoming messages.
- Uses the main thread to read user input and send messages.
- Supports simultaneous sending and receiving.

## Networking and Concurrency Concepts

The project demonstrates practical implementation of:

- TCP connection establishment and communication.
- Socket programming using Winsock2.
- Client-server architecture.
- Concurrent client handling using multithreading.
- TCP stream message framing using newline delimiters.
- Handling partial socket sends.
- Synchronization of shared resources using mutexes.
- Atomic variables for thread-safe counters and state.
- Graceful socket shutdown and resource cleanup.

## Testing Performed

The following scenarios were tested:

- Multiple clients communicating simultaneously.
- Public message broadcasting.
- Private messaging between clients.
- Username changes and duplicate username rejection.
- Listing connected users.
- Message history delivery to newly connected clients.
- Abrupt client termination.
- Oversized message handling.
- Server shutdown with multiple active clients.

## Current Limitations

- The application currently supports localhost connections only.
- Message history is stored in memory and is lost when the server restarts.
- The application uses a command-line interface.
- Communication does not currently include encryption or authentication.

## Future Improvements

- Support LAN communication between different devices.
- Add persistent message storage.
- Introduce user authentication.
- Implement encrypted communication using TLS.
- Develop a graphical user interface.

## Author

**Abhinav Joshi**

[GitHub](https://github.com/abhinavjoshi2003) | [LinkedIn](https://www.linkedin.com/in/abhinav-joshi-85a1b4273)
