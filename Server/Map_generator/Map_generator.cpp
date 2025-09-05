#define _WIN32_WINNT_WIN10  0x0500
#include <iostream>
#include <fstream>
#include <string>
#include <Windows.h>
#include <shlobj.h> //SHLDialog
#include <ctime>
#include <cstdlib>
#include <cmath>
#include <random>
#pragma comment(lib, "winmm.lib")

using namespace std;

// Размеры массива задаются пользователем
int WIDTH, HEIGHT;

// Градиенты для каждой ячейки сетки
float*** gradients;

// Функция для интерполяции между двумя значениями
inline float lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

// Функция для плавного перехода от 0 до 1
inline float smoothstep(float t)
{
    return t * t * (3 - 2 * t);
}

// Генерация случайных градиентов
void generateGradients()
{
    mt19937 gen(random_device{}());
    uniform_real_distribution<float> dist(-1.0f, 1.0f);

    gradients = new float** [WIDTH];
    for (int x = 0; x < WIDTH; ++x) {
        gradients[x] = new float* [HEIGHT];
        for (int y = 0; y < HEIGHT; ++y) {
            gradients[x][y] = new float[2];
        }
    }

    for (int x = 0; x < WIDTH; ++x) {
        for (int y = 0; y < HEIGHT; ++y) {
            // Нормализуем вектор градиента
            float gx = dist(gen), gy = dist(gen);
            float length = sqrt(gx * gx + gy * gy);
            if (length > 0) {
                gx /= length;
                gy /= length;
            }
            gradients[x][y][0] = gx;
            gradients[x][y][1] = gy;
        }
    }
}

// Функция для вычисления скалярного произведения вектора и градиента
float dotGridGradient(int ix, int iy, float x, float y)
{
    // Приводим координаты к диапазону [0, WIDTH-1], [0, HEIGHT-1]
    ix %= WIDTH;
    iy %= HEIGHT;

    // Вычисляем расстояние от точки до центра текущей ячейки
    float dx = x - ix;
    float dy = y - iy;

    // Скалярное произведение расстояния и градиента
    return dx * gradients[ix][iy][0] + dy * gradients[ix][iy][1];
}

// Основная функция для генерации шума Перлина
float perlinNoise(float x, float y)
{
    // Приводим координаты к целым числам
    int x0 = static_cast<int>(floor(x));
    int x1 = x0 + 1;
    int y0 = static_cast<int>(floor(y));
    int y1 = y0 + 1;

    // Вычисление весовых коэффициентов для интерполяции
    float sx = x - x0;
    float sy = y - y0;

    // Интерполируем значения в четырех углах квадрата
    float n0 = dotGridGradient(x0, y0, x, y);
    float n1 = dotGridGradient(x1, y0, x, y);
    float ix0 = lerp(n0, n1, smoothstep(sx));

    n0 = dotGridGradient(x0, y1, x, y);
    n1 = dotGridGradient(x1, y1, x, y);
    float ix1 = lerp(n0, n1, smoothstep(sx));

    // Итоговая интерполяция
    return lerp(ix0, ix1, smoothstep(sy));
}

void Write_txt(string filename) {
    string line;

    cout << "Is your map square or rectangle? (Write S or R)" << endl;
    cin >> line;
    
    int Chunk_length;

    if (line == "S") {
        // Ввод длины от пользователя
        while (true)
        {
            cout << "Attention! The size of your world will be increased by the number of chunk cells.";
            cout << "Chunk length: ";
            cin >> Chunk_length;
            cout << "Length: ";
            cin >> WIDTH;
            if (WIDTH % Chunk_length == 0) {
                break;
            }
        }
        HEIGHT = WIDTH;
    }
    else if (line == "R") {
        // Ввод количества строк и столбцов от пользователя
        while (true)
        {
            cout << "Attention! The size of your world will be increased by the number of chunk cells.";
            cout << "Chunk length: ";
            cin >> Chunk_length;
            cout << "Value of lines: ";
            cin >> WIDTH;
            cout << "Value of colons: ";
            cin >> HEIGHT;
            if (WIDTH % Chunk_length == 0 && HEIGHT % Chunk_length == 0) {
                break;
            }
        }
    }

    // Проверка допустимости размеров
    if (WIDTH <= 0 || HEIGHT <= 0) {
        cerr << "Incorrect size of massive. Numbers need to be more then 0." << endl;
    }

    cout << "How many levels you need?" << endl;
    double lev;
    cin >> lev;
    if (lev <= 0) {
        lev = 1;
    }
    double piece = 2.0 / lev;
    int piece_level = 0;

    // Генерируем случайные градиенты
    generateGradients();

    // Массив для хранения значений шума
    float** noiseMap = new float* [WIDTH];
    for (int i = 0; i < WIDTH; ++i) {
        noiseMap[i] = new float[HEIGHT];
    }

    // Заполняем массив шумом Перлина
    for (int i = 0; i < WIDTH; ++i) {
        for (int j = 0; j < HEIGHT; ++j) {
            // Получаем значение шума в диапазоне [-1, 1]
            float noiseValue = perlinNoise(i / (float)WIDTH, j / (float)HEIGHT);
            noiseMap[i][j] = noiseValue;
        }
    }

    ofstream world(filename);

    if (!world.is_open()) {
        cerr << "Не удалось создать файл " << filename << endl;
    }

    // Выводим результат
    for (int i = 0; i < WIDTH; ++i) {
        for (int j = 0; j < HEIGHT; ++j) {
            double N = piece;
            if (lev == 2.0) {
                while (true) {
                    if (noiseMap[i][j] + 1 <= N) {
                        world << piece_level << ";";
                        cout << piece_level << ";";
                        break;
                    }
                    else if (noiseMap[i][j] + 1 >= N && N >= piece * lev) {
                        world << piece_level << ";";
                        cout << piece_level << ";";
                        break;
                    }
                    piece_level++;
                    N += piece;
                }
            }
            else {
                for (N; N < lev; N += piece) {
                    if (noiseMap[i][j] + 1 <= N) {
                        world << piece_level << ";";
                        cout << piece_level << ";";
                        break;
                    }
                    else if (noiseMap[i][j] + 1 >= N && N >= piece * lev) {
                        world << piece_level << ";";
                        cout << piece_level << ";";
                        break;
                    }
                    else {
                        piece_level++;
                    }
                }
            }
            piece_level = 0;
        }
        world << endl;
        cout << endl;
    }

    world.close();

    cout << "Запись в файл " << filename << " выполнена." << endl;

    // Освобождаем память
    for (int x = 0; x < WIDTH; ++x) {
        for (int y = 0; y < HEIGHT; ++y) {
            delete[] gradients[x][y];
        }
        delete[] gradients[x];
    }
    delete[] gradients;

    for (int i = 0; i < WIDTH; ++i) {
        delete[] noiseMap[i];
    }
    delete[] noiseMap;
}

void Read_txt(string filename) {
    ifstream world(filename); // Открываем файл для чтения

    if (!world.is_open()) { // Проверяем успешность открытия файла
        cerr << "Coudn't open file" << endl;
    }

    string line;
    while (getline(world, line)) { // Читаем файл построчно
        cout << line << endl; // Выводим каждую строку
    }

    world.close(); // Закрываем файл после завершения работы
}

int main() {
    cout << "You wish to check the map, write - 'Check'" << endl << "If you want to generate map, write - 'Gen'" << endl;
    string answer;
    cin >> answer;
    string folder; // Имя сервера
    cin >> folder;
    if (answer == "Check") {
        system("cls");
        Read_txt("../../../Saved_data/Created_servers/" + folder + "/map/World.txt");
    }
    else if (answer == "Gen") {
        Write_txt("../../../Saved_data/Created_servers/" + folder + "/map/World.txt");
        system("cls");
        Read_txt("../../../Saved_data/Created_servers/" + folder + "/map/World.txt");
        Sleep(1500);
    }
}