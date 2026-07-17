// TAS1020A DFU payload — Digi's Intel-HEX-style binary record format.
// Each record: { u32 length (big-endian); u32 address (big-endian);
//                u32 type (big-endian); u8 data[length] }
// RecordType 0 = data, 1 = EOF (per Intel HEX convention).

#ifndef MBOXFLASH_PAYLOAD_H
#define MBOXFLASH_PAYLOAD_H

#import <Foundation/Foundation.h>

@interface MBoxPayloadRecord : NSObject
@property (nonatomic) NSUInteger fileOffset;
@property (nonatomic) uint32_t   length;     // = data.length
@property (nonatomic) uint32_t   address;    // target EEPROM byte offset
@property (nonatomic) uint32_t   type;       // 0 = data, 1 = EOF
@property (nonatomic, strong) NSData *data;
@end

// Parse the payload starting at exactly this offset. Returns however
// many records validate before something looks wrong (invalid len/type,
// or EOF record encountered).
NSArray<MBoxPayloadRecord *> *MBoxPayload_Parse(NSData *blob,
                                                NSUInteger startOffset);

// Autodetect where the record stream begins. Uses the signature of the
// FIRST record for the Mbox 1: len=32, addr=0, type=0, followed by the
// TAS1020A firmware header `60 12 12 34` + `0d ba` (Digi VID).
// Returns YES iff signature found.
BOOL MBoxPayload_Autodetect(NSData *blob, NSUInteger *outStartOffset);

#define MBOX_PAYLOAD_RECORD_MAX 256

#endif
