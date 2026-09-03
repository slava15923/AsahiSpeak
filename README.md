этот проект задумавается, как простой голосовой чат с интерфейсом

клиент и сервер в процессе реализации.

всё свежее от разработки будет в ветке unstable


работает только со стерео микрофонами

настройки звука: 48кгц, все пакеты по 20мс

компиляция под: linux mint

#sfml
sudo apt install libpulse-dev libasound2-dev

git clone --recurse-submodules -b unstable https://github.com/slava15923/AsahiSpeak.git

mkdir build && cd build && cmake .. && cmake --build . && ./AsahiSpeak

поддерживает только моно микрофоны

для интерфейса используется FLTK
openssl genpkey -algorithm RSA -out server-key.pem -pkeyopt rsa_keygen_bits:2048
openssl req -new -x509 -key server-key.pem -out server-cert.pem -days 365 -subj "/CN=localhost"

cmake -DWOLFSSL_EXAMPLES=no -DWOLFSSL_CRYPT_TESTS=no -DWOLFSSL_DTLS=ON -DWOLFSSL_DTLS13=ON -DWOLFSSL_DTLS_CID=ON -DWOLFSSL_USER_IO=on-DDEBUG_WOLFSSL=ON -DWOLFSSL_OPENSSLEXTRA=YES -DBUILD_TESTING=OFF -DWOLFSSL_BUILD_TESTS=OFF .. && cmake --build . -j 16

старт клиента: ./AsahiSpeak ip port

sudo apt update
sudo apt install libwebrtc-audio-processing-dev pkg-config
sudo apt install meson ninja-build pkg-config
sudo apt install libabsl-dev