# RAFS

Файловая система для Linux: модуль ядра регистрирует тип `rafs` в VFS. Данные не привязаны к блочному устройству — их отдаёт бэкенд, выбранный при сборке.

---

## Идея в двух словах

- **Сверху** — обычные хуки VFS: `super.c` (монтирование), `inode.c`, `dir.c`, `file.c`, плюс вход в `rafs.c`.
- **Снизу** — таблица функций `rafs_backend_ops`: всё хранилище сводится к одному набору вызовов.

Так разделяется «как ядро ходит в ФС» и «где на самом деле лежат inode, имена и байты».

Интерфейс бэкенда выглядит примерно так (фрагмент из `source/api/api.h`):

```c
struct rafs_backend_ops {
    int (*init)(struct super_block *sb, const char *token);
    void (*destroy)(struct super_block *sb);

    struct rafs_file_info* (*lookup)(struct super_block *sb, ino_t parent_ino, const char *name);
    struct rafs_file_info* (*create)(struct super_block *sb, ino_t parent_ino, const char *name, umode_t mode);
    ssize_t (*read)(struct rafs_file_info *file, char *buffer, size_t len, loff_t offset);
    ssize_t (*write)(struct rafs_file_info *file, const char *buffer, size_t len, loff_t offset);
    int (*readdir)(struct super_block *sb, ino_t dir_ino, struct dir_context *ctx);
    // …unlink, rmdir, link, счётчики каталога, освобождение rafs_file_info
};
```

Какой набор указателей подставить в `rafs_backend_ops`, решает `source/api/select.c` в зависимости от макроса `RAFS_BACKEND_RAM` или `RAFS_BACKEND_NET`.

Строка из `mount` попадает в бэкенд как токен — её читает `rafs_fill_super` и передаёт в `init`:

```c
const char *token = (const char *)data;
ret = rafs_backend_ops.init(sb, token);
```

---

## Режимы хранения

**RAM (`-DRAFS_BACKEND_RAM`)**

- Данные только в памяти ядра внутри модуля.
- После `umount` / `rmmod` содержимое исчезает.
- Бэкенд отдельно поднимать не нужно.

**Сеть (`-DRAFS_BACKEND_NET`, сейчас так в Makefile)**

- Модуль дергает HTTP API из каталога `backend` (FastAPI + SQLAlchemy + SQLite).
- Аргумент `mount` — это `token`: в базе им размечаются независимые деревья файлов.
- В `source/api/net/http.c` зашиты адрес и порт клиента ядра:

```c
const char *SERVER_IP = "10.0.2.2";  // типичный «хост» с точки зрения гостя QEMU
const int SERVER_PORT = 8081;
```

Если бэкенд на другой машине или порту — правьте здесь и пересобирайте модуль.

---

## Структура репозитория

- `source/rafs.c`, `super.c`, `inode.c`, `dir.c`, `file.c` — связка с VFS.
- `source/api/` — общий заголовок, `select.c`, RAM- и сетевой бэкенд.
- `backend/src/` — пользовательский сервер и логика «файловой системы» в БД.
- `Makefile` — сборка `rafs.ko` под `$(uname -r)`.
- `mount.sh` — набросок команд; ниже полный ручной сценарий удобнее копировать как есть.

---

## Сборка

**Нужно**

- Исходники/заголовки ядра той же версии, что и у запущенного ядра (часто пакет `linux-headers-$(uname -r)`).

**По умолчанию (сеть)** — в корне репозитория:

```bash
make
```

В `Makefile` сейчас явно задано:

```makefile
EXTRA_CFLAGS = -Wall -g -DRAFS_BACKEND_NET
```

**Переключение на RAM:** замените `-DRAFS_BACKEND_NET` на `-DRAFS_BACKEND_RAM`, выполните `make clean && make`.

---

## Запуск: сетевой режим

1. Убедитесь, что с гостя (или с той машины, где грузится модуль) до бэкенда есть сетевой доступ до `SERVER_IP` и порта из `http.c`.

2. Поставьте зависимости Python (минимум): `fastapi`, `uvicorn`, `sqlalchemy` (+ SQLite обычно уже в комплекте с Python).

3. Запуск сервера из `backend/src` (импорты вида `app.app` рассчитаны на этот каталог; порт по умолчанию `8081`, как в `main.py` и в модуле ядра):

```bash
cd backend/src
python -m uvicorn app.app:app --host 0.0.0.0 --port 8081
```

   То же самое можно сделать командой `python main.py` из этого каталога.

4. На машине с модулем:

```bash
sudo mkdir -p /mnt/rafs
sudo insmod rafs.ko
sudo mount -t rafs мой_токен /mnt/rafs
```

5. Проверка:

```bash
cd /mnt/rafs
ls -la
echo hello > a.txt
cat a.txt
```

6. Аккуратное завершение:

```bash
sudo umount /mnt/rafs
sudo rmmod rafs
```

Логи модуля смотрите через `dmesg` (в коде есть префиксы вроде `[rafs]` и `[rafs-net]`).

---

## Запуск: только RAM

- Соберите с `-DRAFS_BACKEND_RAM`.
- `sudo insmod rafs.ko`
- `sudo mount -t rafs произвольная_строка /mnt/rafs` — для RAM-бэкенда эта строка фактически не задействована (в `ram_backend_init` она не подхватывается как токен).
- Дальше тот же цикл `umount` / `rmmod`.

---

## Ограничения

- RAM: нет персистентности между перезагрузками и выгрузкой модуля.
- Сеть: ФС живёт, пока жив сервер и совпадают IP/порт; каждый сетевой запрос — отдельная цена по сравнению с локальным диском.
- Проект учебный, не рассчитан на продакшен.
