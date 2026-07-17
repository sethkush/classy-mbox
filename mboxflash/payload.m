#import "payload.h"

@implementation MBoxPayloadRecord
@end

NSArray<MBoxPayloadRecord *> *MBoxPayload_Parse(NSData *blob,
                                                NSUInteger startOffset,
                                                NSError **error) {
    NSMutableArray<MBoxPayloadRecord *> *out = [NSMutableArray array];
    const uint8_t *bytes = blob.bytes;
    NSUInteger len = blob.length;
    NSUInteger off = startOffset;

    while (off + 16 <= len) {
        // uint32 size, little-endian
        uint32_t size = (uint32_t)bytes[off]
                     | ((uint32_t)bytes[off+1] << 8)
                     | ((uint32_t)bytes[off+2] << 16)
                     | ((uint32_t)bytes[off+3] << 24);
        if (size == 0 || size > 4096) {
            // End of stream (or invalid) — stop here.
            break;
        }
        if (off + 16 + size > len) {
            if (error) *error = [NSError errorWithDomain:@"MBoxPayload" code:1
                userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:
                    @"record @0x%lx size 0x%x extends past blob end 0x%lx",
                    (unsigned long)off, size, (unsigned long)len]}];
            return nil;
        }
        MBoxPayloadRecord *r = [MBoxPayloadRecord new];
        r.header = [NSData dataWithBytes:bytes + off + 4 length:12];
        r.sequenceNumber = (uint32_t)bytes[off+4]
                        | ((uint32_t)bytes[off+5] << 8)
                        | ((uint32_t)bytes[off+6] << 16)
                        | ((uint32_t)bytes[off+7] << 24);
        r.data = [NSData dataWithBytes:bytes + off + 16 length:size];
        [out addObject:r];
        off += 16 + size;
    }
    return out;
}
