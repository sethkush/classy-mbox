# USB20CV 1.4.9.7 — the Chapter 9 test list, extracted from the tool

Source: `USB20CV_Releasex64_1_4_9_7.msi`, USB-IF, built 2013-04-19,
suite revision `$Rev: 4726 $` dated 2011-05-17. Extracted with 7z; the
suite definition is plain XML (`Chapter_9_Tests_cvtests`) and the
assertions are strings in `USBCommandVerifier.dll`.

**This replaces every previous guess about what USB20CV tests.** Before
this the repo described its Chapter 9 work as 'USB20CV coverage' while
actually implementing the spec from the text, and had no way to check the
match. Now the list is a fact.

## The tests, in execution order

`dll='USBCommandVerifier.dll'`, `products='ehci;usb20'`


### InitGroup 

| function | type | label |
|---|---|---|
| `CVCommon_InitializeTestSuite` | Action | InitializeTestSuite |
| `CVCommon_GetNumberOfConfigurations` | Action | GetNumberOfConfigurations |
| `CVCommon_GetNumberOfConfigurations` | Action | GetNumberOfConfigurations_Other |

### Group For each configuration:

| function | type | label |
|---|---|---|
| `DFW_DeviceDescriptorTest` | PassFailTest | Device Descriptor Test - Configured State |
| `DFW_DeviceDescriptorTest` | PassFailTest | Device Descriptor Test - Addressed State |
| `DFW_InterfaceAssociationDescriptorTest` | PassFailTest | Interface Association Descriptor Test |
| `DFW_BOSDescriptorTest` | PassFailTest | BOS Descriptor Test - Addressed state |
| `DFW_ConfigurationDescriptorTest` | PassFailTest | Config Descriptor Test - Configured State |
| `DFW_ConfigurationDescriptorTest` | PassFailTest | Config Descriptor Test - Addressed State |
| `DFW_InterfaceDescriptorTest` | PassFailTest | Interface Descriptor Test |
| `DFW_EndpointDescriptorTest` | PassFailTest | Endpoint Descriptor Test - Configured State |
| `DFW_EndpointDescriptorTest` | PassFailTest | Endpoint Descriptor Test - Addressed State |
| `DFW_HaltEndPointTest` | PassFailTest | Halt Endpoint Test |
| `DFW_SetConfigurationTest` | PassFailTest | Set Configuration Test |
| `DFW_SuspendResumeTest` | PassFailTest | Suspend/Resume Test |
| `DFW_RemoteWakeupTest` | PassFailTest | Remote Wakeup Test - Enabled |
| `DFW_RemoteWakeupTest` | PassFailTest | Remote Wakeup Test - Disabled |

### Group For each other speed configuration:

| function | type | label |
|---|---|---|
| `DFW_OtherSpeedConfigurationTest` | PassFailTest | Other Speed Config Descriptor Test - Addressed State |
| `DFW_InterfaceDescriptorTest` | PassFailTest | Other Speed Interface Descriptor Test - Addressed State |
| `DFW_EndpointDescriptorTest` | PassFailTest | Other Speed Endpoint Descriptor Test - Addressed State |

### Group For each configuration:

| function | type | label |
|---|---|---|
| `DFW_DeviceQualifierTest` | PassFailTest | Device Qualifier Descriptor Test - Addressed State |
| `DFW_DeviceQualifierTest` | PassFailTest | Device Qualifier Descriptor Test - Configured State |
| `DFW_OtherSpeedConfigurationTest` | PassFailTest | Other Speed Config Descriptor Test - Configured State |
| `DFW_InterfaceDescriptorTest` | PassFailTest | Other Speed Interface Descriptor Test - Configured State |
| `DFW_EndpointDescriptorTest` | PassFailTest | Other Speed Endpoint Descriptor Test - Configured State |

### Group Enumeration Group

| function | type | label |
|---|---|---|
| `DFW_EnumerateTest` | PassFailTest | Enumeration Test |
| `CVCommon_DisplayOtherTestsToRun` | Action | Display Other Tests To Run |

### ExitGroup 

| function | type | label |
|---|---|---|
| `CVCommon_CloseTestSuite` | Action | Summary |

## Assertion strings from USBCommandVerifier.dll

These are the actual failure messages, i.e. the rules the tool enforces.

```
Memory allocation failed
BOS descriptor bLength incorrect(0x%02x)
BOS descriptor type incorrect : 0x%02x
Invalid bDevCapabilityType 0x%02x
Device must support at least 2 speeds
Reserved bits of bmAttributes are non-zero : 0x%02x
Reserved bits of wSpeedsSupported are non-zero : 0x%02x
bU1DevExitLatency is invalid value that is Reserved
wU2DevExitLatency is invalid value
USB3.0 devices must be LPM capable.  LPMCapable = %d
USB 2.0 Extension Capability Descriptor bmAttributes reserved bits not 0
Reserved field bReserved is non-zero
Test Stack Initialization failed.
Invalid arguments received
Get number of configurations failed
Get configuration descriptor failed for configuration index : %x
Failed to enable remote wakeup on the device
Set configuration failed
Enable remote wakeup for the device failed
Get status for the device failed
Failed to get the device qualifier descriptor
Failed to get the device descriptor
Get configuration failed
Set configuration failed for configuration value : %x
Invalid configuration descriptor type : %x
Incorrect configuration descriptor length : %x
Invalid configuaration descriptor type : %x
Get full configuaration descriptor failed for configuration index %x
Invalid configuaration descriptor length : %x
Get OtherSpeedConfiguration descriptor failed for configuration index %x
Invalid OtherSpeedConfiguration descriptor type : %x
Incorrect OtherSpeedConfiguration descriptor length : %x
Get full OtherSpeedConfiguration descriptor failed for configuration index %x
Get BOS descriptor failed
Invalid BOS descriptor type : 0x%02x
Incorrect BOS descriptor length : 0x%02x
Get full BOS descriptor failed
Invalid BOS descriptor length : 0x%02x
Get device descriptor failed
Incorrect device descriptor length : %x
Invalid device descriptor type : %x
Re-enumeration failed
invalid string position
Read file failed
Could not assign any field, sscanf failed
Get string descriptor with LANGID : 0 failed
Invalid string descriptor length : %x
Invalid string descriptor type : %x
Get Language ID description failed %s
Get string descriptor failed
RESERVED_FOR_HID_CLASS_USE
Invalid configuration value : %x
Endpoint GetStatus request failed
Clear endpoint halt failed
Halt endpoint failed
Mismatch in number of interface descriptors. Expected : %x Found : %x
Mismatch in number of endpoint descriptors. Expected : %x Found : %x
Invalid otherspeed configuration descriptor type : %x
Failed to get configuration string for iConfiguration : %x
Bits B4..B0 must be set to 0 in the attributes field : %x
Pointer validation failed:  Exiting Test.
Invalid major version : %x
Invalid minor version : %x
Invalid device subclass. Device class is 0, subclass : %x
High speed device must have 64 bytes MaxPacketSize0 : %x
Low speed device must have 8 bytes MaxPacketSize0 : %x
Full speed device must have 8/16/32/64 bytes MaxPacketSize0 : %x
Failed to get vendor information for VendorID : %x
Failed to get manufacturer string for iManufacturer : %x
Failed to get product string for iProduct : %x
Failed to get serial number string for iSerialNumber : %x
Invalid device qualifier length : %x
Invalid device qualifier type : %x
Reserved field has to be set to zero : %x
Incorrect endpoint descriptor length : %x
Invalid endpoint descriptor type : %x
Bits 6 and 7 must be set to zero in the attributes field : %x
Bits 2 through 5 must be set to zero in the attributes field : %x
Bits 13 through 15 must be set to zero in the MaxPacketSize field : %x
Bits 11 and 12 must be set to zero in the MaxPacketSize field : %x
Invalid global device address.
Enumeration failed at iteration %d
SetInterface failed for interface number : %x alternate setting : %x
SetInterface with interface number : %x failed.
CVHub_ReEnumerateAll::Enumeration failed
Must be TS_ANY_DEVICE or TS_NO_DEVICE
Must not be TS_HOST_CONTROLLER or TS_HIGH_SPEED_HUB or TS_FULL_SPEED_HUB
User failed to populate ports correctly
Re-enumaration failed.  Trying again...
Device in port %d must be remote wakeup capable.  Please replace with such a device
Device in port %d must be%s.  Please replace with such a device
CVHub_CheckPortStatusBits::GetPortStatus failed
Get Hub descriptor failed TS_STATUS : %x
Get hub port for port number : %c failed
Get port status for port number  : %c failed TS_STATUS : %x
Range check on port number for port under test failed; must be 0>portnum<=totalnumberofports
Initialize Hub state failed TS_STATUS [0x%08x]
All bits in the PowerControlMask must be set to one
Suspend parent port failed TS_STATUS : %x
Resume parent port failed TS_STATUS : %x
Failed to Enable Remote Wake on device below port under test [%d]
Get status of non test ports failed
Compare status non test ports failed
Suspend parent port failed
  TD 9.8 : String Descriptor Test failed for iFunction : 0x%02x
Expected Interface Number : %d
Unexpected Interface Association First Interface :  expected value: %u, actual value: %u
Found %d interface%s;  Expected %d from Interface Association Descriptor
Unexpected descriptor type found.
Bandwidth check failed
Incorrect interface descriptor length : %x
Invalid interface descriptor type : %x
Interface descriptor bInterfaceClass reserved for future standardization
Interface descriptor bInterfaceClass reserved for assignment by the USB-IF : %x
Invalid interface subclass. Interface class : 0, bInterfaceSubClass : %x
Failed to get interface string for iInterface : %x
Dynamic Buffer Allocation Failed
Allocation of Endpoint failed (%s)[0x%08x]
Configuration of HS Loopback Endpoints Failed (%s)[0x%08x]
Isochronous Write (Bulk) Failed (%s)[0x%08x]
Get device qualifier descriptor failed. Device may not support it
Request to suspend the parent port failed
Request to resume the parent port failed
A failure to generate Remote Wake may be waived if the Device Under Test meets these requirements:
Selective enumeration failed
SetConfiguration with configuration value : %x failed
This is a compound device.  We shall also include summaries of the embedded devices
Failed to get vendor information for Vendor ID : %x
          Interface descriptor bInterfaceClass reserved for future standardization
          Interface descriptor bInterfaceClass reserved for assignment by the USB-IF : %x
ONE OR MORE OF THE ATTACHED DEVICES FAILED TO RETURN THE CORRECT POWER.  THE FOLLOWING TOTALS MAY BE IN ERROR.
Get Device Descriptor failed after resuming device.
```
