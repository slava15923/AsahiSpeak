#ifndef WOLFSSL_USER_SETTINGS_H
#define WOLFSSL_USER_SETTINGS_H

/* Отключаем лишние модули, если не нужны */
// #define NO_CRYPT_TEST
// #define NO_CRYPT_BENCHMARK

/* Поддержка TLS 1.2 и 1.3 (по умолчанию) */
#define HAVE_TLS_EXTENSIONS
#define HAVE_SUPPORTED_CURVES
#define HAVE_ECC
#define HAVE_HASHDRBG

/* Для производительности */
#define USE_FAST_MATH
#define TFM_TIMING_RESISTANT

#define WOLFSSL_DTLS
#define WOLFSSL_DTLS_CID

#define OPENSSL_EXTRA
#define HAVE_WOLFSSL_BIO

/* Для работы с системными сертификатами (если нужно) */
// #define HAVE_SYS_CA_CERTS

#endif /* WOLFSSL_USER_SETTINGS_H */