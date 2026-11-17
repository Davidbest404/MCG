package main

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"

	lua "github.com/yuin/gopher-lua"
)

type LuaManager struct {
	mu      sync.RWMutex
	plugins []*lua.LState
	dir     string
}

func NewLuaManager(dir string) *LuaManager {
	return &LuaManager{dir: dir}
}

func (m *LuaManager) LoadAll() error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.closeAll()

	entries, err := os.ReadDir(m.dir)
	if err != nil { return err }

	for _, e := range entries {
		if !e.IsDir() { continue }
		path := filepath.Join(m.dir, e.Name(), "init.lua")
		if _, err := os.Stat(path); err != nil { continue }

		L := lua.NewState()
		L.SetGlobal("mcg_send", L.NewFunction(func(L *lua.LState) int {
			text := L.CheckString(1)
			fmt.Println("[lua->client]", text)
			return 0
		}))
		if err := L.DoFile(path); err != nil {
			fmt.Printf("plugin %s: %v\n", e.Name(), err)
			L.Close()
			continue
		}
		m.plugins = append(m.plugins, L)
		fmt.Println("loaded plugin:", e.Name())
	}
	return nil
}

func (m *LuaManager) Reload() error {
	return m.LoadAll()
}

func (m *LuaManager) OnMessage(msg map[string]any) map[string]any {
	m.mu.RLock()
	defer m.mu.RUnlock()

	for _, L := range m.plugins {
		fn := L.GetGlobal("on_message")
		if fn.Type() != lua.LTFunction { continue }
		tbl := L.NewTable()
		for k, v := range msg { tbl.RawSetString(k, lua.LString(fmt.Sprint(v))) }
		if err := L.CallByParam(lua.P{Fn: fn, NRet: 1, Protect: true}, tbl); err == nil {
			if ret := L.Get(-1); ret.Type() == lua.LTTable {
				L.Pop(1)
				out := map[string]any{}
				ret.(*lua.LTable).ForEach(func(k, v lua.LValue) {
					out[k.String()] = v.String()
				})
				return out
			}
			L.Pop(1)
		}
	}
	return map[string]any{"type": "echo", "text": fmt.Sprint(msg["text"])}
}

func (m *LuaManager) closeAll() {
	for _, L := range m.plugins { L.Close() }
	m.plugins = nil
}