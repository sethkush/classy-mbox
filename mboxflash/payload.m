#import "payload.h"

@implementation MBoxPayloadRecord
@end

static MBoxPayloadRecord *readOne(NSData *blob, NSUInteger off,
                                  NSUInteger *outConsumed) {
    const uint8_t *b = blob.bytes;
    NSUInteger len = blob.length;
    if (off + 12 > len) return nil;
    uint32_t rec_len  = ((uint32_t)b[off+0] << 24) | ((uint32_t)b[off+1] << 16)
                       | ((uint32_t)b[off+2] << 8)  |  (uint32_t)b[off+3];
    uint32_t rec_addr = ((uint32_t)b[off+4] << 24) | ((uint32_t)b[off+5] << 16)
                       | ((uint32_t)b[off+6] << 8)  |  (uint32_t)b[off+7];
    uint32_t rec_type = ((uint32_t)b[off+8] << 24) | ((uint32_t)b[off+9] << 16)
                       | ((uint32_t)b[off+10] << 8) |  (uint32_t)b[off+11];
    if (rec_len == 0 || rec_len > MBOX_PAYLOAD_RECORD_MAX) return nil;
    if (rec_type > 5) return nil;
    if (off + 12 + rec_len > len) return nil;
    MBoxPayloadRecord *r = [MBoxPayloadRecord new];
    r.fileOffset = off;
    r.length     = rec_len;
    r.address    = rec_addr;
    r.type       = rec_type;
    r.data       = [NSData dataWithBytes:b + off + 12 length:rec_len];
    if (outConsumed) *outConsumed = 12 + rec_len;
    return r;
}

NSArray<MBoxPayloadRecord *> *MBoxPayload_Parse(NSData *blob,
                                                NSUInteger startOffset) {
    NSMutableArray<MBoxPayloadRecord *> *out = [NSMutableArray array];
    NSUInteger off = startOffset;
    for (;;) {
        NSUInteger consumed = 0;
        MBoxPayloadRecord *r = readOne(blob, off, &consumed);
        if (!r) break;
        [out addObject:r];
        off += consumed;
        if (r.type == 1) break;  // EOF record
    }
    return out;
}

BOOL MBoxPayload_Autodetect(NSData *blob, NSUInteger *outStartOffset) {
    // First-record signature for any Mbox 1 firmware. Structure:
    //   len = 0x00000020 BE (32), addr = 0, type = 0,
    //   [chksum byte — payload-size-dependent, so wildcarded],
    //   headerSize = 0x12, sig bytes = 0x12 0x34, VID = 0x0dba.
    // The chksum byte was originally pinned to 0x60 (stock rev20's value
    // for payloadSize=8174), which made autodetect reject any custom
    // firmware with a different payload size — e.g. mboxfw's ~2 KB build.
    // Wildcarding it is safe: 0x12 0x12 0x34 (header size + Mbox sig) plus
    // the Digi VID is more than unique enough to autodetect against junk.
    static const uint8_t prefix[] = {
        0x00, 0x00, 0x00, 0x20,     // length 32 BE
        0x00, 0x00, 0x00, 0x00,     // addr 0
        0x00, 0x00, 0x00, 0x00,     // type 0
    };
    static const uint8_t tail[] = {
        0x12, 0x12, 0x34,           // headerSize + signature bytes
        0x0d, 0xba,                 // Digi VID
    };
    NSUInteger sig_len = sizeof(prefix) + 1 + sizeof(tail);
    const uint8_t *b = blob.bytes;
    NSUInteger n = blob.length;
    if (n < sig_len) return NO;
    for (NSUInteger i = 0; i + sig_len <= n; i++) {
        if (memcmp(b + i, prefix, sizeof(prefix)) != 0) continue;
        // b + i + sizeof(prefix) is the chksum byte — accept any value.
        if (memcmp(b + i + sizeof(prefix) + 1, tail, sizeof(tail)) != 0) continue;
        if (outStartOffset) *outStartOffset = i;
        return YES;
    }
    return NO;
}
