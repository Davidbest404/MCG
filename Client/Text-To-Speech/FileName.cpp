#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <iostream>
#include <string>
#include <map>
#include <cctype>
#include <thread>
#include <chrono>
#include <vector>
#include <fstream>
#include <cstring>
#include <atomic>
#include <condition_variable>
#include <regex>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <queue>

// For cross-platform directory operations
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define GetCurrentDir _getcwd
#else
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#define GetCurrentDir getcwd
#endif

// Playback mode enum
enum class PlaybackMode {
    SEQUENTIAL,
    SIMULTANEOUS
};

// Sound unit type
enum class SoundUnitType {
    LETTER,
    DIGRAPH,
    TRIGRAPH,
    WORD,
    NUMBER,
    CURRENCY,
    PUNCTUATION
};

// Sound mapping structure
struct SoundMapping {
    std::string pattern;
    std::string filename;
    SoundUnitType type;
    int priority;
};

// Number word mapping for English
struct NumberWord {
    int value;
    std::string word;
    std::string soundFile;
};

// Structure to track playing sounds - ИСПРАВЛЕНО: используем указатель
struct PlayingSound {
    ma_sound* sound;  // Теперь указатель, а не объект
    std::chrono::steady_clock::time_point startTime;
    float duration;
};

// Sound player class
class SoundPlayer {
private:
    std::map<char, std::string> letterSounds;
    std::map<std::string, std::string> wordSounds;
    std::map<std::string, std::string> numberSounds;
    std::map<char, std::string> punctuationSounds;
    std::vector<SoundMapping> patternSounds;

    ma_engine engine;
    bool engineInitialized;
    PlaybackMode currentMode;
    bool usePhonemes;
    bool interpretNumbers;

    float overlapFactor;

    std::vector<PlayingSound> activeSounds;
    std::mutex soundsMutex;
    std::thread cleanupThread;
    std::atomic<bool> cleanupRunning;

    const std::vector<std::string> commonDigraphs = {
        "sh", "ch", "th", "ph", "wh", "ck", "ng", "qu",
        "ea", "ee", "oo", "ai", "ay", "oi", "oy", "ou", "ow"
    };

    const std::vector<std::string> commonTrigraphs = {
        "sch", "tch", "dge", "igh", "eau"
    };

    const std::vector<NumberWord> numberWords = {
        {0, "zero", ""}, {1, "one", ""}, {2, "two", ""}, {3, "three", ""},
        {4, "four", ""}, {5, "five", ""}, {6, "six", ""}, {7, "seven", ""},
        {8, "eight", ""}, {9, "nine", ""}, {10, "ten", ""}, {11, "eleven", ""},
        {12, "twelve", ""}, {13, "thirteen", ""}, {14, "fourteen", ""},
        {15, "fifteen", ""}, {16, "sixteen", ""}, {17, "seventeen", ""},
        {18, "eighteen", ""}, {19, "nineteen", ""}, {20, "twenty", ""},
        {30, "thirty", ""}, {40, "forty", ""}, {50, "fifty", ""},
        {60, "sixty", ""}, {70, "seventy", ""}, {80, "eighty", ""},
        {90, "ninety", ""}, {100, "hundred", ""}, {1000, "thousand", ""},
        {1000000, "million", ""}
    };

    // Check if directory exists
    bool directoryExists(const std::string& path) {
        struct stat info;
        if (stat(path.c_str(), &info) != 0) {
            return false;
        }
        return (info.st_mode & S_IFDIR) != 0;
    }

    // Get list of files in directory
    std::vector<std::string> getFilesInDirectory(const std::string& directory) {
        std::vector<std::string> files;
#ifdef _WIN32
        std::string searchPath = directory + "\\*";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    files.push_back(fd.cFileName);
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
#else
        DIR* dir;
        struct dirent* ent;
        if ((dir = opendir(directory.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                std::string filename = ent->d_name;
                if (filename != "." && filename != "..") {
                    std::string fullPath = directory + "/" + filename;
                    struct stat pathStat;
                    if (stat(fullPath.c_str(), &pathStat) == 0 && !S_ISDIR(pathStat.st_mode)) {
                        files.push_back(filename);
                    }
                }
            }
            closedir(dir);
        }
#endif
        return files;
    }

    std::string getFileExtension(const std::string& filename) {
        size_t pos = filename.find_last_of(".");
        if (pos != std::string::npos) return filename.substr(pos);
        return "";
    }

    std::string getFileNameWithoutExtension(const std::string& filename) {
        size_t pos = filename.find_last_of(".");
        if (pos != std::string::npos) return filename.substr(0, pos);
        return filename;
    }

    bool parsePatternFromFilename(const std::string& filename, std::string& pattern, SoundUnitType& type) {
        std::string nameWithoutExt = getFileNameWithoutExtension(filename);
        std::string lowerName = nameWithoutExt;
        for (char& c : lowerName) c = std::tolower(c);

        if (lowerName.find("num-") == 0 && lowerName.length() > 4) {
            pattern = lowerName.substr(4);
            type = SoundUnitType::NUMBER;
            return true;
        }
        if (lowerName.find("curr-") == 0 && lowerName.length() > 5) {
            pattern = lowerName.substr(5);
            type = SoundUnitType::CURRENCY;
            return true;
        }
        if (lowerName.find("punct-") == 0 && lowerName.length() > 6) {
            pattern = lowerName.substr(6);
            type = SoundUnitType::PUNCTUATION;
            return true;
        }
        if (lowerName.find("word-") == 0 && lowerName.length() > 5) {
            pattern = lowerName.substr(5);
            type = SoundUnitType::WORD;
            return true;
        }
        if (lowerName.find("tri-") == 0 && lowerName.length() > 4) {
            pattern = lowerName.substr(4);
            type = SoundUnitType::TRIGRAPH;
            return true;
        }
        if (lowerName.find("di-") == 0 && lowerName.length() > 3) {
            pattern = lowerName.substr(3);
            type = SoundUnitType::DIGRAPH;
            return true;
        }
        if (lowerName.find("letter-") == 0 && lowerName.length() > 7) {
            char possibleLetter = lowerName[7];
            if (isalpha(possibleLetter)) {
                pattern = std::string(1, possibleLetter);
                type = SoundUnitType::LETTER;
                return true;
            }
        }

        for (const auto& digraph : commonDigraphs) {
            if (lowerName == digraph || lowerName == "di-" + digraph) {
                pattern = digraph;
                type = SoundUnitType::DIGRAPH;
                return true;
            }
        }

        for (const auto& trigraph : commonTrigraphs) {
            if (lowerName == trigraph || lowerName == "tri-" + trigraph) {
                pattern = trigraph;
                type = SoundUnitType::TRIGRAPH;
                return true;
            }
        }

        for (const auto& nw : numberWords) {
            if (lowerName == nw.word) {
                pattern = nw.word;
                type = SoundUnitType::NUMBER;
                return true;
            }
        }

        if (lowerName.length() == 1 && isalpha(lowerName[0])) {
            pattern = lowerName;
            type = SoundUnitType::LETTER;
            return true;
        }

        if (lowerName == "space" || lowerName == "letter-space") {
            pattern = " ";
            type = SoundUnitType::LETTER;
            return true;
        }

        return false;
    }

    bool fileExists(const std::string& filename) {
        std::ifstream file(filename.c_str());
        return file.good();
    }

    float getSoundDuration(const std::string& filePath) {
        ma_decoder decoder;
        ma_result result = ma_decoder_init_file(filePath.c_str(), NULL, &decoder);
        if (result != MA_SUCCESS) return 0.1f;

        ma_uint64 lengthInFrames;
        result = ma_decoder_get_length_in_pcm_frames(&decoder, &lengthInFrames);
        if (result != MA_SUCCESS) {
            ma_decoder_uninit(&decoder);
            return 0.1f;
        }

        float duration = static_cast<float>(lengthInFrames) /
            static_cast<float>(decoder.outputSampleRate);
        ma_decoder_uninit(&decoder);
        return duration;
    }

    // ИСПРАВЛЕНО: cleanupFinishedSounds с правильной очисткой
    void cleanupFinishedSounds() {
        while (cleanupRunning) {
            {
                std::lock_guard<std::mutex> lock(soundsMutex);
                auto it = activeSounds.begin();
                while (it != activeSounds.end()) {
                    if (!ma_sound_is_playing(it->sound)) {
                        ma_sound_uninit(it->sound);
                        delete it->sound;  // Освобождаем память
                        it = activeSounds.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void waitForNextSoundStart() {
        if (activeSounds.empty()) return;

        if (overlapFactor >= 1.0f) {
            while (!activeSounds.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return;
        }

        if (overlapFactor <= 0.0f) return;

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        float maxRemaining = 0.0f;

        {
            std::lock_guard<std::mutex> lock(soundsMutex);
            for (const auto& ps : activeSounds) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - ps.startTime).count() / 1000.0f;
                float targetTime = ps.duration * overlapFactor;
                float remaining = targetTime - elapsed;
                if (remaining > maxRemaining) maxRemaining = remaining;
            }
        }

        if (maxRemaining > 0.001f) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<int>(maxRemaining * 1000)));
        }
    }

    bool isNumber(const std::string& s) {
        if (s.empty()) return false;
        size_t start = (s[0] == '-' || s[0] == '+') ? 1 : 0;
        for (size_t i = start; i < s.length(); i++) {
            if (!isdigit(s[i])) return false;
        }
        return true;
    }

    std::string numberToWords(long long n) {
        if (n == 0) return "zero";
        std::vector<std::string> result;
        if (n < 0) {
            result.push_back("minus");
            n = -n;
        }

        struct Scale { long long value; std::string name; };
        std::vector<Scale> scales = {
            {1000000000, "billion"}, {1000000, "million"},
            {1000, "thousand"}, {100, "hundred"}
        };

        for (const auto& scale : scales) {
            if (n >= scale.value) {
                long long count = n / scale.value;
                result.push_back(convertBelowThousand(count));
                result.push_back(scale.name);
                n %= scale.value;
            }
        }

        if (n > 0) result.push_back(convertBelowThousand(n));

        std::stringstream ss;
        for (size_t i = 0; i < result.size(); i++) {
            if (i > 0) ss << " ";
            ss << result[i];
        }
        return ss.str();
    }

    std::string convertBelowThousand(long long n) {
        if (n == 0) return "";
        std::vector<std::string> result;
        if (n >= 100) {
            long long hundreds = n / 100;
            result.push_back(getNumberWord(hundreds * 100));
            n %= 100;
        }
        if (n >= 20) {
            long long tens = (n / 10) * 10;
            result.push_back(getNumberWord(tens));
            n %= 10;
            if (n > 0) result.push_back(getNumberWord(n));
        }
        else if (n > 0) {
            result.push_back(getNumberWord(n));
        }
        std::stringstream ss;
        for (size_t i = 0; i < result.size(); i++) {
            if (i > 0) ss << " ";
            ss << result[i];
        }
        return ss.str();
    }

    std::string getNumberWord(long long value) {
        for (const auto& nw : numberWords) {
            if (nw.value == value) return nw.word;
        }
        return "";
    }

    std::vector<std::string> tokenize(const std::string& text) {
        std::vector<std::string> tokens;
        std::string current;
        for (size_t i = 0; i < text.length(); i++) {
            char c = text[i];
            if (isdigit(c)) {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                std::string number;
                while (i < text.length() && isdigit(text[i])) {
                    number += text[i];
                    i++;
                }
                i--;
                tokens.push_back(number);
            }
            else if (isalpha(c)) {
                current += c;
            }
            else if (c == ' ') {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.push_back(" ");
            }
            else {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
                tokens.push_back(std::string(1, c));
            }
        }
        if (!current.empty()) tokens.push_back(current);
        return tokens;
    }

    std::vector<std::string> splitIntoPhonemes(const std::string& text) {
        std::vector<std::string> result;
        size_t i = 0;
        while (i < text.length()) {
            bool found = false;
            if (i + 2 < text.length() && isalpha(text[i]) && isalpha(text[i + 1]) && isalpha(text[i + 2])) {
                std::string tri = text.substr(i, 3);
                std::string triLower = tri;
                for (char& c : triLower) c = std::tolower(c);
                for (const auto& mapping : patternSounds) {
                    if (mapping.type == SoundUnitType::TRIGRAPH && mapping.pattern == triLower) {
                        result.push_back(tri);
                        i += 3;
                        found = true;
                        break;
                    }
                }
                if (found) continue;
            }
            if (i + 1 < text.length() && isalpha(text[i]) && isalpha(text[i + 1])) {
                std::string di = text.substr(i, 2);
                std::string diLower = di;
                for (char& c : diLower) c = std::tolower(c);
                for (const auto& mapping : patternSounds) {
                    if (mapping.type == SoundUnitType::DIGRAPH && mapping.pattern == diLower) {
                        result.push_back(di);
                        i += 2;
                        found = true;
                        break;
                    }
                }
                if (found) continue;
            }
            std::string single = text.substr(i, 1);
            char c = std::tolower(single[0]);
            if (letterSounds.find(c) != letterSounds.end() || c == ' ') {
                result.push_back(single);
                i += 1;
            }
            else {
                result.push_back(single);
                i += 1;
            }
        }
        return result;
    }

public:
    SoundPlayer(const std::string& soundFolder) :
        currentMode(PlaybackMode::SEQUENTIAL),
        usePhonemes(true),
        interpretNumbers(true),
        overlapFactor(0.0f),
        cleanupRunning(true)
    {
        engineInitialized = false;
        ma_result result = ma_engine_init(NULL, &engine);
        if (result != MA_SUCCESS) {
            std::cerr << "Failed to initialize sound engine!" << std::endl;
            return;
        }
        engineInitialized = true;
        std::cout << "Sound engine initialized successfully!" << std::endl;

        if (!directoryExists(soundFolder)) {
            std::cerr << "Sound folder not found: " << soundFolder << std::endl;
            std::cerr << "Create a 'sounds' folder in the current directory." << std::endl;
#ifdef _WIN32
            _mkdir(soundFolder.c_str());
#else
            mkdir(soundFolder.c_str(), 0777);
#endif
            return;
        }

        std::vector<std::string> files = getFilesInDirectory(soundFolder);
        if (files.empty()) {
            std::cout << "'sounds' folder is empty." << std::endl;
        }

        for (const auto& filename : files) {
            std::string extension = getFileExtension(filename);
            std::string extLower = extension;
            for (char& c : extLower) c = std::tolower(c);

            if (extLower == ".mp3" || extLower == ".wav" || extLower == ".ogg" || extLower == ".flac") {
                std::string pattern;
                SoundUnitType type;
                if (parsePatternFromFilename(filename, pattern, type)) {
                    std::string fullPath = soundFolder + "/" + filename;
                    switch (type) {
                    case SoundUnitType::LETTER:
                        if (pattern.length() == 1) {
                            char letter = pattern[0];
                            letterSounds[letter] = fullPath;
                            if (letter == ' ') {
                                std::cout << "✅ Loaded sound for space: " << filename << std::endl;
                            }
                            else {
                                std::cout << "✅ Loaded sound for letter '" << letter << "': " << filename << std::endl;
                            }
                        }
                        break;
                    case SoundUnitType::NUMBER:
                        numberSounds[pattern] = fullPath;
                        std::cout << "✅ Loaded sound for number word '" << pattern << "': " << filename << std::endl;
                        break;
                    case SoundUnitType::CURRENCY:
                        punctuationSounds[pattern[0]] = fullPath;
                        std::cout << "✅ Loaded sound for currency '" << pattern << "': " << filename << std::endl;
                        break;
                    case SoundUnitType::PUNCTUATION:
                        punctuationSounds[pattern[0]] = fullPath;
                        std::cout << "✅ Loaded sound for punctuation '" << pattern << "': " << filename << std::endl;
                        break;
                    case SoundUnitType::WORD:
                        wordSounds[pattern] = fullPath;
                        std::cout << "✅ Loaded sound for word '" << pattern << "': " << filename << std::endl;
                        break;
                    default:
                        SoundMapping mapping{ pattern, fullPath, type, (int)pattern.length() * 10 };
                        patternSounds.push_back(mapping);
                        std::string typeStr = (type == SoundUnitType::DIGRAPH) ? "digraph" :
                            (type == SoundUnitType::TRIGRAPH) ? "trigraph" : "unknown";
                        std::cout << "✅ Loaded sound for " << typeStr << " '" << pattern << "': " << filename << std::endl;
                        break;
                    }
                }
            }
        }

        std::sort(patternSounds.begin(), patternSounds.end(),
            [](const SoundMapping& a, const SoundMapping& b) {
                return a.priority > b.priority;
            });

        if (letterSounds.empty() && patternSounds.empty() && wordSounds.empty() && numberSounds.empty()) {
            std::cout << "\n⚠️  No suitable sound files found in 'sounds' folder." << std::endl;
            std::cout << "Add files in .wav, .mp3, .ogg or .flac format:" << std::endl;
            std::cout << "  - For letters: letter-a.mp3, letter-b.wav, etc." << std::endl;
            std::cout << "  - For digraphs: di-sh.mp3, di-ch.wav, di-th.ogg, etc." << std::endl;
            std::cout << "  - For trigraphs: tri-sch.mp3, tri-tch.wav, etc." << std::endl;
            std::cout << "  - For whole words: word-hello.mp3, word-world.wav, etc." << std::endl;
            std::cout << "  - For numbers: num-one.mp3, num-two.wav, num-ten.ogg, num-eleven.mp3, etc." << std::endl;
            std::cout << "  - For currency: curr-dollar.mp3, curr-euro.wav, etc." << std::endl;
            std::cout << "  - For punctuation: punct-comma.mp3, punct-period.wav, etc." << std::endl;
            std::cout << "  - For space: space.mp3 or letter-space.mp3" << std::endl;
        }

        cleanupThread = std::thread(&SoundPlayer::cleanupFinishedSounds, this);
    }

    // ИСПРАВЛЕН деструктор
    ~SoundPlayer() {
        cleanupRunning = false;
        if (cleanupThread.joinable()) {
            cleanupThread.join();
        }

        for (auto& ps : activeSounds) {
            if (ps.sound) {
                ma_sound_uninit(ps.sound);
                delete ps.sound;
            }
        }
        activeSounds.clear();

        if (engineInitialized) {
            ma_engine_uninit(&engine);
        }
    }

    void setPlaybackMode(PlaybackMode mode) {
        currentMode = mode;
        std::cout << "Playback mode set to: " << (mode == PlaybackMode::SEQUENTIAL ? "SEQUENTIAL" : "SIMULTANEOUS") << std::endl;
    }

    void togglePlaybackMode() {
        if (currentMode == PlaybackMode::SEQUENTIAL) {
            currentMode = PlaybackMode::SIMULTANEOUS;
            std::cout << "🔊 Switched to SIMULTANEOUS mode (all sounds play at once)" << std::endl;
        }
        else {
            currentMode = PlaybackMode::SEQUENTIAL;
            std::cout << "⏯️  Switched to SEQUENTIAL mode (sounds play one after another)" << std::endl;
        }
    }

    void togglePhonemeMode() {
        usePhonemes = !usePhonemes;
        if (usePhonemes) {
            std::cout << "🔤 Switched to PHONEME mode (using sound combinations: sh, ch, th, etc.)" << std::endl;
        }
        else {
            std::cout << "🔡 Switched to LETTER mode (individual letters only)" << std::endl;
        }
    }

    void toggleNumberInterpretation() {
        interpretNumbers = !interpretNumbers;
        if (interpretNumbers) {
            std::cout << "🔢 Numbers will be spoken as words (e.g., 123 → one hundred twenty three)" << std::endl;
        }
        else {
            std::cout << "🔢 Numbers will be spoken digit by digit (e.g., 123 → one two three)" << std::endl;
        }
    }

    void setOverlap(float factor) {
        if (factor < 0) factor = 0;
        if (factor > 1) factor = 1;
        overlapFactor = factor;

        if (overlapFactor == 0) {
            std::cout << "🔄 Overlap: ALL TOGETHER (0% - all sounds start at once)" << std::endl;
        }
        else if (overlapFactor == 1) {
            std::cout << "🔄 Overlap: SEQUENTIAL (100% - next starts after previous ends)" << std::endl;
        }
        else {
            std::cout << "🔄 Overlap: " << (overlapFactor * 100) << "% - next starts when previous is "
                << (overlapFactor * 100) << "% complete" << std::endl;
        }
    }

    std::string getCurrentModeString() const {
        return currentMode == PlaybackMode::SEQUENTIAL ? "SEQUENTIAL" : "SIMULTANEOUS";
    }

    std::string getSoundUnitModeString() const {
        return usePhonemes ? "PHONEME" : "LETTER";
    }

    std::string getNumberModeString() const {
        return interpretNumbers ? "WORDS" : "DIGITS";
    }

    std::string getOverlapString() const {
        std::stringstream ss;
        if (overlapFactor == 0) {
            ss << "ALL TOGETHER";
        }
        else if (overlapFactor == 1) {
            ss << "SEQUENTIAL";
        }
        else {
            ss << std::fixed << std::setprecision(0) << (overlapFactor * 100) << "% overlap";
        }
        return ss.str();
    }

    // ИСПРАВЛЕН метод playSoundForPattern
    bool playSoundForPattern(const std::string& pattern) {
        if (!engineInitialized) {
            return false;
        }

        std::string lowerPattern = pattern;
        for (char& c : lowerPattern) c = std::tolower(c);

        std::string filePath;
        bool found = false;

        auto wordIt = wordSounds.find(lowerPattern);
        if (wordIt != wordSounds.end()) {
            filePath = wordIt->second;
            found = true;
        }

        if (!found) {
            auto numIt = numberSounds.find(lowerPattern);
            if (numIt != numberSounds.end()) {
                filePath = numIt->second;
                found = true;
            }
        }

        if (!found) {
            for (const auto& mapping : patternSounds) {
                if (mapping.pattern == lowerPattern) {
                    filePath = mapping.filename;
                    found = true;
                    break;
                }
            }
        }

        if (!found && pattern.length() == 1) {
            char c = pattern[0];
            auto punctIt = punctuationSounds.find(c);
            if (punctIt != punctuationSounds.end()) {
                filePath = punctIt->second;
                found = true;
            }
        }

        if (!found && pattern.length() == 1) {
            char c = lowerPattern[0];
            auto letterIt = letterSounds.find(c);
            if (letterIt != letterSounds.end()) {
                filePath = letterIt->second;
                found = true;
            }
        }

        if (!found && pattern == " ") {
            auto spaceIt = letterSounds.find(' ');
            if (spaceIt != letterSounds.end()) {
                filePath = spaceIt->second;
                found = true;
            }
        }

        if (!found) return false;

        if (!fileExists(filePath)) {
            std::cerr << "File not found: " << filePath << std::endl;
            return false;
        }

        waitForNextSoundStart();

        // ВАЖНО: Создаем звук в куче
        ma_sound* sound = new ma_sound();
        ma_result result = ma_sound_init_from_file(&engine, filePath.c_str(), 0, NULL, NULL, sound);

        if (result != MA_SUCCESS) {
            std::cerr << "Error initializing sound: " << filePath << std::endl;
            delete sound;
            return false;
        }

        float duration = getSoundDuration(filePath);
        ma_sound_start(sound);

        {
            std::lock_guard<std::mutex> lock(soundsMutex);
            PlayingSound ps;
            ps.sound = sound;
            ps.startTime = std::chrono::steady_clock::now();
            ps.duration = duration;
            activeSounds.push_back(ps);
        }

        return true;
    }

    bool playText(const std::string& text) {
        std::vector<std::string> tokens = tokenize(text);

        std::cout << "\n🎤 Speaking: \"" << text << "\"" << std::endl;
        std::cout << "▶️ ";

        for (const auto& token : tokens) {
            if (token == " ") {
                std::cout << " " << std::flush;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            if (isNumber(token) && interpretNumbers) {
                long long num = std::stoll(token);
                std::string words = numberToWords(num);
                std::istringstream iss(words);
                std::string word;
                while (iss >> word) {
                    std::cout << word << " " << std::flush;
                    if (!playSoundForPattern(word)) {
                        for (char c : word) playSoundForPattern(std::string(1, c));
                    }
                }
            }
            else if (isNumber(token) && !interpretNumbers) {
                for (char c : token) {
                    std::cout << c << std::flush;
                    playSoundForPattern(std::string(1, c));
                }
            }
            else if (usePhonemes && token.length() > 0 && isalpha(token[0])) {
                std::vector<std::string> phonemes = splitIntoPhonemes(token);
                for (const auto& phoneme : phonemes) {
                    std::cout << phoneme << std::flush;
                    if (!playSoundForPattern(phoneme)) {
                        for (char c : phoneme) playSoundForPattern(std::string(1, c));
                    }
                }
            }
            else {
                for (char c : token) {
                    std::cout << c << std::flush;
                    playSoundForPattern(std::string(1, c));
                }
            }
        }

        while (!activeSounds.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        std::cout << "\n✅ Done speaking!" << std::endl;
        return true;
    }

    void printAvailableSounds() {
        if (letterSounds.empty() && patternSounds.empty() && wordSounds.empty() && numberSounds.empty()) {
            std::cout << "No sounds available." << std::endl;
            return;
        }

        if (!letterSounds.empty()) {
            std::cout << "\n📢 Available letters: ";
            int count = 0;
            for (const auto& pair : letterSounds) {
                if (pair.first != ' ' && pair.first != '\n') {
                    std::cout << pair.first << " ";
                    count++;
                }
            }
            std::cout << "(" << count << " letters)" << std::endl;
            if (letterSounds.find(' ') != letterSounds.end()) {
                std::cout << "✅ Sound for space is available" << std::endl;
            }
        }

        if (!numberSounds.empty()) {
            std::cout << "\n🔢 Available number words:" << std::endl;
            for (const auto& pair : numberSounds) {
                std::cout << "  - " << pair.first << std::endl;
            }
        }

        if (!wordSounds.empty()) {
            std::cout << "\n📚 Available whole words:" << std::endl;
            for (const auto& pair : wordSounds) {
                std::cout << "  - " << pair.first << std::endl;
            }
        }

        if (!patternSounds.empty()) {
            std::cout << "\n🔤 Available sound combinations:" << std::endl;
            for (const auto& mapping : patternSounds) {
                std::string typeStr = (mapping.type == SoundUnitType::DIGRAPH) ? "digraph" :
                    (mapping.type == SoundUnitType::TRIGRAPH) ? "trigraph" : "unknown";
                std::cout << "  - " << typeStr << ": '" << mapping.pattern << "'" << std::endl;
            }
        }

        if (!punctuationSounds.empty()) {
            std::cout << "\n🔣 Available punctuation:" << std::endl;
            for (const auto& pair : punctuationSounds) {
                std::cout << "  - '" << pair.first << "'" << std::endl;
            }
        }
    }

    static std::string getCurrentDirectory() {
        char buffer[FILENAME_MAX];
        if (GetCurrentDir(buffer, sizeof(buffer))) {
            return std::string(buffer);
        }
        return "";
    }
};

int main() {
    std::cout << "===================================" << std::endl;
    std::cout << "   ADVANCED TEXT SPEAKER          " << std::endl;
    std::cout << "   with Overlap Control           " << std::endl;
    std::cout << "===================================" << std::endl;

    std::cout << "Current directory: " << SoundPlayer::getCurrentDirectory() << std::endl;
    std::cout << "Sound folder: " << SoundPlayer::getCurrentDirectory() << "/sounds" << std::endl;
    std::cout << std::endl;

    SoundPlayer player("sounds");
    player.printAvailableSounds();

    std::cout << "\n📝 Interactive mode. Enter text to speak." << std::endl;
    std::cout << "   Commands:" << std::endl;
    std::cout << "     'exit' - quit" << std::endl;
    std::cout << "     'list' - show available sounds" << std::endl;
    std::cout << "     'test' - test demo" << std::endl;
    std::cout << "     'mode' - toggle between sequential/simultaneous (deprecated)" << std::endl;
    std::cout << "     'unit' - toggle between letter/phoneme mode" << std::endl;
    std::cout << "     'num'  - toggle between number words/digits" << std::endl;
    std::cout << "     'overlap <0-1>' - set overlap factor (0=all together, 1=sequential)" << std::endl;
    std::cout << "   " << std::string(60, '-') << std::endl;

    while (true) {
        std::cout << "\n🔤 Enter text: ";
        std::string input;
        std::getline(std::cin, input);

        if (input == "exit" || input == "quit") {
            std::cout << "👋 Goodbye!" << std::endl;
            break;
        }

        if (input == "list") {
            player.printAvailableSounds();
            continue;
        }

        if (input == "mode") {
            std::cout << "⚠️  'mode' command is deprecated. Use 'overlap' instead:" << std::endl;
            std::cout << "   overlap 0 - all together" << std::endl;
            std::cout << "   overlap 1 - sequential" << std::endl;
            std::cout << "   overlap 0.5 - 50% overlap" << std::endl;
            continue;
        }

        if (input == "unit") {
            player.togglePhonemeMode();
            std::cout << "Current settings: " << player.getSoundUnitModeString() << " | Numbers: "
                << player.getNumberModeString() << " | Overlap: "
                << player.getOverlapString() << std::endl;
            continue;
        }

        if (input == "num") {
            player.toggleNumberInterpretation();
            std::cout << "Current settings: " << player.getSoundUnitModeString() << " | Numbers: "
                << player.getNumberModeString() << " | Overlap: "
                << player.getOverlapString() << std::endl;
            continue;
        }

        if (input.substr(0, 8) == "overlap ") {
            try {
                float factor = std::stof(input.substr(8));
                player.setOverlap(factor);
                std::cout << "Current settings: " << player.getSoundUnitModeString() << " | Numbers: "
                    << player.getNumberModeString() << " | Overlap: "
                    << player.getOverlapString() << std::endl;
            }
            catch (...) {
                std::cout << "Invalid overlap value. Use: overlap 0.5" << std::endl;
            }
            continue;
        }

        if (input == "test") {
            player.playText("Hello, I have 123 apples and 45 oranges. That costs $50.99!");
            continue;
        }

        if (input.empty()) {
            continue;
        }

        player.playText(input);
    }

    return 0;
}