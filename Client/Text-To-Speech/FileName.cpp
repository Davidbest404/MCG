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
    SEQUENTIAL,  // Play sounds one after another (default)
    SIMULTANEOUS // Play all sounds at once
};

// Sound player class
class SoundPlayer {
private:
    std::map<char, std::string> soundFiles;
    ma_engine engine;
    bool engineInitialized;
    PlaybackMode currentMode;

    // For sequential playback tracking
    std::atomic<bool> isPlaying;
    std::condition_variable soundFinishedCV;
    std::mutex soundMutex;

    // Callback function for sequential playback
    static void sound_end_callback(void* pUserData, ma_sound* pSound) {
        SoundPlayer* player = (SoundPlayer*)pUserData;
        if (player) {
            player->onSoundFinished();
        }
    }

    // Check if directory exists
    bool directoryExists(const std::string& path) {
        struct stat info;
        if (stat(path.c_str(), &info) != 0) {
            return false;
        }
        return (info.st_mode & S_IFDIR) != 0;
    }

    // Get list of files in directory (cross-platform)
    std::vector<std::string> getFilesInDirectory(const std::string& directory) {
        std::vector<std::string> files;

#ifdef _WIN32
        // Windows version
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
        // Linux/Unix version
        DIR* dir;
        struct dirent* ent;

        if ((dir = opendir(directory.c_str())) != NULL) {
            while ((ent = readdir(dir)) != NULL) {
                std::string filename = ent->d_name;

                // Skip current and parent directories
                if (filename != "." && filename != "..") {
                    // Check if it's a file (not directory)
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

    // Get file extension
    std::string getFileExtension(const std::string& filename) {
        size_t pos = filename.find_last_of(".");
        if (pos != std::string::npos) {
            return filename.substr(pos);
        }
        return "";
    }

    // Get filename without extension
    std::string getFileNameWithoutExtension(const std::string& filename) {
        size_t pos = filename.find_last_of(".");
        if (pos != std::string::npos) {
            return filename.substr(0, pos);
        }
        return filename;
    }

    // Extract letter from filename like "letter-a.mp3" or "letter-a.wav"
    char extractLetterFromFilename(const std::string& filename) {
        std::string nameWithoutExt = getFileNameWithoutExtension(filename);

        // Convert to lowercase for processing
        std::string lowerName = nameWithoutExt;
        for (char& c : lowerName) {
            c = std::tolower(c);
        }

        // Check for pattern "letter-x" where x is a letter
        if (lowerName.find("letter-") == 0 && lowerName.length() > 7) {
            // The character after "letter-" should be the letter
            char possibleLetter = lowerName[7];
            if (isalpha(possibleLetter)) {
                return possibleLetter;
            }
        }

        // Also check for direct letter names (as fallback)
        if (lowerName.length() == 1 && isalpha(lowerName[0])) {
            return lowerName[0];
        }

        // Check for space
        if (lowerName == "space" || lowerName == "letter-space") {
            return ' ';
        }

        // Check for newline/enter
        if (lowerName == "enter" || lowerName == "newline" || lowerName == "letter-enter") {
            return '\n';
        }

        return '\0'; // No letter found
    }

    // Check if file exists using ifstream
    bool fileExists(const std::string& filename) {
        std::ifstream file(filename.c_str());
        return file.good();
    }

    // Called when a sound finishes playing (for sequential mode)
    void onSoundFinished() {
        {
            std::lock_guard<std::mutex> lock(soundMutex);
            isPlaying = false;
        }
        soundFinishedCV.notify_one();
    }

    // Wait for current sound to finish (sequential mode)
    void waitForSoundToFinish() {
        std::unique_lock<std::mutex> lock(soundMutex);
        soundFinishedCV.wait(lock, [this] { return !isPlaying; });
    }

public:
    // Constructor scans the sound folder
    SoundPlayer(const std::string& soundFolder) : currentMode(PlaybackMode::SEQUENTIAL), isPlaying(false) {
        // Initialize sound engine
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

            // Try to create directory
#ifdef _WIN32
            _mkdir(soundFolder.c_str());
#else
            mkdir(soundFolder.c_str(), 0777);
#endif
            return;
        }

        // Get list of files in directory
        std::vector<std::string> files = getFilesInDirectory(soundFolder);

        if (files.empty()) {
            std::cout << "'sounds' folder is empty." << std::endl;
        }

        // Scan files
        for (const auto& filename : files) {
            std::string extension = getFileExtension(filename);

            // Convert extension to lowercase for comparison
            std::string extLower = extension;
            for (char& c : extLower) {
                c = std::tolower(c);
            }

            // Supported formats
            if (extLower == ".mp3" || extLower == ".wav" || extLower == ".ogg" || extLower == ".flac") {
                // Extract letter from filename
                char letter = extractLetterFromFilename(filename);

                if (letter != '\0') {
                    // Form full path to file
                    std::string fullPath = soundFolder + "/" + filename;

                    soundFiles[letter] = fullPath;

                    if (letter == ' ') {
                        std::cout << "✅ Loaded sound for space: " << filename << std::endl;
                    }
                    else if (letter == '\n') {
                        std::cout << "✅ Loaded sound for newline: " << filename << std::endl;
                    }
                    else {
                        std::cout << "✅ Loaded sound for letter '" << letter << "': " << filename << std::endl;
                    }
                }
            }
        }

        if (soundFiles.empty()) {
            std::cout << "\n⚠️  No suitable sound files found in 'sounds' folder." << std::endl;
            std::cout << "Add files in .wav, .mp3, .ogg or .flac format:" << std::endl;
            std::cout << "  - For letters: letter-a.mp3, letter-b.wav, letter-c.ogg, etc." << std::endl;
            std::cout << "  - For space: space.mp3, letter-space.mp3, or space.wav" << std::endl;
            std::cout << "  - For newline: enter.mp3 or letter-enter.mp3" << std::endl;
        }
    }

    // Destructor
    ~SoundPlayer() {
        if (engineInitialized) {
            ma_engine_uninit(&engine);
        }
    }

    // Set playback mode
    void setPlaybackMode(PlaybackMode mode) {
        currentMode = mode;
        std::cout << "Playback mode set to: " << (mode == PlaybackMode::SEQUENTIAL ? "SEQUENTIAL" : "SIMULTANEOUS") << std::endl;
    }

    // Toggle playback mode
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

    // Get current playback mode as string
    std::string getCurrentModeString() const {
        return currentMode == PlaybackMode::SEQUENTIAL ? "SEQUENTIAL" : "SIMULTANEOUS";
    }

    // Play sound for a specific character
    bool playSoundForChar(char c) {
        if (!engineInitialized) {
            return false;
        }

        char lowerChar = std::tolower(c);

        // Check if we have sound for this character
        auto it = soundFiles.find(lowerChar);
        if (it == soundFiles.end()) {
            return false;
        }

        std::string filePath = it->second;

        // Check if file exists before playing
        if (!fileExists(filePath)) {
            std::cerr << "File not found: " << filePath << std::endl;
            return false;
        }

        if (currentMode == PlaybackMode::SEQUENTIAL) {
            // For sequential mode, wait for previous sound to finish
            if (isPlaying) {
                waitForSoundToFinish();
            }

            // Create a sound with end callback
            ma_sound sound;
            ma_result result = ma_sound_init_from_file(&engine, filePath.c_str(), 0, NULL, NULL, &sound);

            if (result != MA_SUCCESS) {
                std::cerr << "Error initializing sound: " << filePath << std::endl;
                return false;
            }

            // Set callback for when sound finishes
            ma_sound_set_end_callback(&sound, sound_end_callback, this);

            // Mark as playing
            {
                std::lock_guard<std::mutex> lock(soundMutex);
                isPlaying = true;
            }

            // Start playback
            ma_sound_start(&sound);

            // Wait for sound to finish
            waitForSoundToFinish();

            // Clean up
            ma_sound_uninit(&sound);
        }
        else {
            // Simultaneous mode - just play and forget
            ma_result result = ma_engine_play_sound(&engine, filePath.c_str(), NULL);

            if (result != MA_SUCCESS) {
                std::cerr << "Error playing sound: " << filePath << std::endl;
                return false;
            }
        }

        return true;
    }

    // Check if we have sound for a character
    bool hasSoundForChar(char c) {
        return soundFiles.find(std::tolower(c)) != soundFiles.end();
    }

    // Get list of available letters
    void printAvailableLetters() {
        if (soundFiles.empty()) {
            std::cout << "No sounds available." << std::endl;
            return;
        }

        std::cout << "\n📢 Available sounds for letters: ";
        int count = 0;
        for (const auto& pair : soundFiles) {
            if (pair.first != ' ' && pair.first != '\n') {
                std::cout << pair.first << " ";
                count++;
            }
        }
        std::cout << "(" << count << " letters)" << std::endl;

        if (soundFiles.find(' ') != soundFiles.end()) {
            std::cout << "✅ Sound for space is available" << std::endl;
        }
        if (soundFiles.find('\n') != soundFiles.end()) {
            std::cout << "✅ Sound for newline is available" << std::endl;
        }
    }

    // Get current working directory
    static std::string getCurrentDirectory() {
        char buffer[FILENAME_MAX];
        if (GetCurrentDir(buffer, sizeof(buffer))) {
            return std::string(buffer);
        }
        return "";
    }
};

// Function to speak text
void speakText(SoundPlayer& player, const std::string& text) {
    std::cout << "\n🎤 Speaking: \"" << text << "\"" << std::endl;
    std::cout << "▶️ ";

    for (char c : text) {
        if (player.hasSoundForChar(c)) {
            std::cout << c << std::flush;
            player.playSoundForChar(c);

            // Small pause between sounds (only for visual feedback, not audio)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        else {
            std::cout << "?" << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    std::cout << "\n✅ Done speaking!" << std::endl;
}

int main() {
    std::cout << "===================================" << std::endl;
    std::cout << "   LETTER-BY-LETTER TEXT SPEAKER  " << std::endl;
    std::cout << "===================================" << std::endl;

    std::cout << "Current directory: " << SoundPlayer::getCurrentDirectory() << std::endl;
    std::cout << "Sound folder: " << SoundPlayer::getCurrentDirectory() << "/sounds" << std::endl;
    std::cout << std::endl;

    // Create player and load sounds from "sounds" folder
    SoundPlayer player("sounds");

    player.printAvailableLetters();

    // Interactive mode
    std::cout << "\n📝 Interactive mode. Enter text to speak." << std::endl;
    std::cout << "   Commands: 'exit' - quit, 'list' - show letters, 'test' - test demo" << std::endl;
    std::cout << "            'mode' - toggle between sequential and simultaneous playback" << std::endl;
    std::cout << "   Current mode: " << player.getCurrentModeString() << std::endl;
    std::cout << "   " << std::string(50, '-') << std::endl;

    while (true) {
        std::cout << "\n🔤 Enter text: ";
        std::string text;
        std::getline(std::cin, text);

        // Handle commands
        if (text == "exit" || text == "quit") {
            std::cout << "👋 Goodbye!" << std::endl;
            break;
        }

        if (text == "list") {
            player.printAvailableLetters();
            continue;
        }

        if (text == "mode") {
            player.togglePlaybackMode();
            std::cout << "Current mode: " << player.getCurrentModeString() << std::endl;
            continue;
        }

        if (text == "test") {
            // Test text with different characters
            speakText(player, "hello world");
            continue;
        }

        if (text.empty()) {
            continue;
        }

        // Speak the entered text
        speakText(player, text);
    }

    return 0;
}