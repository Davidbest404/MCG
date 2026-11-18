package main

import (
	"math"
	"sync"
)

const (
	TileGrass = 0
	TileWater = 1
	TileStone = 2
)

type PlayerState struct {
	Username    string  `json:"username"`
	X           float64 `json:"x"`
	Y           float64 `json:"y"`
	Z           float64 `json:"z"`
	W           float64 `json:"w"`
	CameraAngle float64 `json:"camera_angle"` // <--- Угол камеры
}

var (
	WorldMap   [][]int
	WorldMapMu sync.RWMutex
	Players    = make(map[string]*PlayerState)
	PlayersMu  sync.RWMutex
)

func isWalkable(x, y, z int) bool {
	WorldMapMu.RLock()
	defer WorldMapMu.RUnlock()
	if y < 0 || y >= len(WorldMap) || x < 0 || x >= len(WorldMap[0]) { return false }
	if z > 0 { return true } 
	return WorldMap[y][x] != TileWater
}

func MovePlayer(username string, dx, dy, dz, dw float64) {
	PlayersMu.Lock()
	defer PlayersMu.Unlock()

	p, exists := Players[username]
	if !exists { return }

	newX := p.X + dx
	newY := p.Y + dy

	if Config.CoordinateType == "integer" {
		newX = math.Round(newX)
		newY = math.Round(newY)
	}

	if isWalkable(int(newX), int(newY), int(p.Z)) {
		p.X = newX
		p.Y = newY
	}
}

func GetAllPlayers() []*PlayerState {
	PlayersMu.RLock()
	defer PlayersMu.RUnlock()
	var list []*PlayerState
	for _, p := range Players { list = append(list, p) }
	return list
}