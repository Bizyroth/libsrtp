/*
 * aes_gcm_ossl.c
 *
 * AES Galois Counter Mode
 *
 * John A. Foley
 * Cisco Systems, Inc.
 *
 */

/*
 *
 * Copyright (c) 2013-2017, Cisco Systems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 *
 *   Neither the name of the Cisco Systems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#include "aes_icm_mbedtls.h"
#include "aes_gcm_mbedtls.h"
#include "alloc.h"
#include "err.h"                /* for srtp_debug */
#include "crypto_types.h"


srtp_debug_module_t srtp_mod_aes_gcm = {
    0,               /* debugging is off by default */
    "aes gcm"        /* printable module name       */
};

/*
 * The following are the global singleton instances for the
 * 128-bit and 256-bit GCM ciphers.
 */
extern const srtp_cipher_type_t srtp_aes_gcm_128_mbedtls;
extern const srtp_cipher_type_t srtp_aes_gcm_256_mbedtls;

/*
 * For now we only support 8 and 16 octet tags.  The spec allows for
 * optional 12 byte tag, which may be supported in the future.
 */
#define GCM_AUTH_TAG_LEN    16
#define GCM_AUTH_TAG_LEN_8  8


/*
 * This function allocates a new instance of this crypto engine.
 * The key_len parameter should be one of 28 or 44 for
 * AES-128-GCM or AES-256-GCM respectively.  Note that the
 * key length includes the 14 byte salt value that is used when
 * initializing the KDF.
 */
static srtp_err_status_t srtp_aes_gcm_mbedtls_alloc (srtp_cipher_t **c, int key_len, int tlen)
{
    srtp_aes_gcm_ctx_t *gcm;

    debug_print(srtp_mod_aes_gcm, "allocating cipher with key length %d", key_len);
    debug_print(srtp_mod_aes_gcm, "allocating cipher with tag length %d", tlen);

    /*
     * Verify the key_len is valid for one of: AES-128/256
     */
    if (key_len != SRTP_AES_GCM_128_KEY_LEN_WSALT &&
        key_len != SRTP_AES_GCM_256_KEY_LEN_WSALT) {
        return (srtp_err_status_bad_param);
    }

    if (tlen != GCM_AUTH_TAG_LEN &&
        tlen != GCM_AUTH_TAG_LEN_8) {
        return (srtp_err_status_bad_param);
    }

    /* allocate memory a cipher of type aes_gcm */
    *c = (srtp_cipher_t *)srtp_crypto_alloc(sizeof(srtp_cipher_t));
    if (*c == NULL) {
        return (srtp_err_status_alloc_fail);
    }
    memset(*c, 0x0, sizeof(srtp_cipher_t));

    gcm = (srtp_aes_gcm_ctx_t *)srtp_crypto_alloc(sizeof(srtp_aes_gcm_ctx_t));
    if (gcm == NULL) {
	srtp_crypto_free(*c);	
	*c = NULL;
        return (srtp_err_status_alloc_fail);
    }
    memset(gcm, 0x0, sizeof(srtp_aes_gcm_ctx_t));

    mbedtls_cipher_init(&(gcm->ctxe));
    mbedtls_cipher_init(&(gcm->ctxd));

    if (&(gcm->ctxe) == NULL || &(gcm->ctxd) == NULL) {
        srtp_crypto_free(gcm);
        srtp_crypto_free(*c);
        *c = NULL;
        return srtp_err_status_alloc_fail;
    }

    /* set pointers */
    (*c)->state = gcm;
    /* setup cipher attributes */
    switch (key_len) {
    case SRTP_AES_GCM_128_KEY_LEN_WSALT:
        (*c)->type = &srtp_aes_gcm_128_mbedtls;
        (*c)->algorithm = SRTP_AES_GCM_128;
        gcm->key_size = SRTP_AES_128_KEY_LEN;
        gcm->tag_len = tlen;
        break;
    case SRTP_AES_GCM_256_KEY_LEN_WSALT:
        (*c)->type = &srtp_aes_gcm_256_mbedtls;
        (*c)->algorithm = SRTP_AES_GCM_256;
        gcm->key_size = SRTP_AES_256_KEY_LEN;
        gcm->tag_len = tlen;
        break;
    }

    /* set key size        */
    (*c)->key_len = key_len;

    return (srtp_err_status_ok);
}


/*
 * This function deallocates a GCM session
 */
static srtp_err_status_t srtp_aes_gcm_mbedtls_dealloc (srtp_cipher_t *c)
{
    srtp_aes_gcm_ctx_t *ctx;

    ctx = (srtp_aes_gcm_ctx_t*)c->state;
    if (ctx) {
    	mbedtls_cipher_free(&(ctx->ctxe));
    	mbedtls_cipher_free(&(ctx->ctxd));
        /* zeroize the key material */
        octet_string_set_to_zero(ctx, sizeof(srtp_aes_gcm_ctx_t));
        srtp_crypto_free(ctx);
    }

    /* free memory */
    srtp_crypto_free(c);

    return (srtp_err_status_ok);
}

/*
 * aes_gcm_openssl_context_init(...) initializes the aes_gcm_context
 * using the value in key[].
 *
 * the key is the secret key
 *
 * We need to initialize 2 context because of the construction of the function
 * We MUST set the cipher direction during the initialization of the key but cipher direction's initialization
 * is done during iv initialization.
 *
 * We MUST set two contexts because of the prototype of the function. Encryption direction is set
 * with the iv initialization in Openssl's version but with mbedtls, encryption direction MUST be set with the key
 */
static srtp_err_status_t srtp_aes_gcm_mbedtls_context_init (void* cv, const uint8_t *key)
{
    srtp_aes_gcm_ctx_t *c = (srtp_aes_gcm_ctx_t *)cv;
    mbedtls_cipher_type_t evp;

    c->dir = srtp_direction_any;
    c->aad_len=0;
    debug_print(srtp_mod_aes_gcm, "key:  %s", srtp_octet_string_hex_string(key, c->key_size));
    switch (c->key_size) {
    case SRTP_AES_256_KEY_LEN:
        evp = MBEDTLS_CIPHER_AES_256_GCM;
        break;
    case SRTP_AES_128_KEY_LEN:
        evp = MBEDTLS_CIPHER_AES_128_GCM;
        break;
    default:
        return (srtp_err_status_bad_param);
        break;
    }
    if (mbedtls_cipher_setup(&(c->ctxe),mbedtls_cipher_info_from_type(evp))!=0){
    	return (srtp_err_status_init_fail);
    }
    if (mbedtls_cipher_setup(&(c->ctxd),mbedtls_cipher_info_from_type(evp))!=0){
    	return (srtp_err_status_init_fail);
    }

    if(mbedtls_cipher_setkey(&(c->ctxe), key, c->key_size*8, MBEDTLS_ENCRYPT)!=0) {
        return (srtp_err_status_init_fail);
    }
    if(mbedtls_cipher_setkey(&(c->ctxd), key, c->key_size*8, MBEDTLS_DECRYPT)!=0) {
        return (srtp_err_status_init_fail);
    }

    return (srtp_err_status_ok);
}

/*
 * aes_gcm_openssl_set_iv(c, iv) sets the counter value to iv
 */
static srtp_err_status_t srtp_aes_gcm_mbedtls_set_iv (void *cv, uint8_t *iv, srtp_cipher_direction_t direction)
{
    srtp_aes_gcm_ctx_t *c = (srtp_aes_gcm_ctx_t *)cv;

    if (direction != srtp_direction_encrypt && direction != srtp_direction_decrypt) {
        return (srtp_err_status_bad_param);
    }
    c->dir = direction;
    mbedtls_cipher_context_t * ct=(c->dir==srtp_direction_encrypt)? &(c->ctxe):&(c->ctxd);

    debug_print(srtp_mod_aes_gcm, "setting iv: %s", v128_hex_string((v128_t*)iv));
    /*
     *  We call mbedtls_cipher_reset before update IV.
     *
     */
    if(mbedtls_cipher_reset(ct)!=0){
    		return (srtp_err_status_algo_fail);
    }

	if (mbedtls_cipher_set_iv(ct, iv, 12)!=0){
		return (srtp_err_status_init_fail);
	}

	/*
	 * Need to be done to begin qith the rigth counter
	 * mbedtls_gcm_update is called when we set the AAD
	 *
	 */
	if(mbedtls_cipher_update_ad(ct, NULL,0)!=0){
		return (srtp_err_status_init_fail);
	}

   	return (srtp_err_status_ok);
}

/*
 * This function processes the AAD
 *
 * Parameters:
 *	c	Crypto context
 *	aad	Additional data to process for AEAD cipher suites
 *	aad_len	length of aad buffer
 *
 *	We dont set the aad in the context in this function:
 *	Some tests add AAD in two times however we cant call 2 times
 *	mbedtls_cipher_update_aad. So we bufferize them and we add them
 *	at the begining of encryption/decryption function
 *
 */
static srtp_err_status_t srtp_aes_gcm_mbedtls_set_aad (void *cv, const uint8_t *aad, uint32_t aad_len)
{
	srtp_aes_gcm_ctx_t *c = (srtp_aes_gcm_ctx_t *)cv;
	memcpy(c->aad+c->aad_len,aad,aad_len);
	c->aad_len+=aad_len;
    return (srtp_err_status_ok);
}

/*
 * This function encrypts a buffer using AES GCM mode
 *
 * Parameters:
 *	c	Crypto context
 *	buf	data to encrypt
 *	enc_len	length of encrypt buffer
 */
static srtp_err_status_t srtp_aes_gcm_mbedtls_encrypt (void *cv, unsigned char *buf, unsigned int *enc_len)
{
    srtp_aes_gcm_ctx_t *c = (srtp_aes_gcm_ctx_t *)cv;
    if (c->dir != srtp_direction_encrypt && c->dir != srtp_direction_decrypt) {
        return (srtp_err_status_bad_param);
    }
    mbedtls_cipher_context_t * ct=(c->dir==srtp_direction_encrypt)? &(c->ctxe):&(c->ctxd);
    size_t len = 0;
    /*
     * Buffer for the output. Output MUST be different from the input in mbedtls_cipher_update
     */
    unsigned char buf_tmp[*enc_len+mbedtls_cipher_get_block_size(&(c->ctxe))];

    if(c->aad_len!=0){
		if(mbedtls_cipher_reset(ct)!=0){
				return (srtp_err_status_algo_fail);
		}
		if(mbedtls_cipher_update_ad(ct, c->aad, (size_t)c->aad_len)!=0){
			return (srtp_err_status_algo_fail);
		}
    }
    /*
     * Encrypt the data
     */
    if (mbedtls_cipher_update_patched(ct, buf, *enc_len, buf_tmp, &len)!=0) {
    	return srtp_err_status_cipher_fail;
    }

    *enc_len = len;

   if (mbedtls_cipher_finish(ct, buf_tmp+*enc_len, &len)!=0) {
	   return srtp_err_status_cipher_fail;
    }

    *enc_len += len;

    memcpy(buf,buf_tmp,*enc_len);

    return (srtp_err_status_ok);
}

/*
 * This function calculates and returns the GCM tag for a given context.
 * This should be called after encrypting the data.  The *len value
 * is increased by the tag size.  The caller must ensure that *buf has
 * enough room to accept the appended tag.
 *
 * Parameters:
 *	c	Crypto context
 *	buf	data to encrypt
 *	len	length of encrypt buffer
 */
static srtp_err_status_t srtp_aes_gcm_mbedtls_get_tag (void *cv, uint8_t *buf, uint32_t *len)
{
    srtp_aes_gcm_ctx_t *c = (srtp_aes_gcm_ctx_t *)cv;
    mbedtls_cipher_context_t * ct=(c->dir==srtp_direction_encrypt)? &(c->ctxe):&(c->ctxd);

    /*
     * Calculate the tag
     */
    if(mbedtls_cipher_write_tag(ct, buf, (size_t)c->tag_len)!=0){
    	return srtp_err_status_cipher_fail;
    }

    /*
     * Set tag length by desired tag size
     */
    *len = c->tag_len;
    return (srtp_err_status_ok);
}


/*
 * This function decrypts a buffer using AES GCM mode
 *
 * Parameters:
 *	c	Crypto context
 *	buf	data to encrypt
 *	enc_len	length of encrypt buffer
 */
static srtp_err_status_t srtp_aes_gcm_mbedtls_decrypt (void *cv, unsigned char *buf, unsigned int *enc_len)
{
    srtp_aes_gcm_ctx_t *c = (srtp_aes_gcm_ctx_t *)cv;
    mbedtls_cipher_context_t * ct=(c->dir==srtp_direction_encrypt)? &(c->ctxe):&(c->ctxd);
    if (c->dir != srtp_direction_encrypt && c->dir != srtp_direction_decrypt) {
        return (srtp_err_status_bad_param);
    }

    if(c->aad_len!=0){
		if(mbedtls_cipher_reset(ct)!=0){
				return (srtp_err_status_algo_fail);
		}
		if(mbedtls_cipher_update_ad(ct, c->aad, (size_t)c->aad_len)!=0){
			return (srtp_err_status_algo_fail);
		}
    }

    /*
     * input MUST be different from output in mbedtls_gcm_auth_decrypt
     */
    size_t len = 0;
    unsigned char buff_tmp[*enc_len];
    unsigned char tag[c->tag_len];

    memcpy(buff_tmp,buf,*enc_len);
    memcpy(tag,buff_tmp+*enc_len-c->tag_len,c->tag_len);
    /*
     * Decrypt the data
     */

    if (mbedtls_cipher_update(ct, buff_tmp, *enc_len-c->tag_len, buf, &len)!=0) {
    	return srtp_err_status_cipher_fail;
    }
    *enc_len = len;

    if (mbedtls_cipher_finish(ct, buf+(*enc_len), &len)!=0) {
        return srtp_err_status_cipher_fail;
    }
    *enc_len += len;

    /*
     * Check the tag
     */
    if(mbedtls_cipher_check_tag(ct, tag, c->tag_len)!=0){
    	return srtp_err_status_auth_fail;
    }

    return (srtp_err_status_ok);
}



/*
 * Name of this crypto engine
 */
static const char srtp_aes_gcm_128_mbedtls_description[] = "AES-128 GCM using mbedtls";
static const char srtp_aes_gcm_256_mbedtls_description[] = "AES-256 GCM using mbedtls";


/*
 * KAT values for AES self-test.  These
 * values we're derived from independent test code
 * using OpenSSL.
 */
static const uint8_t srtp_aes_gcm_test_case_0_key[SRTP_AES_GCM_128_KEY_LEN_WSALT] = {
    0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
    0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c,
};

static uint8_t srtp_aes_gcm_test_case_0_iv[12] = {
    0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad,
    0xde, 0xca, 0xf8, 0x88
};

static const uint8_t srtp_aes_gcm_test_case_0_plaintext[60] =  {
    0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5,
    0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
    0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
    0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
    0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53,
    0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
    0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57,
    0xba, 0x63, 0x7b, 0x39
};

static const uint8_t srtp_aes_gcm_test_case_0_aad[20] = {
    0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
    0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
    0xab, 0xad, 0xda, 0xd2
};

static const uint8_t srtp_aes_gcm_test_case_0_ciphertext[76] = {
    0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24,
    0x4b, 0x72, 0x21, 0xb7, 0x84, 0xd0, 0xd4, 0x9c,
    0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0,
    0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac, 0xa1, 0x2e,
    0x21, 0xd5, 0x14, 0xb2, 0x54, 0x66, 0x93, 0x1c,
    0x7d, 0x8f, 0x6a, 0x5a, 0xac, 0x84, 0xaa, 0x05,
    0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97,
    0x3d, 0x58, 0xe0, 0x91,
    /* the last 16 bytes are the tag */
    0x5b, 0xc9, 0x4f, 0xbc, 0x32, 0x21, 0xa5, 0xdb,
    0x94, 0xfa, 0xe9, 0x5a, 0xe7, 0x12, 0x1a, 0x47,
};

static const srtp_cipher_test_case_t srtp_aes_gcm_test_case_0a = {
    SRTP_AES_GCM_128_KEY_LEN_WSALT,      /* octets in key            */
    srtp_aes_gcm_test_case_0_key,        /* key                      */
    srtp_aes_gcm_test_case_0_iv,         /* packet index             */
    60,                                  /* octets in plaintext      */
    srtp_aes_gcm_test_case_0_plaintext,  /* plaintext                */
    68,                                  /* octets in ciphertext     */
    srtp_aes_gcm_test_case_0_ciphertext, /* ciphertext  + tag        */
    20,                                  /* octets in AAD            */
    srtp_aes_gcm_test_case_0_aad,        /* AAD                      */
    GCM_AUTH_TAG_LEN_8,
    NULL                                 /* pointer to next testcase */
};

static const srtp_cipher_test_case_t srtp_aes_gcm_test_case_0 = {
    SRTP_AES_GCM_128_KEY_LEN_WSALT,      /* octets in key            */
    srtp_aes_gcm_test_case_0_key,        /* key                      */
    srtp_aes_gcm_test_case_0_iv,         /* packet index             */
    60,                                  /* octets in plaintext      */
    srtp_aes_gcm_test_case_0_plaintext,  /* plaintext                */
    76,                                  /* octets in ciphertext     */
    srtp_aes_gcm_test_case_0_ciphertext, /* ciphertext  + tag        */
    20,                                  /* octets in AAD            */
    srtp_aes_gcm_test_case_0_aad,        /* AAD                      */
    GCM_AUTH_TAG_LEN,
    &srtp_aes_gcm_test_case_0a           /* pointer to next testcase */
};

static const uint8_t srtp_aes_gcm_test_case_1_key[SRTP_AES_GCM_256_KEY_LEN_WSALT] = {
    0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
    0xa5, 0x59, 0x09, 0xc5, 0x54, 0x66, 0x93, 0x1c,
    0xaf, 0xf5, 0x26, 0x9a, 0x21, 0xd5, 0x14, 0xb2,
    0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c,

};

static uint8_t srtp_aes_gcm_test_case_1_iv[12] = {
    0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad,
    0xde, 0xca, 0xf8, 0x88
};

static const uint8_t srtp_aes_gcm_test_case_1_plaintext[60] =  {
    0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5,
    0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
    0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
    0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
    0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53,
    0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
    0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57,
    0xba, 0x63, 0x7b, 0x39
};

static const uint8_t srtp_aes_gcm_test_case_1_aad[20] = {
    0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
    0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
    0xab, 0xad, 0xda, 0xd2
};

static const uint8_t srtp_aes_gcm_test_case_1_ciphertext[76] = {
    0x0b, 0x11, 0xcf, 0xaf, 0x68, 0x4d, 0xae, 0x46,
    0xc7, 0x90, 0xb8, 0x8e, 0xb7, 0x6a, 0x76, 0x2a,
    0x94, 0x82, 0xca, 0xab, 0x3e, 0x39, 0xd7, 0x86,
    0x1b, 0xc7, 0x93, 0xed, 0x75, 0x7f, 0x23, 0x5a,
    0xda, 0xfd, 0xd3, 0xe2, 0x0e, 0x80, 0x87, 0xa9,
    0x6d, 0xd7, 0xe2, 0x6a, 0x7d, 0x5f, 0xb4, 0x80,
    0xef, 0xef, 0xc5, 0x29, 0x12, 0xd1, 0xaa, 0x10,
    0x09, 0xc9, 0x86, 0xc1,
    /* the last 16 bytes are the tag */
    0x45, 0xbc, 0x03, 0xe6, 0xe1, 0xac, 0x0a, 0x9f,
    0x81, 0xcb, 0x8e, 0x5b, 0x46, 0x65, 0x63, 0x1d,
};

static const srtp_cipher_test_case_t srtp_aes_gcm_test_case_1a = {
    SRTP_AES_GCM_256_KEY_LEN_WSALT,      /* octets in key            */
    srtp_aes_gcm_test_case_1_key,        /* key                      */
    srtp_aes_gcm_test_case_1_iv,         /* packet index             */
    60,                                  /* octets in plaintext      */
    srtp_aes_gcm_test_case_1_plaintext,  /* plaintext                */
    68,                                  /* octets in ciphertext     */
    srtp_aes_gcm_test_case_1_ciphertext, /* ciphertext  + tag        */
    20,                                  /* octets in AAD            */
    srtp_aes_gcm_test_case_1_aad,        /* AAD                      */
    GCM_AUTH_TAG_LEN_8,
    NULL                                 /* pointer to next testcase */
};

static const srtp_cipher_test_case_t srtp_aes_gcm_test_case_1 = {
    SRTP_AES_GCM_256_KEY_LEN_WSALT,      /* octets in key            */
    srtp_aes_gcm_test_case_1_key,        /* key                      */
    srtp_aes_gcm_test_case_1_iv,         /* packet index             */
    60,                                  /* octets in plaintext      */
    srtp_aes_gcm_test_case_1_plaintext,  /* plaintext                */
    76,                                  /* octets in ciphertext     */
    srtp_aes_gcm_test_case_1_ciphertext, /* ciphertext  + tag        */
    20,                                  /* octets in AAD            */
    srtp_aes_gcm_test_case_1_aad,        /* AAD                      */
    GCM_AUTH_TAG_LEN,
    &srtp_aes_gcm_test_case_1a           /* pointer to next testcase */
};

/*
 * This is the vector function table for this crypto engine.
 */
const srtp_cipher_type_t srtp_aes_gcm_128_mbedtls = {
    srtp_aes_gcm_mbedtls_alloc,
    srtp_aes_gcm_mbedtls_dealloc,
    srtp_aes_gcm_mbedtls_context_init,
    srtp_aes_gcm_mbedtls_set_aad,
    srtp_aes_gcm_mbedtls_encrypt,
    srtp_aes_gcm_mbedtls_decrypt,
    srtp_aes_gcm_mbedtls_set_iv,
    srtp_aes_gcm_mbedtls_get_tag,
    srtp_aes_gcm_128_mbedtls_description,
    &srtp_aes_gcm_test_case_0,
    SRTP_AES_GCM_128
};

/*
 * This is the vector function table for this crypto engine.
 */
const srtp_cipher_type_t srtp_aes_gcm_256_mbedtls = {
    srtp_aes_gcm_mbedtls_alloc,
    srtp_aes_gcm_mbedtls_dealloc,
    srtp_aes_gcm_mbedtls_context_init,
    srtp_aes_gcm_mbedtls_set_aad,
    srtp_aes_gcm_mbedtls_encrypt,
    srtp_aes_gcm_mbedtls_decrypt,
    srtp_aes_gcm_mbedtls_set_iv,
    srtp_aes_gcm_mbedtls_get_tag,
    srtp_aes_gcm_256_mbedtls_description,
    &srtp_aes_gcm_test_case_1,
    SRTP_AES_GCM_256
};

