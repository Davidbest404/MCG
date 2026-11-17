// server/main.go
package main

import (
	"context"
	"embed"
	"encoding/json"
	"flag"
	"io/fs"
	"log"
	"net/http"
	"os"
	"time"

	"github.com/coder/websocket"
	"github.com/coder/websocket/wsjson"
)

//go:embed all:web/static
var webFS embed.FS

// Server ...
type Server struct {
	lua *LuaManager
}

func main() {
	// Настройка порта: аргумент > переменная окружения > флаг > по умолчанию
	defaultPort := "8080"
	if envPort := os.Getenv("MCG_PORT"); envPort != "" {
		defaultPort = envPort
	}

	port := flag.String("port", defaultPort, "порт для HTTP/WebSocket сервера")
	flag.Parse()

	// Если пользователь передал аргумент без флага (например, ./mcg-server 8081)
	if flag.NArg() > 0 {
		*port = flag.Arg(0)
	}

	s := &Server{lua: NewLuaManager("plugins")}
	if err := s.lua.LoadAll(); err != nil {
		log.Println("lua load:", err)
	}

	static, _ := fs.Sub(webFS, "web/static")
	http.Handle("/", http.FileServer(http.FS(static)))
	http.HandleFunc("/ws", s.handleWS)
	http.HandleFunc("/api/reload", s.handleReload)

	log.Printf("MCG CDI-AI слушает на :%s\n", *port)
	log.Fatal(http.ListenAndServe(":"+*port, nil))
}

func (s *Server) handleWS(w http.ResponseWriter, r *http.Request) {
	c, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		InsecureSkipVerify: true,
	})
	if err != nil {
		return
	}
	defer c.CloseNow()
	c.SetReadLimit(64 * 1024)

	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()

	wsjson.Write(ctx, c, map[string]any{
		"type": "welcome",
		"text": "Добро пожаловать в MCG (CDI-AI)",
	})

	for {
		readCtx, cancelRead := context.WithTimeout(ctx, 60*time.Second)
		var msg map[string]any
		err := wsjson.Read(readCtx, c, &msg)
		cancelRead()
		if err != nil {
			return
		}

		reply := s.lua.OnMessage(msg)
		if reply != nil {
			writeCtx, cancelWrite := context.WithTimeout(ctx, 10*time.Second)
			wsjson.Write(writeCtx, c, reply)
			cancelWrite()
		}
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