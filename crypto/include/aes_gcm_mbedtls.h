/*
 * aes_gcm_ossl.h
 *
 * Header for AES Galois Counter Mode.
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

#ifndef AES_GCM_OSSL_H
#define AES_GCM_OSSL_H

#include "cipher.h"
#include "srtp.h"
#include "datatypes.h"
#include <../../../mbedtls/include/mbedtls/cipher.h>
#include <../../../mbedtls/include/mbedtls/gcm.h>

typedef struct {
    int key_size;
    int tag_len;
    /*
     * AAD are added in two times during test
     * mbedtls_cipher_update_aad MUST be call just once so we bufferize the aad
     */
    int aad_len;
    unsigned char aad[1024];

    /*
     * ctxe is the context for encryption
     * ctxd is the context for decryption
     */
    mbedtls_cipher_context_t ctxe;
    mbedtls_cipher_context_t ctxd;
    srtp_cipher_direction_t dir;
} srtp_aes_gcm_ctx_t;

#endif /* AES_GCM_OSSL_H */

