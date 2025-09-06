#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <string>
#include <map>

#pragma comment(lib, "Ws2_32.lib")

using namespace std;

const int PORT = 8080;

// Список активных клиентов и их сокеты
vector<SOCKET> clients;
int num_clients = 0;

// Карта для сохранения соответствия сокета и имени пользователя
map<SOCKET, pair<string, int>> users_and_ids; // теперь храним пару {имя, id}

// Следующий свободный идентификатор
int next_id = 1;

// Добавление нового пользователя с присвоением идентификатора
void add_user(SOCKET client_socket, const string& username)
{
    users_and_ids[client_socket] = make_pair(username, next_id++); // добавляем новое уникальное ID
}

// Функция рассылки сообщений всем клиентам, кроме отправителя
void broadcast_message(const string& message, SOCKET sender)
{
    for (auto client : clients)
    {
        if (client != sender)
        {
            send(client, message.c_str(), message.size() + 1, 0);
        }
    }
}

// Глобальное объявление функции проверки имени и пароля
bool check_credentials(const std::string& username, const std::string& password);

// Проверка имени и пароля (теперь разрешена любая комбинация)
bool check_credentials(const std::string& username, const std::string& password)
{
    // Просто возвращаем true, позволяя зарегистрировать любое имя и пароль
    return true;
}

// Обработчик запросов клиентов
void handle_client(SOCKET client_socket)
{
    char buffer[1024];
    bool authenticated = false;

    while (!authenticated)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);

        if (bytes_received <= 0)
        {
            // Клиент отключился во время авторизации
            cout << "Client disconnected during authentication." << endl;
            closesocket(client_socket);
            break;
        }

        string packet(buffer, bytes_received);
        size_t pos = packet.find('|'); // Разделение данных на части
        if (packet.substr(0, pos) == "AUTH")
        {
            // Извлекаем имя и пароль
            string credentials = packet.substr(pos + 1);
            size_t next_pos = credentials.find('|');
            string username = credentials.substr(0, next_pos);
            string password = credentials.substr(next_pos + 1);

            // Проверка пары имя-пароль (разрешено всё!)
            if (check_credentials(username, password))
            {
                authenticated = true;
                add_user(client_socket, username); // Присваиваем ID пользователю
                send(client_socket, "OK", 3, 0); // Подтверждаем успешную регистрацию
            }
            else
            {
                // Возвращаем ошибку и предлагаем правильный формат
                string error_message = "Invalid format. Use AUTH|username|password\n";
                send(client_socket, error_message.c_str(), error_message.size() + 1, 0);
            }
        }
        else
        {
            // Если передан неправильный формат команды
            string error_message = "Incorrect command format. Please use AUTH|username|password\n";
            send(client_socket, error_message.c_str(), error_message.size() + 1, 0);
        }
    }

    // Обычный цикл работы с сообщениями
    while (true)
    {
        memset(buffer, 0, sizeof(buffer));
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0)
        {
            cout << "Client disconnected." << endl;
            closesocket(client_socket);
            clients.erase(remove(clients.begin(), clients.end(), client_socket), clients.end());
            num_clients--;
            cout << "Current number of clients: " << num_clients << endl;
            break;
        }

        // Получаем сообщение
        string clean_message(buffer, bytes_received);

        // Извлекаем ID пользователя
        int user_id = users_and_ids[client_socket].second;
        string username = users_and_ids[client_socket].first;

        // Формируем подпись с ID и именем пользователя
        string signed_message = "[" + to_string(user_id) + "] " + username + ": " + clean_message;

        // Рассылаем подписанное сообщение другим пользователям
        broadcast_message(signed_message, client_socket);
    }
}

int main()
{
    int MAX_CLIENTS;
    cout << "Write max clients on server: ";
    cin >> MAX_CLIENTS;
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET)
    {
        cerr << "Failed to create socket." << endl;
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        cerr << "Failed to bind socket." << endl;
        closesocket(server_socket);
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        cerr << "Failed to listen on socket." << endl;
        closesocket(server_socket);
        return 1;
    }

    cout << "Server is listening on port " << PORT << "." << endl;

    while (true)
    {
        SOCKET client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET)
        {
            cerr << "Failed to accept connection." << endl;
            continue;
        }

        // Проверка на максимальное количество клиентов
        if (num_clients >= MAX_CLIENTS)
        {
            cerr << "The connection attempt was rejected due to exceeding the maximum number of" << endl;

            // Отправляем сообщение клиенту о превышении лимита
            const char* error_message = "Connection refused: maximum number of clients reached.\n";
            send(client_socket, error_message, strlen(error_message), 0);

            closesocket(client_socket); // Закрываем соединение, если лимит достигнут
            continue;
        }

        clients.push_back(client_socket);
        num_clients++;
        cout << "New client connected. Current number of clients: " << num_clients << endl;

        thread client_thread(handle_client, client_socket);
        client_thread.detach();
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}