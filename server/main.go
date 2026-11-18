package main

import (
	"context"
	"embed"
	"encoding/json"
	"flag"
	"fmt"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

//go:embed all:web/static
var webFS embed.FS

type Server struct {
	lua     *LuaManager
	mu      sync.Mutex
	clients map[*websocket.Conn]bool
	port    string
}

func main() {
	defaultPort := "8080"
	if envPort := os.Getenv("MCG_PORT"); envPort != "" { defaultPort = envPort }
	port := flag.String("port", defaultPort, "порт сервера")
	flag.Parse()
	if flag.NArg() > 0 { *port = flag.Arg(0) }

	loadConfig()
	loadUsers()

	s := &Server{
		lua:     NewLuaManager("plugins"),
		clients: make(map[*websocket.Conn]bool),
		port:    *port,
	}
	s.lua.LoadAll()

	log.Printf("Генерация мира с сидом: %d...\n", Config.MapSeed)
	WorldMap = s.lua.GenerateMap(100, 100, Config.MapSeed)
	if WorldMap == nil {
		WorldMap = make([][]int, 100)
		for i := range WorldMap { WorldMap[i] = make([]int, 100) }
	}
	log.Println("Мир сгенерирован!")

	static, _ := fs.Sub(webFS, "web/static")
	http.Handle("/", http.FileServer(http.FS(static)))
	http.HandleFunc("/ws", s.handleWS)
	http.HandleFunc("/api/status", s.handleStatus)
	http.HandleFunc("/api/reload", s.handleReload)
	http.HandleFunc("/api/admin/users", s.handleAdminUsers)
	http.HandleFunc("/api/admin/ban", s.handleAdminBan)

	log.Printf("MCG CDI-AI слушает на :%s\n", *port)
	log.Fatal(http.ListenAndServe(":"+*port, nil))
}

func (s *Server) getLocalIP() string {
	conn, err := net.Dial("udp", "8.8.8.8:80")
	if err != nil { return "127.0.0.1:" + s.port }
	defer conn.Close()
	return conn.LocalAddr().(*net.UDPAddr).IP.String() + ":" + s.port
}

func (s *Server) handleStatus(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Content-Type", "application/json")
	configMu.RLock(); defer configMu.RUnlock()
	s.mu.Lock(); playerCount := len(s.clients); s.mu.Unlock()

	status := map[string]any{
		"name": Config.Name, "description": Config.Description,
		"ip": s.getLocalIP(), "players": playerCount, "max_players": Config.MaxPlayers,
	}
	json.NewEncoder(w).Encode(status)
}

func (s *Server) handleWS(w http.ResponseWriter, r *http.Request) {
	c, err := websocket.Accept(w, r, &websocket.AcceptOptions{InsecureSkipVerify: true})
	if err != nil { return }
	defer c.CloseNow()

	ctx := r.Context()
	ip := r.RemoteAddr
	if host, _, err := net.SplitHostPort(ip); err == nil { ip = host }
	
	wsjson.Write(ctx, c, map[string]any{"type": "require_auth"})
	var authMsg map[string]any
	if err := wsjson.Read(ctx, c, &authMsg); err != nil { return }

	if authMsg["type"] != "auth" {
		c.Close(websocket.StatusPolicyViolation, "Ожидалась авторизация")
		return
	}

	username := fmt.Sprint(authMsg["username"])
	password := fmt.Sprint(authMsg["password"])

	user, err := Authenticate(username, password, ip)
	if err != nil {
		wsjson.Write(ctx, c, map[string]any{"type": "auth_error", "text": err.Error()})
		c.Close(websocket.StatusNormalClosure, "auth failed")
		return
	}

	configMu.RLock()
	wsjson.Write(ctx, c, map[string]any{
		"type": "auth_success",
		"user": map[string]any{"username": user.Username, "is_admin": user.IsAdmin},
		"server": map[string]any{
			"ip": s.getLocalIP(), 
			"name": Config.Name, 
			"description": Config.Description,
			"coordinate_type": Config.CoordinateType,
		},
	})
	configMu.RUnlock()

	PlayersMu.Lock()
	Players[user.Username] = &PlayerState{Username: user.Username, X: 50, Y: 50, Z: 0, W: 0, CameraAngle: 0}
	PlayersMu.Unlock()

	s.mu.Lock(); s.clients[c] = true; s.mu.Unlock()
	defer func() { 
		s.mu.Lock(); delete(s.clients, c); s.mu.Unlock()
		PlayersMu.Lock(); delete(Players, user.Username); PlayersMu.Unlock()
	}()

	WorldMapMu.RLock()
	playerSpecificMap := s.lua.ProcessPlayerMap(user.Username, WorldMap)
	wsjson.Write(ctx, c, map[string]any{"type": "map", "data": playerSpecificMap})
	WorldMapMu.RUnlock()
	
	wsjson.Write(ctx, c, map[string]any{"type": "players_update", "players": GetAllPlayers()})

	buttons := s.lua.GetUIButtons()
	wsjson.Write(ctx, c, map[string]any{"type": "ui_buttons", "buttons": buttons})

	for {
		var msg map[string]any
		if err := wsjson.Read(ctx, c, &msg); err != nil { return }
		msg["from"] = user.Username

		// ДОБАВЛЯЕМ УГОЛ КАМЕРЫ В КОНТЕКСТ ДЛЯ LUA
		PlayersMu.RLock()
		if p, ok := Players[user.Username]; ok {
			msg["camera_angle"] = p.CameraAngle
		}
		PlayersMu.RUnlock()

		reply := s.lua.OnMessage(msg)
		if reply != nil {
			// Если Lua вернула движение
			if reply["type"] == "move" {
				dx, _ := reply["dx"].(float64)
				dy, _ := reply["dy"].(float64)
				MovePlayer(user.Username, dx, dy, 0, 0)
				broadcastPlayerMove(s, user.Username)
				continue
			}

			// Если Lua вернула поворот камеры
			if reply["type"] == "camera_rotate" {
				angle, _ := reply["angle"].(float64)
				PlayersMu.Lock()
				if p, ok := Players[user.Username]; ok {
					p.CameraAngle += angle // Меняем угол на сервере
					reply["current_angle"] = p.CameraAngle // Отправляем клиенту для синхронизации
				}
				PlayersMu.Unlock()
				wsjson.Write(ctx, c, reply)
				continue
			}

			if isBroadcast, ok := reply["broadcast"].(bool); ok && isBroadcast {
				s.broadcastExcept(c, reply)
			} else if senderText, ok := reply["sender_text"].(string); ok {
				wsjson.Write(ctx, c, map[string]any{"type": "chat", "from": "Система", "text": senderText})
			} else if othersText, ok := reply["others_text"].(string); ok {
				s.broadcastExcept(c, map[string]any{"type": "chat", "from": "Система", "text": othersText})
			} else {
				wsjson.Write(ctx, c, reply)
			}
		}
	}
}

func broadcastPlayerMove(s *Server, username string) {
	updateMsg := map[string]any{"type": "player_moved", "username": username}
	PlayersMu.RLock()
	if p, ok := Players[username]; ok { 
		updateMsg["x"] = p.X; updateMsg["y"] = p.Y; updateMsg["z"] = p.Z; updateMsg["w"] = p.W 
	}
	PlayersMu.RUnlock()
	s.broadcastExcept(nil, updateMsg)
}

// ... (остальные функции без изменений)
func (s *Server) handleAdminUsers(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	usersMu.RLock(); defer usersMu.RUnlock()
	safeUsers := make(map[string]any)
	for k, u := range users {
		safeUsers[k] = map[string]any{"username": u.Username, "ips": u.IPs, "is_banned": u.IsBanned}
	}
	json.NewEncoder(w).Encode(safeUsers)
}

func (s *Server) handleAdminBan(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost { http.Error(w, "POST only", 405); return }
	var req struct { Username string `json:"username"`; Ban bool `json:"ban"` }
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil { http.Error(w, "Bad request", 400); return }
	
	usersMu.Lock(); defer usersMu.Unlock()
	if u, ok := users[req.Username]; ok {
		u.IsBanned = req.Ban
		saveUsersUnlocked()
	}
	w.WriteHeader(http.StatusOK)
}

func (s *Server) broadcastExcept(sender *websocket.Conn, msg map[string]any) {
	s.mu.Lock(); defer s.mu.Unlock()
	for client := range s.clients {
		if client == sender { continue }
		go func(c *websocket.Conn) {
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			wsjson.Write(ctx, c, msg)
		}(client)
	}
}

func (s *Server) handleReload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost { http.Error(w, "POST only", 405); return }
	loadConfig()
	if err := s.lua.Reload(); err != nil { http.Error(w, err.Error(), 500); return }
	json.NewEncoder(w).Encode(map[string]bool{"ok": true})
}