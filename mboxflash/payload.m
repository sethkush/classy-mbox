#import "payload.h"

@implementation MBoxPayloadRecord
@end

// Read record at `off`. Return the record and how many bytes it consumed
// via `outConsumed`. Returns nil if it doesn't parse.
static MBoxPayloadRecord *readOne(NSData *blob, NSUInteger off,
                                  NSUInteger *outConsumed) {
    const uint8_t *bytes = blob.bytes;
    NSUInteger len = blob.length;
    if (off + 16 > len) return nil;

    uint32_t size = (uint32_t)bytes[off]
                 | ((uint32_t)bytes[off+1] << 8)
                 | ((uint32_t)bytes[off+2] << 16)
                 | ((uint32_t)bytes[off+3] << 24);
    if (size == 0 || size > MBOX_PAYLOAD_MAX_RECORD_DATA) return nil;
    if (off + 16 + size > len) return nil;

    MBoxPayloadRecord *r = [MBoxPayloadRecord new];
    r.fileOffset     = off;
    r.header         = [NSData dataWithBytes:bytes + off + 4 length:12];
    r.sequenceNumber = (uint32_t)bytes[off+4]
                    | ((uint32_t)bytes[off+5] << 8)
                    | ((uint32_t)bytes[off+6] << 16)
                    | ((uint32_t)bytes[off+7] << 24);
    r.data = [NSData dataWithBytes:bytes + off + 16 length:size];
    if (outConsumed) *outConsumed = 16 + size;
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
    }
    return out;
}

NSUInteger MBoxPayload_ValidRunLength(NSData *blob, NSUInteger startOffset) {
    NSUInteger n = 0;
    NSUInteger off = startOffset;
    for (;;) {
        NSUInteger consumed = 0;
        MBoxPayloadRecord *r = readOne(blob, off, &consumed);
        if (!r) break;
        n++;
        off += consumed;
    }
    return n;
}

BOOL MBoxPayload_Autodetect(NSData *blob, NSUInteger minRun,
                            NSUInteger *outStartOffset,
                            NSUInteger *outRunLength) {
    NSUInteger bestOff = 0;
    NSUInteger bestRun = 0;
    // Scan word-aligned offsets (payload records use a uint32 length prefix,
    // so alignment matters). Try 4-byte stride first for speed; fall back
    // to byte-stride if that yields nothing.
    for (NSUInteger off = 0; off + 16 <= blob.length; off += 4) {
        NSUInteger n = MBoxPayload_ValidRunLength(blob, off);
        if (n > bestRun) { bestRun = n; bestOff = off; }
    }
    if (bestRun < minRun) {
        // Retry with byte-stride
        for (NSUInteger off = 0; off + 16 <= blob.length; off++) {
            if (off % 4 == 0) continue;  // already tried
            NSUInteger n = MBoxPayload_ValidRunLength(blob, off);
            if (n > bestRun) { bestRun = n; bestOff = off; }
        }
    }
    if (outStartOffset) *outStartOffset = bestOff;
    if (outRunLength)   *outRunLength   = bestRun;
    return bestRun >= minRun;
}
