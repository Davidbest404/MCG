package main

import (
	"context"
	"embed"
	"encoding/json"
	"flag"
	"fmt"
	"io/fs"
	"log"
	"math"
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
	// ... (инициализация порта, конфига, юзеров и Lua) ...
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

// ... (getLocalIP, handleStatus ...) ...
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

	// ... (авторизация) ...
	ctx := r.Context()
	ip := r.RemoteAddr
	if host, _, err := net.SplitHostPort(ip); err == nil { ip = host }
	wsjson.Write(ctx, c, map[string]any{"type": "require_auth"})
	var authMsg map[string]any
	if err := wsjson.Read(ctx, c, &authMsg); err != nil { return }

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
			"ip": s.getLocalIP(), "name": Config.Name, "description": Config.Description,
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

	// Отправляем чанки вокруг игрока при входе
	s.sendSurroundingChunks(c, user.Username)

	wsjson.Write(ctx, c, map[string]any{"type": "players_update", "players": GetAllPlayers()})
	buttons := s.lua.GetUIButtons()
	wsjson.Write(ctx, c, map[string]any{"type": "ui_buttons", "buttons": buttons})

	for {
		var msg map[string]any
		if err := wsjson.Read(ctx, c, &msg); err != nil { return }
		msg["from"] = user.Username

		PlayersMu.RLock()
		if p, ok := Players[user.Username]; ok {
			msg["camera_angle"] = p.CameraAngle
			msg["coordinate_type"] = Config.CoordinateType
		}
		PlayersMu.RUnlock()

		reply := s.lua.OnMessage(msg)
		if reply != nil {
			if reply["type"] == "move" {
				dx, _ := reply["dx"].(float64)
				dy, _ := reply["dy"].(float64)
				
				PlayersMu.RLock()
				oldX, oldY := Players[user.Username].X, Players[user.Username].Y
				PlayersMu.RUnlock()

				MovePlayer(user.Username, dx, dy, 0, 0)
				broadcastPlayerMove(s, user.Username)

				// Если игрок перешел в новый чанк, шлем новые
				PlayersMu.RLock()
				newX, newY := Players[user.Username].X, Players[user.Username].Y
				PlayersMu.RUnlock()
				
				oldCx := int(math.Floor(oldX / CHUNK_SIZE))
				oldCy := int(math.Floor(oldY / CHUNK_SIZE))
				newCx := int(math.Floor(newX / CHUNK_SIZE))
				newCy := int(math.Floor(newY / CHUNK_SIZE))

				if oldCx != newCx || oldCy != newCy {
					s.sendSurroundingChunks(c, user.Username)
				}
				continue
			}

			if reply["type"] == "camera_rotate" {
				angle, _ := reply["angle"].(float64)
				PlayersMu.Lock()
				if p, ok := Players[user.Username]; ok {
					p.CameraAngle += angle
					reply["current_angle"] = p.CameraAngle
				}
				PlayersMu.Unlock()
				wsjson.Write(ctx, c, reply)
				continue
			}

			// ... (остальные обработки broadcast, sender_text и т.д.) ...
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

// Вспомогательная функция: генерация и отправка чанков 3x3 вокруг игрока
func (s *Server) sendSurroundingChunks(c *websocket.Conn, username string) {
	PlayersMu.RLock()
	p := Players[username]
	PlayersMu.RUnlock()

	cx := int(math.Floor(p.X / CHUNK_SIZE))
	cy := int(math.Floor(p.Y / CHUNK_SIZE))

	for dx := -1; dx <= 1; dx++ {
		for dy := -1; dy <= 1; dy++ {
			targetCx := cx + dx
			targetCy := cy + dy
			key := chunkKey(targetCx, targetCy)

			WorldMapMu.Lock()
			chunkData, exists := WorldChunks[key]
			if !exists {
				// Генерируем новый чанк
				chunkData = s.lua.GenerateChunk(targetCx, targetCy, CHUNK_SIZE, Config.MapSeed)
				WorldChunks[key] = chunkData
			}
			WorldMapMu.Unlock()

			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			wsjson.Write(ctx, c, map[string]any{"type": "chunk", "cx": targetCx, "cy": targetCy, "data": chunkData})
			cancel()
		}
	}
}

func broadcastPlayerMove(s *Server, username string) {
	// ... (отправка player_moved) ...
	updateMsg := map[string]any{"type": "player_moved", "username": username}
	PlayersMu.RLock()
	if p, ok := Players[username]; ok { 
		updateMsg["x"] = p.X; updateMsg["y"] = p.Y; updateMsg["z"] = p.Z; updateMsg["w"] = p.W 
	}
	PlayersMu.RUnlock()
	s.broadcastExcept(nil, updateMsg)
}

// ... (остальные функции handleAdminUsers, handleAdminBan, broadcastExcept, handleReload) ...
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