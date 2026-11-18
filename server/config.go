package main

import (
	"encoding/json"
	"fmt"
	"os"
	"sync"
)

type ServerConfig struct {
	Name           string `json:"name"`
	Description    string `json:"description"`
	MaxPlayers     int    `json:"max_players"`
	MapSeed        int64  `json:"map_seed"`
	CoordinateType string `json:"coordinate_type"`
}

var (
	Config    = ServerConfig{Name: "Тест", Description: "Добро пожаловать!", MaxPlayers: 100, MapSeed: 12345, CoordinateType: "float"}
	configMu  sync.RWMutex
	configFile = "server_config.json"
)

func loadConfig() {
	configMu.Lock()
	defer configMu.Unlock()
	data, err := os.ReadFile(configFile)
	if err != nil {
		saveConfigLocked()
		return
	}
	if err := json.Unmarshal(data, &Config); err != nil {
		fmt.Println("Ошибка чтения конфига:", err)
	}
}

func saveConfigLocked() {
	data, _ := json.MarshalIndent(Config, "", "  ")
	os.WriteFile(configFile, data, 0644)
}