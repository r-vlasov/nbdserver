# nbdserver
NBDServer - серверная часть реализации nbd-протокола.
Во время работы в консоль-лог сервера пишется мета-информация о происходящих процессах внутри


### Реализованный функционал
1) handshake с поддержкой опций NBD_OPT_STRUCTURED_REPLY и NBD_OPT_GO, NBD_CMD_READ и NBD_CMD_DISC.
2) handshake с поддержкой опций NBD_OPT_LIST, NBD_OPT_ABORT 
3) запросы NBD_CMD_READ, NBD_CMD_DISK, NBD_CMD_WRITE ("пустое действие на стороне сервера")
4) работа с несколькими клиентами (процесс)
5) любое количество export'ов (параметризуется через cmdline)
6) обработка дефолтного экспорта (exportname = 'default')

### Структура проекта
  - `iso/` - директория с файлами, которые можно экспортировать (это для тестов). Но можно любые свои файлы (к примеру, `/dev/sda*`, ...)
  - `includes/` - header-файлы
  - `./` - *.c файлы: (соответствующие файлы с заголовками и директивами лежат в includes/)
     - `functions.c` - вспомогательные функции
     - `args.c` -  парсинг командной строки
     - `server.c` - основная логика сервера
     
### Сборка
##### Makefile:
  1) `make image`  - создание образа тестовой файловой системы в виде файла `iso/image.iso`
  2) `make compile` - компиляция из исходников сервера (ELF nbd_server)
  3) `make clean`

### Запуск сервера
`nbd-server -p [port] -d [[file] [name]...]
- `port` - bind-порт сервера
- `file` - export (к примеру /dev/sdb1, ...)
- `name` - exportname для file (это имя нужно будет использовать при подключении с помощью nbdclient с опцией -N)

Пример:
   ` ./nbd_server -p 10808 -d iso/image.iso ISO iso/debian.qcow2 DEBIAN `

### Тестирование
За тестирование (на данном этапе мануальное) отвечает файл `test.sh`  
Использование: ` ./test.sh [test type] [ip-server] [port-server] `

Пример:
   ` ./test.sh qemu-info localhost 10808 ` 
    
 test type:   
   - `qemu-info` - qemu-img info
   - `qemu-iso` - Идет запуск ОС, стартуя с экспортируемого файла. Желательно использовать тот файл, с которого может стартануть QEMU (в iso/ лежит qcow2 debian, можно его)
   - `nbdc-con`- После указания nbd-device (sudo modprobe nbd), exportname, происходит коннект с помощью Linux nbd-client (должен быть предустановлен)
   - `nbdc-disc` - Дисконнект указанного nbd-device от сервера (отправка спец.запроса с помощью nbd-client)
   - `nbdc-list` - Показывает доступные export'ы (exportnames) с помощью nbd-client 

Для справки:
   ` ./test `
 
### Ручная проверка монтирования
Возьмем файл с ФС (либо блочный, либо регулярный, т.е. с образом) - в случае, если сервер запущен на localhost (если нет, то localhost -> routing ip)

1)  На сервере: 
      - `./nbd_server -p 10808 -d iso/image.iso ISO iso/debian.qcow2 DEBIAN`
    На клиенте: 
      - sudo nbd-client localhost 10808 /dev/nbd6 -N ISO
      - mkdir /tmp/mountpoint
      - sudo mount /dev/nbd6 /tmp/mountpoint
    После этого на клиенте в /tmp/mountpoint будет "тестовая файловая система" из iso/image.iso
    Для отключения на клиенте:
      - sudo nbd-client -d /dev/nbd6
      
2) Можно использовать разделы (к примеру sda, sdb, ...) с файловой системой
   На сервере: 
      - `./nbd_server -p 10808 -d /dev/sda1 SDA1
   На клиенте: 
      - sudo nbd-client localhost 10807 /dev/nbd11 -N SDA1
      - mkdir /tmp/mountpoint2
      - sudo mount /dev/nbd11 /tmp/mountpoint2
   После этого раздел sda1 будет примонтирован (если там есть ФС)
   Для отключения на клиенте:
      - sudo nbd-client -d /dev/nbd11

### Выключение сервера - Ctrl + C
