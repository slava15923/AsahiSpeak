этот проект задумавается, как простой голосовой чат с интерфейсом

клиент и сервер ещё не реализованы.

всё свежее от разработки будет в ветке unstable

i'm created tg channel for news to development AsahiSpeak: https://t.me/AsahiSpeak


компиляция под: linux mint

#sfml
sudo apt install libpulse-dev libasound2-dev

mkdir build && cd build && cmake .. && cmake --build . && ./AsahiSpeak

поддерживает только моно микрофоны

для интерфейса используется FLTK
openssl genpkey -algorithm RSA -out server-key.pem -pkeyopt rsa_keygen_bits:2048
openssl req -new -x509 -key server-key.pem -out server-cert.pem -days 365 -subj "/CN=localhost"