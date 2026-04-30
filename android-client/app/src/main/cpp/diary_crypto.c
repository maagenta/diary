#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include "libsodium/src/libsodium/include/sodium.h"

#define AUTH_PK_LEN  crypto_sign_PUBLICKEYBYTES   /* 32 */
#define AUTH_SK_LEN  crypto_sign_SECRETKEYBYTES   /* 64 */
#define AUTH_SIG_LEN crypto_sign_BYTES             /* 64 */
#define ENC_PK_LEN   crypto_box_PUBLICKEYBYTES    /* 32 */
#define ENC_SK_LEN   crypto_box_SECRETKEYBYTES    /* 32 */

/* ------------------------------------------------------------------ */
/* sodium_init                                                          */
/* ------------------------------------------------------------------ */

JNIEXPORT jint JNICALL
Java_uk_coko_forge_diary_Crypto_sodiumInit(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    return sodium_init();
}

/* ------------------------------------------------------------------ */
/* Key derivation                                                       */
/* auth_sk (64 bytes) → auth_pk is the last 32 bytes                  */
/* enc_sk  (32 bytes) → enc_pk via scalarmult_base                    */
/* ------------------------------------------------------------------ */

JNIEXPORT jbyteArray JNICALL
Java_uk_coko_forge_diary_Crypto_authPkFromSk(JNIEnv *env, jclass cls,
                                               jbyteArray auth_sk_j) {
    (void)cls;
    jbyte *sk = (*env)->GetByteArrayElements(env, auth_sk_j, NULL);
    jbyteArray pk_j = (*env)->NewByteArray(env, AUTH_PK_LEN);
    jbyte *pk = (*env)->GetByteArrayElements(env, pk_j, NULL);
    memcpy(pk, sk + 32, AUTH_PK_LEN);
    (*env)->ReleaseByteArrayElements(env, auth_sk_j, sk, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, pk_j, pk, 0);
    return pk_j;
}

JNIEXPORT jbyteArray JNICALL
Java_uk_coko_forge_diary_Crypto_encPkFromSk(JNIEnv *env, jclass cls,
                                              jbyteArray enc_sk_j) {
    (void)cls;
    jbyte *sk = (*env)->GetByteArrayElements(env, enc_sk_j, NULL);
    jbyteArray pk_j = (*env)->NewByteArray(env, ENC_PK_LEN);
    jbyte *pk = (*env)->GetByteArrayElements(env, pk_j, NULL);
    crypto_scalarmult_base((unsigned char *)pk, (unsigned char *)sk);
    (*env)->ReleaseByteArrayElements(env, enc_sk_j, sk, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, pk_j, pk, 0);
    return pk_j;
}

/* ------------------------------------------------------------------ */
/* Sign challenge (Ed25519)                                             */
/* ------------------------------------------------------------------ */

JNIEXPORT jbyteArray JNICALL
Java_uk_coko_forge_diary_Crypto_signChallenge(JNIEnv *env, jclass cls,
                                               jbyteArray challenge_j,
                                               jbyteArray auth_sk_j) {
    (void)cls;
    jbyte *challenge = (*env)->GetByteArrayElements(env, challenge_j, NULL);
    jsize  clen      = (*env)->GetArrayLength(env, challenge_j);
    jbyte *sk        = (*env)->GetByteArrayElements(env, auth_sk_j, NULL);

    jbyteArray sig_j = (*env)->NewByteArray(env, AUTH_SIG_LEN);
    jbyte *sig = (*env)->GetByteArrayElements(env, sig_j, NULL);

    unsigned long long sig_len;
    crypto_sign_detached((unsigned char *)sig, &sig_len,
                          (unsigned char *)challenge, (unsigned long long)clen,
                          (unsigned char *)sk);

    (*env)->ReleaseByteArrayElements(env, challenge_j, challenge, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, auth_sk_j, sk, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, sig_j, sig, 0);
    return sig_j;
}

/* ------------------------------------------------------------------ */
/* Seal (encrypt) entry — crypto_box_seal                              */
/* ------------------------------------------------------------------ */

JNIEXPORT jbyteArray JNICALL
Java_uk_coko_forge_diary_Crypto_seal(JNIEnv *env, jclass cls,
                                      jbyteArray msg_j,
                                      jbyteArray enc_pk_j) {
    (void)cls;
    jbyte *msg    = (*env)->GetByteArrayElements(env, msg_j, NULL);
    jsize  msglen = (*env)->GetArrayLength(env, msg_j);
    jbyte *pk     = (*env)->GetByteArrayElements(env, enc_pk_j, NULL);

    size_t cipher_len = (size_t)msglen + crypto_box_SEALBYTES;
    jbyteArray cipher_j = (*env)->NewByteArray(env, (jsize)cipher_len);
    jbyte *cipher = (*env)->GetByteArrayElements(env, cipher_j, NULL);

    crypto_box_seal((unsigned char *)cipher,
                    (unsigned char *)msg, (unsigned long long)msglen,
                    (unsigned char *)pk);

    (*env)->ReleaseByteArrayElements(env, msg_j, msg, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, enc_pk_j, pk, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, cipher_j, cipher, 0);
    return cipher_j;
}

/* ------------------------------------------------------------------ */
/* Unseal (decrypt) entry — crypto_box_seal_open                       */
/* ------------------------------------------------------------------ */

JNIEXPORT jbyteArray JNICALL
Java_uk_coko_forge_diary_Crypto_unseal(JNIEnv *env, jclass cls,
                                        jbyteArray cipher_j,
                                        jbyteArray enc_pk_j,
                                        jbyteArray enc_sk_j) {
    (void)cls;
    jbyte *cipher    = (*env)->GetByteArrayElements(env, cipher_j, NULL);
    jsize  cipherlen = (*env)->GetArrayLength(env, cipher_j);
    jbyte *pk        = (*env)->GetByteArrayElements(env, enc_pk_j, NULL);
    jbyte *sk        = (*env)->GetByteArrayElements(env, enc_sk_j, NULL);

    if ((size_t)cipherlen < crypto_box_SEALBYTES) {
        (*env)->ReleaseByteArrayElements(env, cipher_j, cipher, JNI_ABORT);
        (*env)->ReleaseByteArrayElements(env, enc_pk_j, pk, JNI_ABORT);
        (*env)->ReleaseByteArrayElements(env, enc_sk_j, sk, JNI_ABORT);
        return NULL;
    }

    size_t plain_len = (size_t)cipherlen - crypto_box_SEALBYTES;
    jbyteArray plain_j = (*env)->NewByteArray(env, (jsize)plain_len);
    jbyte *plain = (*env)->GetByteArrayElements(env, plain_j, NULL);

    int rc = crypto_box_seal_open((unsigned char *)plain,
                                   (unsigned char *)cipher,
                                   (unsigned long long)cipherlen,
                                   (unsigned char *)pk,
                                   (unsigned char *)sk);

    (*env)->ReleaseByteArrayElements(env, cipher_j, cipher, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, enc_pk_j, pk, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, enc_sk_j, sk, JNI_ABORT);
    (*env)->ReleaseByteArrayElements(env, plain_j, plain, 0);

    if (rc != 0) { return NULL; }
    return plain_j;
}
