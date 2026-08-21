этот проект задумавается, как простой голосовой чат с интерфейсом

клиент и сервер ещё не реализованы.

всё свежее от разработки будет в ветке unstable


компиляция под: linux mint

#sfml
sudo apt install libpulse-dev libasound2-dev

mkdir build && cd build && cmake .. && cmake --build . && ./AsahiSpeak

поддерживает только моно микрофоны