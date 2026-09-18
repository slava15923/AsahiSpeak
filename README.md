AsahiSpeak version v0.1.0

этот проект задумавается, как простой голосовой чат с интерфейсом

клиент и сервер в процессе реализации.

сервер пока без авторизации, но работает

клиент сырой и без gui пока-что

всё свежее от разработки будет в ветке unstable


работает только со стерео микрофонами

настройки звука: 48кгц, все пакеты по 20мс

компиляция под: linux mint

git clone -r https://github.com/slava15923/AsahiSpeak.git

mkdir build && cd build && cmake .. && cmake --build . && ./AsahiSpeak

поддерживает только моно микрофоны

для интерфейса будет использоваться Qt

openssl genpkey -algorithm RSA -out server-key.pem -pkeyopt rsa_keygen_bits:2048
openssl req -new -x509 -key server-key.pem -out server-cert.pem -days 365 -subj "/CN=localhost"

cmake .. && cmake --build . -j 16

старт клиента: ./AsahiSpeak 

--user - username
--password - password
--address - ip or dns
--port - set port(standart: 15923)
--no-verify - off verifi cert(no recomendeted)

sudo apt install ccache libx11-dev libxtst-dev libxt-dev libxinerama-dev libx11-xcb-dev libxkbcommon-dev libxkbcommon-x11-dev libxkbfile-dev mold

-DASAHI_NO_COMPILE_CLIENT=ON для того чтобы не компилировать клиент