# ChangeLog

## v0.4.2 - 2023-01-30

### Enhancements:

* Camera:
  * Disable BULK_BYTES_PER_URB_SAME_AS_FRAME by default, support reassemble bulk package segment (will save one image frame size RAM)
  * Add config UVC_DROP_NO_EOF_FRAME, if disable(default) the frame with no EOF will be reserved (MJPEG may broken)
  * Add config UVC_DROP_OVERFLOW_FRAME, if enable(default) the overflow frame will be dropped

### Bug Fixes:

* Camera
  * fix transfer size in GET_CUR Probe stage


## v0.4.1 - 2023-01-05

### Enhancements:

* Add IRAM_ATTR to packet parsing functions
* Speaker:
  * padding zero packet if speaker buffer empty


## v0.4.0 - 2022-12-22

### Enhancements:

* Support parsing descriptors to check the device functions and capabilities
* Self-adaption appropriate interfaces based on descriptors and user's configurations
* Camera:
  * Support UVC config to any resolution, the first frame index will be used

### API Changes:

* add `optional` label to some members in `uvc_config_t` and `uac_config_t`, users can optionally set a value. The self-adaption process will first use actual configs from descriptors, if no appropriate interface found, will use the params from user's configuration

### Bug Fixes:

* USB:
  * Fix STALL (appear on some devices) during get short configuration descriptor
  * Resize endpoint 0 MPS based on device descriptor

## v0.3.3 - 2022-12-22

### Enhancements:

* print version message

### Bug Fixes:

* Camera:
  * Fix not parse header only frames

## v0.3.2 - 2022-12-16

### Enhancements:

* Mic:
  * Padding last frame data when receive zero length packet

## v0.3.1 - 2022-12-8

### Enhancements:

* add development documentation https://docs.espressif.com/projects/espressif-esp-iot-solution/zh_CN/latest/usb/usb_stream.html

## v0.3.0 - 2022-12-05

### Bug Fixes:

* USB:
  * Fix memory leak during hot-plug
