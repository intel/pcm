PCM can collect CLX bandwidth using the methods below.

--------------------------------------------------------------------------------
CXL.mem and CXL.cache traffic
--------------------------------------------------------------------------------

Please use pcm-memory utility for monitoring CXL.mem and CLX.cache traffic. pcm-memory will detect available CXL ports and will show traffic per CXL port and protocol (mem and cache) and per direction (read and write).

--------------------------------------------------------------------------------
CXL.io traffic
--------------------------------------------------------------------------------

pcm-iio utility should be used to monitor CXL.io traffic. pcm-iio will show traffic per CXL device and direction (inbound/outbound, read/write)


