/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2020 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define _GNU_SOURCE

#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-raw.h"
#include "libsmb2-dcerpc.h"
#include "libsmb2-dcerpc-lsa.h"
#include "libsmb2-dcerpc-srvsvc.h"

#ifndef discard_const
#define discard_const(ptr) ((void *)((intptr_t)(ptr)))
#endif

void dcerpc_set_tctx(struct dcerpc_context *ctx, int tctx);
void dcerpc_set_endian(struct dcerpc_pdu *pdu, int little_endian);
 
int is_finished;
struct ndr_context_handle PolicyHandle;

int usage(void)
{
        fprintf(stderr, "Usage:\n"
                "smb2-dcerpc-coder-test\n\n");
        exit(1);
}


typedef int (*compare_func)(void *ptr1, void *ptr2);

static void test_dcerpc_coder(struct dcerpc_context *dce, char *method,
                              dcerpc_coder coder, compare_func cmp,
                              void *req, int req_size,
                              int expected_offset, uint8_t *expected_data,
                              int print_buf, int endian)
{
        struct dcerpc_pdu *pdu1, *pdu2;
        struct smb2_iovec iov;
        static unsigned char buf[65536];
        int offset;
        int i;
        char *req2 = NULL;
        
        printf("Test codec for %s\n", method);

        /* Encode */
        pdu1 = dcerpc_allocate_pdu(dce, DCERPC_ENCODE, req_size);
        iov.len = 65536;
        iov.buf = buf;
        memset(iov.buf, 0, iov.len);
        offset = 0;
        dcerpc_set_endian(pdu1, endian);
        if (dcerpc_ptr_coder(dce, pdu1, &iov, &offset, req,
                             PTR_REF, coder)) {
                printf("Encoding failed\n");
                exit(20);
        }
        if (offset != expected_offset) {
                printf("Encoding failed 0. Offset/Expected mismatch. %d/%d\n",
                       offset, expected_offset);
                printf("\n");
                exit(20);
        }

        if (print_buf) {
                printf("offset:%d  expected:%d\n", offset, expected_offset);
                for (i = 0; i < offset; i++) {
                        if (i % 8 == 0) printf("[0x%02x]  ", i);
                        printf("0x%02x, ", iov.buf[i]);
                        if (i % 8 == 7) printf("\n");
                }
                printf("\n");
        }
        if (memcmp(iov.buf, expected_data, expected_offset)) {
                printf("Encoding failed 1. Data Mismatch\n");
                for (i = 0; i < expected_offset; i++) {
                        if (iov.buf[i] != expected_data[i]) {
                                printf("[0x%02x]: Expected:0x%02x Got:0x%02x\n", i, expected_data[i], iov.buf[i]);
                        }
                }
                exit(20);
        }
        
        /* Decode it again */
        req2 = calloc(1, req_size);
        pdu2 = dcerpc_allocate_pdu(dce, DCERPC_DECODE, req_size);
        offset = 0;
        dcerpc_set_endian(pdu2, endian);
        if (dcerpc_ptr_coder(dce, pdu2, &iov, &offset, req2,
                             PTR_REF, coder)) {
                printf("Encoding failed\n");
                exit(20);
        }
        if (offset != expected_offset) {
                printf("Encoding failed 2. Offset/Expected mismatch. %d/%d\n",
                       offset, expected_offset);
                exit(20);
        }
        cmp(req, req2);
        dcerpc_free_pdu(dce, pdu1);
        dcerpc_free_pdu(dce, pdu2);
        free(req2);
}

static int compare_utf16(void *ptr1, void *ptr2)
{
        struct dcerpc_utf16 *s1 = ptr1;
        struct dcerpc_utf16 *s2 = ptr2;

        if (strcmp(s1->utf8, s2->utf8)) {
                printf("Compare ->utf8 failed %s != %s\n", s1->utf8, s2->utf8);
                exit(20);
        }
        return 0;
}

static void test_utf16_ndr32_le(struct dcerpc_context *dce)
{
        struct dcerpc_utf16 s1;
        unsigned char buf[] = {
                0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x0a, 0x00, 0x00, 0x00, 0x5c, 0x00, 0x5c, 0x00,
                0x77, 0x00, 0x69, 0x00, 0x6e, 0x00, 0x31, 0x00,
                0x36, 0x00, 0x2d, 0x00, 0x31, 0x00, 0x00, 0x00
        };

        s1.utf8 = "\\\\win16-1";
        dcerpc_set_tctx(dce, 0); /* NDR32 */
        test_dcerpc_coder(dce, "dcerpc_utf16 NDR32 LE",
                          dcerpc_utf16z_coder, compare_utf16,
                          &s1, sizeof(s1),
                          sizeof(buf), buf, 0, 1);
}

static void test_utf16_ndr32_be(struct dcerpc_context *dce)
{
        struct dcerpc_utf16 s1;
        unsigned char buf[] = {
                0x00, 0x00, 0x00, 0x0a,  0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x0a,  0x00, 0x5c, 0x00, 0x5c,
                0x00, 0x77, 0x00, 0x69,  0x00, 0x6e, 0x00, 0x31,
                0x00, 0x36, 0x00, 0x2d,  0x00, 0x31, 0x00, 0x00
        };

        s1.utf8 = "\\\\win16-1";
        dcerpc_set_tctx(dce, 0); /* NDR32 */
        test_dcerpc_coder(dce, "dcerpc_utf16 NDR32 BE",
                          dcerpc_utf16z_coder, compare_utf16,
                          &s1, sizeof(s1),
                          sizeof(buf), buf, 0, 0);
}

static void test_utf16_ndr64_le(struct dcerpc_context *dce)
{
        struct dcerpc_utf16 s1;
        unsigned char buf[] = {
0x0a, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
0x0a, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
0x5c, 0x00, 0x5c, 0x00,  0x77, 0x00, 0x69, 0x00,
0x6e, 0x00, 0x31, 0x00,  0x36, 0x00, 0x2d, 0x00,
0x31, 0x00, 0x00, 0x00
        };

        s1.utf8 = "\\\\win16-1";
        dcerpc_set_tctx(dce, 1); /* NDR64 */
        test_dcerpc_coder(dce, "dcerpc_utf16 NDR64 LE",
                          dcerpc_utf16z_coder, compare_utf16,
                          &s1, sizeof(s1),
                          sizeof(buf), buf, 0, 1);
}

/*
  struct srvsvc_SHARE_INFO_1 {
        struct dcerpc_utf16 netname;
        uint32_t type;
        struct dcerpc_utf16 remark;
  };
  int
  srvsvc_SHARE_INFO_1_coder(struct dcerpc_context *ctx,
                            struct dcerpc_pdu *pdu,
                            struct smb2_iovec *iov, int *offset,
                            void *ptr)
*/

static int compare_SHARE_INFO_1(void *ptr1, void *ptr2)
{
        struct srvsvc_SHARE_INFO_1 *s1 = ptr1;
        struct srvsvc_SHARE_INFO_1 *s2 = ptr2;

        if (strcmp(s1->netname.utf8, s2->netname.utf8)) {
                printf("Compare ->netname failed %s != %s\n", s1->netname.utf8, s2->netname.utf8);
                exit(20);
        }
        if (s1->type != s2->type) {
                printf("Compare ->type failed 0x%08x != 0x%08x\n", s1->type, s2->type);
                exit(20);
        }
        if (strcmp(s1->remark.utf8, s2->remark.utf8)) {
                printf("Compare ->remark failed %s != %s\n", s1->remark.utf8, s2->remark.utf8);
                exit(20);
        }
        return 0;
}

static void test_SHARE_INFO_1_ndr32_le(struct dcerpc_context *dce)
{
        struct srvsvc_SHARE_INFO_1 s1;
        unsigned char buf[] = {
                0x55, 0x70, 0x74, 0x72,  0x03, 0x00, 0x00, 0x80,
                0x55, 0x70, 0x74, 0x72,  0x05, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,  0x05, 0x00, 0x00, 0x00,
                0x49, 0x00, 0x50, 0x00,  0x43, 0x00, 0x24, 0x00,
                0x00, 0x00, 0x00, 0x00,  0x0b, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00,  0x0b, 0x00, 0x00, 0x00,
                0x52, 0x00, 0x65, 0x00,  0x6d, 0x00, 0x6f, 0x00,
                0x74, 0x00, 0x65, 0x00,  0x20, 0x00, 0x49, 0x00,
                0x50, 0x00, 0x43, 0x00,  0x00, 0x00
        };

        s1.netname.utf8 = "IPC$";
        s1.type         = 0x80000003;
        s1.remark.utf8  = "Remote IPC";
        dcerpc_set_tctx(dce, 0); /* NDR32 */
        test_dcerpc_coder(dce, "dcerpc_SHARE_INFO_1 NDR32 LE",
                          srvsvc_SHARE_INFO_1_coder, compare_SHARE_INFO_1,
                          &s1, sizeof(s1),
                          sizeof(buf), buf, 0, 1);
}

int main(int argc, char *argv[])
{
        struct smb2_context *smb2;
        struct dcerpc_context *dce;
        char ph[16] = "abcdefghij012345";

        if (argc != 1) {
                usage();
        }

	smb2 = smb2_init_context();
        if (smb2 == NULL) {
                fprintf(stderr, "Failed to init context\n");
                exit(0);
        }

        dce = dcerpc_create_context(smb2);
        if (dce == NULL) {
		printf("Failed to create dce context. %s\n",
                       smb2_get_error(smb2));
		exit(10);
        }

        PolicyHandle.context_handle_attributes = 0;
        memcpy(&PolicyHandle.context_handle_uuid, ph, 16);

        test_utf16_ndr32_le(dce);
        test_utf16_ndr32_be(dce);
        test_utf16_ndr64_le(dce);
        test_SHARE_INFO_1_ndr32_le(dce);
        
        dcerpc_destroy_context(dce);
        smb2_destroy_context(smb2);
        
	return 0;
}
