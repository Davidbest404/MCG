package main

import (
	"encoding/json"
	"fmt"
	"os"
	"sync"

	"golang.org/x/crypto/bcrypt"
)

type User struct {
	Username     string   `json:"username"`
	PasswordHash string   `json:"password_hash"`
	IPs          []string `json:"ips"`
	IsBanned     bool     `json:"is_banned"`
	IsAdmin      bool     `json:"is_admin"`
}

var (
	users     = make(map[string]*User)
	usersMu   sync.RWMutex
	usersFile = "users.json"
)

func loadUsers() {
	usersMu.Lock()
	defer usersMu.Unlock()
	data, err := os.ReadFile(usersFile)
	if err != nil {
		fmt.Println("Файл users.json не найден, начинаем с чистого листа.")
		return
	}
	if err := json.Unmarshal(data, &users); err != nil {
		fmt.Println("КРИТИЧЕСКАЯ ОШИБКА ЧТЕНИЯ ПОЛЬЗОВАТЕЛЕЙ:", err)
	} else {
		fmt.Printf("Загружено %d пользователей.\n", len(users))
	}
}

// ВАЖНО: эта функция не берет блокировку, так как вызывается из уже заблокированных функций
func saveUsersUnlocked() {
	data, err := json.MarshalIndent(users, "", "  ")
	if err != nil {
		fmt.Println("Ошибка сериализации JSON:", err)
		return
	}
	tmpFile := usersFile + ".tmp"
	if err := os.WriteFile(tmpFile, data, 0644); err != nil {
		fmt.Println("Ошибка записи временного файла:", err)
		return
	}
	if err := os.Rename(tmpFile, usersFile); err != nil {
		fmt.Println("Ошибка замены файла:", err)
	}
}

func Authenticate(username, password, ip string) (*User, error) {
	fmt.Printf("[AUTH] Попытка входа: %s (IP: %s)\n", username, ip)
	usersMu.Lock()
	defer usersMu.Unlock()

	user, exists := users[username]
	if !exists {
		hash, _ := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
		user = &User{
			Username:     username,
			PasswordHash: string(hash),
			IPs:          []string{ip},
			IsAdmin:      len(users) == 0,
		}
		users[username] = user
		saveUsersUnlocked()
		fmt.Println("[AUTH] Зарегистрирован новый игрок:", username)
		return user, nil
	}

	if err := bcrypt.CompareHashAndPassword([]byte(user.PasswordHash), []byte(password)); err != nil {
		fmt.Println("[AUTH] Ошибка: неверный пароль для", username)
		return nil, fmt.Errorf("неверный пароль")
	}

	if user.IsBanned {
		fmt.Println("[AUTH] Ошибка: игрок", username, "забанен")
		return nil, fmt.Errorf("вы забанены на этом сервере")
	}

	hasIP := false
	for _, uIP := range user.IPs {
		if uIP == ip { hasIP = true; break }
	}
	if !hasIP {
		user.IPs = append(user.IPs, ip)
		saveUsersUnlocked()
	}
	fmt.Println("[AUTH] Успешный вход:", username)
	return user, nil
}