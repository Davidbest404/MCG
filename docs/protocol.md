# Протокол MCG

## Формат фрейма
[u32 frame_size][u16 msg_type][JSON payload]


- `frame_size` — little-endian, размер ВСЕГО фрейма в байтах,
  включая эти 4 байта.
- `msg_type` — little-endian, тип сообщения (см. `MessageType`).
- `payload` — JSON-объект (UTF-8).
- Максимум 16 MB на фрейм (защита от мусора).

## Типы сообщений

| Значение | Имя             | Назначение                                     |
|---------:|-----------------|------------------------------------------------|
| 1        | Ping            | Проверка соединения                            |
| 2        | Pong            | Ответ на Ping                                  |
| 10       | AuthRequest     | Клиент → сервер: имя игрока                    |
| 11       | AuthChallenge   | Сервер → клиент: PoW-задача                    |
| 12       | AuthResponse    | Клиент → сервер: решение PoW + хеш пароля      |
| 13       | AuthResult      | Сервер → клиент: результат                     |
| 20       | ChatMessage     | Сообщение чата (см. §Чат)                      |
| 21       | Command         | Команда от клиента                             |
| 22       | CommandResult   | Ответ на команду                               |
| 30       | EntityUpdate    | Обновление сущности                            |
| 31       | EntityRemove    | Удаление сущности                              |
| 40       | WorldSnapshot   | Полный снапшот мира                            |
| 41       | ChunkData       | Данные чанка                                   |
| 100      | Error           | Ошибка                                         |

## Чат

```json
{
  "from_id": 42,
  "from_name": "Ivan",
  "text": "Привет",
  "scope": "radius",
  "radius": 10,
  "form": "plain",
  "voice_profile": { "pitch": 1.0, "rate": 1.0, "voice_id": "male_01" }
}

scope — global | radius | team | direct.

form — plain | card.

voice_profile — для TTS на клиенте (см. architecture.md §10).

Карточка
Если form = "card":

json
{
  "form": "card",
  "card": {
    "art": "  /\\_/\\\n ( o.o )\n  > ^ <",
    "name": "Ivan the Cat",
    "text": "Приветствую!",
    "frame_id": "user_ivan_default"
  }
}
art — ASCII-рисунок (моноширинный).

frame_id — идентификатор рамки. Клиент ищет
определение в assets/frames/<frame_id>.json. Если не нашёл —
использует "default". Так авторская рамка видна у всех, у кого
есть такой файл, и заменяется дефолтной у остальных.

Пример: вход в игру
Клиент → AuthRequest: {"name": "Ivan"}

Сервер → AuthChallenge: {"task": "sha256(n) ends with 0000", "seed": "..."}

Клиент → AuthResponse: {"solution": "12345", "password_hash": "sha256:..."}

Сервер → AuthResult: {"ok": true, "player_id": 42, "rules": "...", "description": "..."}

Сервер → ChatMessage: приветствие от System.