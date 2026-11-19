package main

import (
	"math"
	"sync"
	"fmt"
)

const (
	TileGrass = 0
	TileWater = 1
	TileStone = 2
	CHUNK_SIZE = 32
)

type PlayerState struct {
	Username    string  `json:"username"`
	X           float64 `json:"x"`
	Y           float64 `json:"y"`
	Z           float64 `json:"z"`
	W           float64 `json:"w"`
	CameraAngle float64 `json:"camera_angle"`
}

var (
	WorldChunks = make(map[string][][]int) // "cx,cy" -> [][]int
	WorldMapMu  sync.RWMutex
	Players     = make(map[string]*PlayerState)
	PlayersMu   sync.RWMutex
)

func isWalkable(x, y, z int) bool {
	WorldMapMu.RLock()
	defer WorldMapMu.RUnlock()
	
	cx := int(math.Floor(float64(x) / float64(CHUNK_SIZE)))
	cy := int(math.Floor(float64(y) / float64(CHUNK_SIZE)))
	chunk, ok := WorldChunks[chunkKey(cx, cy)]
	if !ok { return true } // За пределами сгенерированного - идем
	
	lx := x - (cx * CHUNK_SIZE)
	ly := y - (cy * CHUNK_SIZE)
	
	if ly < 0 || ly >= CHUNK_SIZE || lx < 0 || lx >= CHUNK_SIZE { return false }
	if z > 0 { return true } 
	return chunk[ly][lx] != TileWater
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

func chunkKey(cx, cy int) string {
	return fmt.Sprintf("%d,%d", cx, cy)
}