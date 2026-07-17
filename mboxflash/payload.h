// Payload record parser. The stock Digi flasher embeds firmware as a
// stream of records: [uint32_t size][12 bytes header][size bytes data].
// Each record becomes one DFU_DNLOAD control transfer.

#ifndef MBOXFLASH_PAYLOAD_H
#define MBOXFLASH_PAYLOAD_H

#import <Foundation/Foundation.h>

@interface MBoxPayloadRecord : NSObject
@property (nonatomic) NSUInteger fileOffset;     // where this record started in the blob
@property (nonatomic) uint32_t   sequenceNumber; // header bytes 0..3 (LE)
@property (nonatomic, strong) NSData *header;    // 12 header bytes
@property (nonatomic, strong) NSData *data;      // `size` bytes of firmware
@end

// Parse from a specific offset. Stops at first invalid record; returns
// however many valid records it read (possibly zero). Never returns nil.
NSArray<MBoxPayloadRecord *> *MBoxPayload_Parse(NSData *blob,
                                                NSUInteger startOffset);

// Scan the blob for the offset that produces the longest run of valid
// records. Returns the auto-detected startOffset via out param.
// Returns YES iff at least `minRun` consecutive records validated
// somewhere in the blob.
BOOL MBoxPayload_Autodetect(NSData *blob, NSUInteger minRun,
                            NSUInteger *outStartOffset,
                            NSUInteger *outRunLength);

// Validate that a candidate offset looks plausible: reads N records
// forward, returns count. Zero means "not a valid start."
NSUInteger MBoxPayload_ValidRunLength(NSData *blob, NSUInteger startOffset);

// Max data-byte length we'll accept in a single record. Anything above
// this is treated as invalid (garbage / not-a-record). USB DFU 1.0 caps
// wLength at ~2 KB in practice; we allow 4 KB slack.
#define MBOX_PAYLOAD_MAX_RECORD_DATA 4096

#endif
