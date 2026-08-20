#pragma once
#include <windows.h>
#include <string>

class ConsoleHelper {
public:
    static void InitConsole();
    static void SetConsoleFont();
    static void SetColor(int textColor, int bgColor = 0);
    static void ResetColor();
};