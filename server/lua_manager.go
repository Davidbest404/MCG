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

	// Рекурсивно обходим папку plugins
	err := filepath.Walk(m.dir, func(path string, info os.FileInfo, err error) error {
		if err != nil { return err }
		if info.IsDir() || filepath.Ext(path) != ".lua" { return nil } // Ищем только .lua файлы

		L := lua.NewState()
		if err := L.DoFile(path); err != nil {
			fmt.Printf("Ошибка в скрипте %s: %v\n", path, err)
			L.Close()
			return nil
		}
		m.plugins = append(m.plugins, L)
		fmt.Println("Загружен скрипт:", path)
		return nil
	})
	return err
}

func (m *LuaManager) Reload() error {
	return m.LoadAll()
}

func (m *LuaManager) GetUIButtons() []map[string]string {
	m.mu.RLock()
	defer m.mu.RUnlock()
	var allButtons []map[string]string

	for _, L := range m.plugins {
		fn := L.GetGlobal("get_ui_buttons")
		if fn.Type() != lua.LTFunction { continue }
		
		if err := L.CallByParam(lua.P{Fn: fn, NRet: 1, Protect: true}); err == nil {
			ret := L.Get(-1)
			if ret.Type() == lua.LTTable {
				ret.(*lua.LTable).ForEach(func(_, v lua.LValue) {
					if v.Type() == lua.LTTable {
						btn := map[string]string{}
						v.(*lua.LTable).ForEach(func(k, val lua.LValue) {
							btn[k.String()] = val.String()
						})
						allButtons = append(allButtons, btn)
					}
				})
			}
			L.Pop(1)
		}
	}
	return allButtons
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
			ret := L.Get(-1)
			if ret.Type() == lua.LTTable {
				L.Pop(1)
				out := map[string]any{}
				ret.(*lua.LTable).ForEach(func(k, v lua.LValue) {
					switch v.Type() {
					case lua.LTBool:
						out[k.String()] = lua.LVAsBool(v)
					case lua.LTNumber:
						out[k.String()] = float64(v.(lua.LNumber))
					default:
						out[k.String()] = v.String()
					}
				})
				return out // Возвращаем первый ответ
			}
			L.Pop(1)
		}
	}
	return nil
}

func (m *LuaManager) closeAll() {
	for _, L := range m.plugins { L.Close() }
	m.plugins = nil
}