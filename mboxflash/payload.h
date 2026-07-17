// Payload record parser. The stock Digi flasher embeds firmware as a
// stream of records: [uint32_t size][12 bytes header][size bytes data].
// Each record becomes one DFU_DNLOAD control transfer.

#ifndef MBOXFLASH_PAYLOAD_H
#define MBOXFLASH_PAYLOAD_H

#import <Foundation/Foundation.h>

@interface MBoxPayloadRecord : NSObject
@property (nonatomic) uint32_t sequenceNumber;   // header bytes 0..3 (LE)
@property (nonatomic, strong) NSData *header;    // 12 header bytes
@property (nonatomic, strong) NSData *data;      // `size` bytes of firmware
@end

// Parse a raw payload blob into a list of records. Returns nil on parse
// failure (invalid size, truncated). `startOffset` is where the record
// stream begins inside `blob` (blob may include cruft before the stream).
NSArray<MBoxPayloadRecord *> *MBoxPayload_Parse(NSData *blob,
                                                NSUInteger startOffset,
                                                NSError **error);

#endif
