#include "windows.h"
#include "stdio.h"
#include "tchar.h"

// Executable version.
static TCHAR version[] = TEXT("1.3. Built ") TEXT(__DATE__);
#define PMEM_DEVICE_NAME "pmem"
#define PMEM_SERVICE_NAME TEXT("winpmem")

#define PAGE_SIZE 0x1000

class WinPmem {
 public:
  WinPmem();
  virtual ~WinPmem();

  virtual int install_driver(bool delete_driver = true);
  virtual int uninstall_driver();
  virtual int set_acquisition_mode(__int32 mode);

  unsigned int read32(__int64 start);

  // This is set if output should be suppressed (e.g. if we pipe the
  // image to the STDOUT).
  int suppress_output;
  TCHAR last_error[1024];

 protected:
  virtual int load_driver_() = 0;

  virtual void LogError(TCHAR *message);
  virtual void Log(const TCHAR *message, ...);

  // The file handle to the pmem device.
  HANDLE fd_;

  // The file handle to the image file.
  HANDLE out_fd_;
  TCHAR *service_name;
  TCHAR driver_filename[MAX_PATH];

  // This is the maximum size of memory calculated.
  __int64 max_physical_memory_;
};

// This is the filename of the driver we drop.
static TCHAR driver_filename[MAX_PATH];

// ioctl to get memory ranges from our driver.
#define PMEM_CTRL_IOCTRL CTL_CODE(0x22, 0x101, 0, 3)
#define PMEM_WRITE_ENABLE CTL_CODE(0x22, 0x102, 0, 3)
#define PMEM_INFO_IOCTRL CTL_CODE(0x22, 0x103, 0, 3)

// Available modes
#define PMEM_MODE_IOSPACE 0
#define PMEM_MODE_PHYSICAL 1
