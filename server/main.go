package main

import (
	"context"
	"embed"
	"encoding/json"
	"flag"
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
	if envPort := os.Getenv("MCG_PORT"); envPort != "" {
		defaultPort = envPort
	}

	port := flag.String("port", defaultPort, "порт для HTTP/WebSocket сервера")
	flag.Parse()
	if flag.NArg() > 0 {
		*port = flag.Arg(0)
	}

	s := &Server{
		lua:     NewLuaManager("plugins"),
		clients: make(map[*websocket.Conn]bool),
		port:    *port,
	}
	if err := s.lua.LoadAll(); err != nil {
		log.Println("lua load error:", err)
	}

	static, _ := fs.Sub(webFS, "web/static")
	http.Handle("/", http.FileServer(http.FS(static)))
	http.HandleFunc("/ws", s.handleWS)
	http.HandleFunc("/api/status", s.handleStatus)
	http.HandleFunc("/api/reload", s.handleReload)

	log.Printf("MCG CDI-AI слушает на :%s\n", *port)
	log.Fatal(http.ListenAndServe(":"+*port, nil))
}

func (s *Server) getLocalIP() string {
	conn, err := net.Dial("udp", "8.8.8.8:80")
	if err != nil {
		return "127.0.0.1:" + s.port
	}
	defer conn.Close()
	localAddr := conn.LocalAddr().(*net.UDPAddr)
	return localAddr.IP.String() + ":" + s.port
}

func (s *Server) handleStatus(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Content-Type", "application/json")
	s.mu.Lock()
	playerCount := len(s.clients)
	s.mu.Unlock()

	status := map[string]any{
		"name":        "Тестовый сервер",
		"description": "Сервер CDI-AI",
		"ip":          s.getLocalIP(),
		"players":     playerCount,
	}
	json.NewEncoder(w).Encode(status)
}

func (s *Server) handleWS(w http.ResponseWriter, r *http.Request) {
	c, err := websocket.Accept(w, r, &websocket.AcceptOptions{InsecureSkipVerify: true})
	if err != nil {
		return
	}
	defer c.CloseNow()
	c.SetReadLimit(64 * 1024)

	s.mu.Lock()
	s.clients[c] = true
	s.mu.Unlock()
	defer func() {
		s.mu.Lock()
		delete(s.clients, c)
		s.mu.Unlock()
	}()

	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()

	wsjson.Write(ctx, c, map[string]any{
		"type": "welcome",
		"text": "Добро пожаловать! Сервер: " + s.getLocalIP(),
	})

	// Запрашиваем кнопки (исправлено, никаких := внутри скобок)
	buttons := s.lua.GetUIButtons()
	wsjson.Write(ctx, c, map[string]any{
		"type":    "ui_buttons",
		"buttons": buttons,
	})

	for {
		var msg map[string]any
		err := wsjson.Read(ctx, c, &msg)
		if err != nil {
			return
		}

		reply := s.lua.OnMessage(msg)
		if reply != nil {
			// Логика разделения сообщений
			if senderText, ok := reply["sender_text"].(string); ok {
				wsjson.Write(ctx, c, map[string]any{"type": "chat", "from": "Система", "text": senderText})
			}
			if othersText, ok := reply["others_text"].(string); ok {
				s.broadcastExcept(c, map[string]any{"type": "chat", "from": "Система", "text": othersText})
			}
			if isBroadcast, ok := reply["broadcast"].(bool); ok && isBroadcast {
				s.broadcastExcept(c, reply)
			} else if reply["sender_text"] == nil && reply["others_text"] == nil {
				wsjson.Write(ctx, c, reply)
			}
		}
	}
}

func (s *Server) broadcastExcept(sender *websocket.Conn, msg map[string]any) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for client := range s.clients {
		if client == sender {
			continue
		}
		go func(c *websocket.Conn) {
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			wsjson.Write(ctx, c, msg)
		}(client)
	}
}

func (s *Server) handleReload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", 405)
		return
	}
	if err := s.lua.Reload(); err != nil {
		http.Error(w, err.Error(), 500)
		return
	}
	json.NewEncoder(w).Encode(map[string]bool{"ok": true})
}