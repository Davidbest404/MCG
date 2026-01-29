#include <windows.h>
#include <iostream>
#include <io.h>
#include <fcntl.h>

    // ANSI цветные коды (работают после инициализации)
    static const char* Reset = "\033[0m";
    static const char* Red = "\033[31m";
    static const char* Green = "\033[32m";
    static const char* Yellow = "\033[33m";
    static const char* Blue = "\033[34m";
    static const char* Magenta = "\033[35m";
    static const char* Cyan = "\033[36m";
    static const char* White = "\033[37m";

class ConsoleHelper {
public:
    // Инициализация консоли для поддержки Unicode и русского
    static void InitConsole() {
        // Устанавливаем кодовую страницу UTF-8
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        // Настраиваем буфер для поддержки Unicode
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING; // Для ANSI escape-кодов
        SetConsoleMode(hOut, dwMode);

        // Для старых версий Windows (до Win10)
        // используем SetConsoleFont
        SetConsoleFont();
    }

    // Установка шрифта, поддерживающего Unicode
    static void SetConsoleFont() {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_FONT_INFOEX fontInfo;
        fontInfo.cbSize = sizeof(fontInfo);
        GetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);

        // Шрифт Consolas хорошо поддерживает Unicode
        wcscpy_s(fontInfo.FaceName, L"Consolas");
        SetCurrentConsoleFontEx(hConsole, FALSE, &fontInfo);
    }

    // Установка цвета текста
    static void SetColor(int textColor, int bgColor = 0) {
        HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hStdOut, (WORD)((bgColor << 4) | textColor));
    }
};

// Альтернативный подход с wchar_t
void PrintUnicodeWithWideChars() {
    // Устанавливаем режим для широких символов
    _setmode(_fileno(stdout), _O_U16TEXT);

    // Вывод русских букв
    std::wcout << L"Привет, мир! Русские буквы работают!" << std::endl;

    // Unicode символы
    std::wcout << L"Блоки: \u2588\u2588\u2588" << std::endl;      // ███
    std::wcout << L"Смайлик: \u263A" << std::endl;                // ☺
    std::wcout << L"Сердце: \u2665" << std::endl;                // ♥
    std::wcout << L"Шахматы: \u2654 \u2655" << std::endl;        // ♔ ♕

    // Возвращаем обычный режим
    _setmode(_fileno(stdout), _O_TEXT);
}

// Основной рекомендуемый подход (используем UTF-8 строки)
void PrintWithUTF8() {
    // Инициализируем консоль
    ConsoleHelper::InitConsole();

    // Для работы с UTF-8 в std::string
    // Включаем локаль
    std::locale::global(std::locale(""));
    std::wcout.imbue(std::locale());
    std::wcin.imbue(std::locale());

    // Способ 1: Использование широких строк (wstring)
    std::wstring russianText = L"Привет! Русский текст и символы: ";
    std::wcout << russianText << std::endl;

    // Unicode символы
    std::wcout << L"Блоки разных стилей:" << std::endl;
    std::wcout << L"▓▓▓▓▓▓▓▓▓▓ 100%" << std::endl;
    std::wcout << L"▒▒▒▒▒▒▒▒▒▒ 75%" << std::endl;
    std::wcout << L"░░░░░░░░░░ 25%" << std::endl;

    // Использование escape-последовательностей в широких строках
    std::wcout << Red << L"Красный русский текст!"
        << Reset << std::endl;
}

// Пример использования всего вместе
int main() {
    // Включение UTF-8
    SetConsoleOutputCP(CP_UTF8);

    // Инициализация
    ConsoleHelper::InitConsole();

    // Вывод русского текста
    std::cout << u8"=== ПРИМЕР ВЫВОДА ===\n";

    // Цветной русский текст
    ConsoleHelper::SetColor(10); // Зеленый
    std::cout << u8"✓ Операция завершена успешно\n";
    ConsoleHelper::SetColor(4); // Красный
    std::cout << u8"✗ Произошла ошибка при выполнении\n";
    ConsoleHelper::SetColor(6); // Желтый
    std::cout << u8"             ⚠\n";
    std::cout << u8"Внимание: низкий заряд батареи\n";
    ConsoleHelper::SetColor(1); // Желтый
    std::cout << u8"            ℹ\n";
    std::cout << u8"Программа инициализирована\n";

    ConsoleHelper::SetColor(7); // Зеленый
    // Вывод блоков и символов
    std::wcout << u8"\nПрогресс-бар:\n";
    std::wcout << u8"[";
    for (int i = 0; i < 10; i++) {
        if (i < 7) {
            ConsoleHelper::SetColor(10); // Зеленый
            std::wcout << u8"█";
        }
        else {
            ConsoleHelper::SetColor(8); // Серый
            std::wcout << u8"░";
        }
    }
    ConsoleHelper::SetColor(7); // Белый
    std::wcout << u8"] 70%\n";

    // Таблица с символами
    std::wcout << u8"\nТаблица символов:\n";
    std::wcout << u8"┌────────────┬────────────┐\n";
    std::wcout << u8"│ Символ     │ Код        │\n";
    std::wcout << u8"├────────────┼────────────┤\n";
    std::wcout << u8"│ █ Полный   │ U+2588     │\n";
    std::wcout << u8"│ ▓ Светлый  │ U+2593     │\n";
    std::wcout << u8"│ ▒ Средний  │ U+2592     │\n";
    std::wcout << u8"│ ░ Темный   │ U+2591     │\n";
    std::wcout << u8"└────────────┴────────────┘\n";

    PrintUnicodeWithWideChars();

    PrintWithUTF8();    
    
    int Mess;
    std::cin >> Mess;
    std::cout << Mess;
}