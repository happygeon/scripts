// https://syzkaller.appspot.com/bug?id=40a8650766c76a78b5a0f8b4dd4278a190dcbf3e
// autogenerated by syzkaller (https://github.com/google/syzkaller)

#define _GNU_SOURCE

#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <linux/usb/ch9.h>

static unsigned long long procid;

static void sleep_ms(uint64_t ms)
{
  usleep(ms * 1000);
}

static uint64_t current_time_ms(void)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts))
    exit(1);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static bool write_file(const char* file, const char* what, ...)
{
  char buf[1024];
  va_list args;
  va_start(args, what);
  vsnprintf(buf, sizeof(buf), what, args);
  va_end(args);
  buf[sizeof(buf) - 1] = 0;
  int len = strlen(buf);
  int fd = open(file, O_WRONLY | O_CLOEXEC);
  if (fd == -1)
    return false;
  if (write(fd, buf, len) != len) {
    int err = errno;
    close(fd);
    errno = err;
    return false;
  }
  close(fd);
  return true;
}

#define MAX_FDS 30

#define USB_MAX_IFACE_NUM 4
#define USB_MAX_EP_NUM 32
#define USB_MAX_FDS 6

struct usb_endpoint_index {
  struct usb_endpoint_descriptor desc;
  int handle;
};

struct usb_iface_index {
  struct usb_interface_descriptor* iface;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bInterfaceClass;
  struct usb_endpoint_index eps[USB_MAX_EP_NUM];
  int eps_num;
};

struct usb_device_index {
  struct usb_device_descriptor* dev;
  struct usb_config_descriptor* config;
  uint8_t bDeviceClass;
  uint8_t bMaxPower;
  int config_length;
  struct usb_iface_index ifaces[USB_MAX_IFACE_NUM];
  int ifaces_num;
  int iface_cur;
};

struct usb_info {
  int fd;
  struct usb_device_index index;
};

static struct usb_info usb_devices[USB_MAX_FDS];
static int usb_devices_num;

static bool parse_usb_descriptor(const char* buffer, size_t length,
                                 struct usb_device_index* index)
{
  if (length < sizeof(*index->dev) + sizeof(*index->config))
    return false;
  memset(index, 0, sizeof(*index));
  index->dev = (struct usb_device_descriptor*)buffer;
  index->config = (struct usb_config_descriptor*)(buffer + sizeof(*index->dev));
  index->bDeviceClass = index->dev->bDeviceClass;
  index->bMaxPower = index->config->bMaxPower;
  index->config_length = length - sizeof(*index->dev);
  index->iface_cur = -1;
  size_t offset = 0;
  while (true) {
    if (offset + 1 >= length)
      break;
    uint8_t desc_length = buffer[offset];
    uint8_t desc_type = buffer[offset + 1];
    if (desc_length <= 2)
      break;
    if (offset + desc_length > length)
      break;
    if (desc_type == USB_DT_INTERFACE &&
        index->ifaces_num < USB_MAX_IFACE_NUM) {
      struct usb_interface_descriptor* iface =
          (struct usb_interface_descriptor*)(buffer + offset);
      index->ifaces[index->ifaces_num].iface = iface;
      index->ifaces[index->ifaces_num].bInterfaceNumber =
          iface->bInterfaceNumber;
      index->ifaces[index->ifaces_num].bAlternateSetting =
          iface->bAlternateSetting;
      index->ifaces[index->ifaces_num].bInterfaceClass = iface->bInterfaceClass;
      index->ifaces_num++;
    }
    if (desc_type == USB_DT_ENDPOINT && index->ifaces_num > 0) {
      struct usb_iface_index* iface = &index->ifaces[index->ifaces_num - 1];
      if (iface->eps_num < USB_MAX_EP_NUM) {
        memcpy(&iface->eps[iface->eps_num].desc, buffer + offset,
               sizeof(iface->eps[iface->eps_num].desc));
        iface->eps_num++;
      }
    }
    offset += desc_length;
  }
  return true;
}

static struct usb_device_index* add_usb_index(int fd, const char* dev,
                                              size_t dev_len)
{
  int i = __atomic_fetch_add(&usb_devices_num, 1, __ATOMIC_RELAXED);
  if (i >= USB_MAX_FDS)
    return NULL;
  if (!parse_usb_descriptor(dev, dev_len, &usb_devices[i].index))
    return NULL;
  __atomic_store_n(&usb_devices[i].fd, fd, __ATOMIC_RELEASE);
  return &usb_devices[i].index;
}

static struct usb_device_index* lookup_usb_index(int fd)
{
  for (int i = 0; i < USB_MAX_FDS; i++) {
    if (__atomic_load_n(&usb_devices[i].fd, __ATOMIC_ACQUIRE) == fd)
      return &usb_devices[i].index;
  }
  return NULL;
}

struct vusb_connect_string_descriptor {
  uint32_t len;
  char* str;
} __attribute__((packed));

struct vusb_connect_descriptors {
  uint32_t qual_len;
  char* qual;
  uint32_t bos_len;
  char* bos;
  uint32_t strs_len;
  struct vusb_connect_string_descriptor strs[0];
} __attribute__((packed));

static const char default_string[] = {8, USB_DT_STRING, 's', 0, 'y', 0, 'z', 0};

static const char default_lang_id[] = {4, USB_DT_STRING, 0x09, 0x04};

static bool
lookup_connect_response_in(int fd, const struct vusb_connect_descriptors* descs,
                           const struct usb_ctrlrequest* ctrl,
                           char** response_data, uint32_t* response_length)
{
  struct usb_device_index* index = lookup_usb_index(fd);
  uint8_t str_idx;
  if (!index)
    return false;
  switch (ctrl->bRequestType & USB_TYPE_MASK) {
  case USB_TYPE_STANDARD:
    switch (ctrl->bRequest) {
    case USB_REQ_GET_DESCRIPTOR:
      switch (ctrl->wValue >> 8) {
      case USB_DT_DEVICE:
        *response_data = (char*)index->dev;
        *response_length = sizeof(*index->dev);
        return true;
      case USB_DT_CONFIG:
        *response_data = (char*)index->config;
        *response_length = index->config_length;
        return true;
      case USB_DT_STRING:
        str_idx = (uint8_t)ctrl->wValue;
        if (descs && str_idx < descs->strs_len) {
          *response_data = descs->strs[str_idx].str;
          *response_length = descs->strs[str_idx].len;
          return true;
        }
        if (str_idx == 0) {
          *response_data = (char*)&default_lang_id[0];
          *response_length = default_lang_id[0];
          return true;
        }
        *response_data = (char*)&default_string[0];
        *response_length = default_string[0];
        return true;
      case USB_DT_BOS:
        *response_data = descs->bos;
        *response_length = descs->bos_len;
        return true;
      case USB_DT_DEVICE_QUALIFIER:
        if (!descs->qual) {
          struct usb_qualifier_descriptor* qual =
              (struct usb_qualifier_descriptor*)response_data;
          qual->bLength = sizeof(*qual);
          qual->bDescriptorType = USB_DT_DEVICE_QUALIFIER;
          qual->bcdUSB = index->dev->bcdUSB;
          qual->bDeviceClass = index->dev->bDeviceClass;
          qual->bDeviceSubClass = index->dev->bDeviceSubClass;
          qual->bDeviceProtocol = index->dev->bDeviceProtocol;
          qual->bMaxPacketSize0 = index->dev->bMaxPacketSize0;
          qual->bNumConfigurations = index->dev->bNumConfigurations;
          qual->bRESERVED = 0;
          *response_length = sizeof(*qual);
          return true;
        }
        *response_data = descs->qual;
        *response_length = descs->qual_len;
        return true;
      default:
        break;
      }
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
  return false;
}

typedef bool (*lookup_connect_out_response_t)(
    int fd, const struct vusb_connect_descriptors* descs,
    const struct usb_ctrlrequest* ctrl, bool* done);

static bool lookup_connect_response_out_generic(
    int fd, const struct vusb_connect_descriptors* descs,
    const struct usb_ctrlrequest* ctrl, bool* done)
{
  switch (ctrl->bRequestType & USB_TYPE_MASK) {
  case USB_TYPE_STANDARD:
    switch (ctrl->bRequest) {
    case USB_REQ_SET_CONFIGURATION:
      *done = true;
      return true;
    default:
      break;
    }
    break;
  }
  return false;
}

#define UDC_NAME_LENGTH_MAX 128

struct usb_raw_init {
  __u8 driver_name[UDC_NAME_LENGTH_MAX];
  __u8 device_name[UDC_NAME_LENGTH_MAX];
  __u8 speed;
};

enum usb_raw_event_type {
  USB_RAW_EVENT_INVALID = 0,
  USB_RAW_EVENT_CONNECT = 1,
  USB_RAW_EVENT_CONTROL = 2,
};

struct usb_raw_event {
  __u32 type;
  __u32 length;
  __u8 data[0];
};

struct usb_raw_ep_io {
  __u16 ep;
  __u16 flags;
  __u32 length;
  __u8 data[0];
};

#define USB_RAW_EPS_NUM_MAX 30
#define USB_RAW_EP_NAME_MAX 16
#define USB_RAW_EP_ADDR_ANY 0xff

struct usb_raw_ep_caps {
  __u32 type_control : 1;
  __u32 type_iso : 1;
  __u32 type_bulk : 1;
  __u32 type_int : 1;
  __u32 dir_in : 1;
  __u32 dir_out : 1;
};

struct usb_raw_ep_limits {
  __u16 maxpacket_limit;
  __u16 max_streams;
  __u32 reserved;
};

struct usb_raw_ep_info {
  __u8 name[USB_RAW_EP_NAME_MAX];
  __u32 addr;
  struct usb_raw_ep_caps caps;
  struct usb_raw_ep_limits limits;
};

struct usb_raw_eps_info {
  struct usb_raw_ep_info eps[USB_RAW_EPS_NUM_MAX];
};

#define USB_RAW_IOCTL_INIT _IOW('U', 0, struct usb_raw_init)
#define USB_RAW_IOCTL_RUN _IO('U', 1)
#define USB_RAW_IOCTL_EVENT_FETCH _IOR('U', 2, struct usb_raw_event)
#define USB_RAW_IOCTL_EP0_WRITE _IOW('U', 3, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP0_READ _IOWR('U', 4, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP_ENABLE _IOW('U', 5, struct usb_endpoint_descriptor)
#define USB_RAW_IOCTL_EP_DISABLE _IOW('U', 6, __u32)
#define USB_RAW_IOCTL_EP_WRITE _IOW('U', 7, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP_READ _IOWR('U', 8, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_CONFIGURE _IO('U', 9)
#define USB_RAW_IOCTL_VBUS_DRAW _IOW('U', 10, __u32)
#define USB_RAW_IOCTL_EPS_INFO _IOR('U', 11, struct usb_raw_eps_info)
#define USB_RAW_IOCTL_EP0_STALL _IO('U', 12)
#define USB_RAW_IOCTL_EP_SET_HALT _IOW('U', 13, __u32)
#define USB_RAW_IOCTL_EP_CLEAR_HALT _IOW('U', 14, __u32)
#define USB_RAW_IOCTL_EP_SET_WEDGE _IOW('U', 15, __u32)

static int usb_raw_open()
{
  return open("/dev/raw-gadget", O_RDWR);
}

static int usb_raw_init(int fd, uint32_t speed, const char* driver,
                        const char* device)
{
  struct usb_raw_init arg;
  strncpy((char*)&arg.driver_name[0], driver, sizeof(arg.driver_name));
  strncpy((char*)&arg.device_name[0], device, sizeof(arg.device_name));
  arg.speed = speed;
  return ioctl(fd, USB_RAW_IOCTL_INIT, &arg);
}

static int usb_raw_run(int fd)
{
  return ioctl(fd, USB_RAW_IOCTL_RUN, 0);
}

static int usb_raw_event_fetch(int fd, struct usb_raw_event* event)
{
  return ioctl(fd, USB_RAW_IOCTL_EVENT_FETCH, event);
}

static int usb_raw_ep0_write(int fd, struct usb_raw_ep_io* io)
{
  return ioctl(fd, USB_RAW_IOCTL_EP0_WRITE, io);
}

static int usb_raw_ep0_read(int fd, struct usb_raw_ep_io* io)
{
  return ioctl(fd, USB_RAW_IOCTL_EP0_READ, io);
}

static int usb_raw_ep_enable(int fd, struct usb_endpoint_descriptor* desc)
{
  return ioctl(fd, USB_RAW_IOCTL_EP_ENABLE, desc);
}

static int usb_raw_ep_disable(int fd, int ep)
{
  return ioctl(fd, USB_RAW_IOCTL_EP_DISABLE, ep);
}

static int usb_raw_configure(int fd)
{
  return ioctl(fd, USB_RAW_IOCTL_CONFIGURE, 0);
}

static int usb_raw_vbus_draw(int fd, uint32_t power)
{
  return ioctl(fd, USB_RAW_IOCTL_VBUS_DRAW, power);
}

static int usb_raw_ep0_stall(int fd)
{
  return ioctl(fd, USB_RAW_IOCTL_EP0_STALL, 0);
}

static void set_interface(int fd, int n)
{
  struct usb_device_index* index = lookup_usb_index(fd);
  if (!index)
    return;
  if (index->iface_cur >= 0 && index->iface_cur < index->ifaces_num) {
    for (int ep = 0; ep < index->ifaces[index->iface_cur].eps_num; ep++) {
      int rv = usb_raw_ep_disable(
          fd, index->ifaces[index->iface_cur].eps[ep].handle);
      if (rv < 0) {
      } else {
      }
    }
  }
  if (n >= 0 && n < index->ifaces_num) {
    for (int ep = 0; ep < index->ifaces[n].eps_num; ep++) {
      int rv = usb_raw_ep_enable(fd, &index->ifaces[n].eps[ep].desc);
      if (rv < 0) {
      } else {
        index->ifaces[n].eps[ep].handle = rv;
      }
    }
    index->iface_cur = n;
  }
}

static int configure_device(int fd)
{
  struct usb_device_index* index = lookup_usb_index(fd);
  if (!index)
    return -1;
  int rv = usb_raw_vbus_draw(fd, index->bMaxPower);
  if (rv < 0) {
    return rv;
  }
  rv = usb_raw_configure(fd);
  if (rv < 0) {
    return rv;
  }
  set_interface(fd, 0);
  return 0;
}

#define USB_MAX_PACKET_SIZE 4096

struct usb_raw_control_event {
  struct usb_raw_event inner;
  struct usb_ctrlrequest ctrl;
  char data[USB_MAX_PACKET_SIZE];
};

struct usb_raw_ep_io_data {
  struct usb_raw_ep_io inner;
  char data[USB_MAX_PACKET_SIZE];
};

static volatile long
syz_usb_connect_impl(uint64_t speed, uint64_t dev_len, const char* dev,
                     const struct vusb_connect_descriptors* descs,
                     lookup_connect_out_response_t lookup_connect_response_out)
{
  if (!dev) {
    return -1;
  }
  int fd = usb_raw_open();
  if (fd < 0) {
    return fd;
  }
  if (fd >= MAX_FDS) {
    close(fd);
    return -1;
  }
  struct usb_device_index* index = add_usb_index(fd, dev, dev_len);
  if (!index) {
    return -1;
  }
  char device[32];
  sprintf(&device[0], "dummy_udc.%llu", procid);
  int rv = usb_raw_init(fd, speed, "dummy_udc", &device[0]);
  if (rv < 0) {
    return rv;
  }
  rv = usb_raw_run(fd);
  if (rv < 0) {
    return rv;
  }
  bool done = false;
  while (!done) {
    struct usb_raw_control_event event;
    event.inner.type = 0;
    event.inner.length = sizeof(event.ctrl);
    rv = usb_raw_event_fetch(fd, (struct usb_raw_event*)&event);
    if (rv < 0) {
      return rv;
    }
    if (event.inner.type != USB_RAW_EVENT_CONTROL)
      continue;
    char* response_data = NULL;
    uint32_t response_length = 0;
    if (event.ctrl.bRequestType & USB_DIR_IN) {
      if (!lookup_connect_response_in(fd, descs, &event.ctrl, &response_data,
                                      &response_length)) {
        usb_raw_ep0_stall(fd);
        continue;
      }
    } else {
      if (!lookup_connect_response_out(fd, descs, &event.ctrl, &done)) {
        usb_raw_ep0_stall(fd);
        continue;
      }
      response_data = NULL;
      response_length = event.ctrl.wLength;
    }
    if ((event.ctrl.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD &&
        event.ctrl.bRequest == USB_REQ_SET_CONFIGURATION) {
      rv = configure_device(fd);
      if (rv < 0) {
        return rv;
      }
    }
    struct usb_raw_ep_io_data response;
    response.inner.ep = 0;
    response.inner.flags = 0;
    if (response_length > sizeof(response.data))
      response_length = 0;
    if (event.ctrl.wLength < response_length)
      response_length = event.ctrl.wLength;
    response.inner.length = response_length;
    if (response_data)
      memcpy(&response.data[0], response_data, response_length);
    else
      memset(&response.data[0], 0, response_length);
    if (event.ctrl.bRequestType & USB_DIR_IN) {
      rv = usb_raw_ep0_write(fd, (struct usb_raw_ep_io*)&response);
    } else {
      rv = usb_raw_ep0_read(fd, (struct usb_raw_ep_io*)&response);
    }
    if (rv < 0) {
      return rv;
    }
  }
  sleep_ms(200);
  return fd;
}

static volatile long syz_usb_connect(volatile long a0, volatile long a1,
                                     volatile long a2, volatile long a3)
{
  uint64_t speed = a0;
  uint64_t dev_len = a1;
  const char* dev = (const char*)a2;
  const struct vusb_connect_descriptors* descs =
      (const struct vusb_connect_descriptors*)a3;
  return syz_usb_connect_impl(speed, dev_len, dev, descs,
                              &lookup_connect_response_out_generic);
}

static long syz_open_dev(volatile long a0, volatile long a1, volatile long a2)
{
  if (a0 == 0xc || a0 == 0xb) {
    char buf[128];
    sprintf(buf, "/dev/%s/%d:%d", a0 == 0xc ? "char" : "block", (uint8_t)a1,
            (uint8_t)a2);
    return open(buf, O_RDWR, 0);
  } else {
    char buf[1024];
    char* hash;
    strncpy(buf, (char*)a0, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    while ((hash = strchr(buf, '#'))) {
      *hash = '0' + (char)(a1 % 10);
      a1 /= 10;
    }
    return open(buf, a2, 0);
  }
}

static void kill_and_wait(int pid, int* status)
{
  kill(-pid, SIGKILL);
  kill(pid, SIGKILL);
  for (int i = 0; i < 100; i++) {
    if (waitpid(-1, status, WNOHANG | __WALL) == pid)
      return;
    usleep(1000);
  }
  DIR* dir = opendir("/sys/fs/fuse/connections");
  if (dir) {
    for (;;) {
      struct dirent* ent = readdir(dir);
      if (!ent)
        break;
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
        continue;
      char abort[300];
      snprintf(abort, sizeof(abort), "/sys/fs/fuse/connections/%s/abort",
               ent->d_name);
      int fd = open(abort, O_WRONLY);
      if (fd == -1) {
        continue;
      }
      if (write(fd, abort, 1) < 0) {
      }
      close(fd);
    }
    closedir(dir);
  } else {
  }
  while (waitpid(-1, status, __WALL) != pid) {
  }
}

static void setup_test()
{
  prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
  setpgrp();
  write_file("/proc/self/oom_score_adj", "1000");
}

static void setup_sysctl()
{
  char mypid[32];
  snprintf(mypid, sizeof(mypid), "%d", getpid());
  struct {
    const char* name;
    const char* data;
  } files[] = {
      {"/sys/kernel/debug/x86/nmi_longest_ns", "10000000000"},
      {"/proc/sys/kernel/hung_task_check_interval_secs", "20"},
      {"/proc/sys/net/core/bpf_jit_kallsyms", "1"},
      {"/proc/sys/net/core/bpf_jit_harden", "0"},
      {"/proc/sys/kernel/kptr_restrict", "0"},
      {"/proc/sys/kernel/softlockup_all_cpu_backtrace", "1"},
      {"/proc/sys/fs/mount-max", "100"},
      {"/proc/sys/vm/oom_dump_tasks", "0"},
      {"/proc/sys/debug/exception-trace", "0"},
      {"/proc/sys/kernel/printk", "7 4 1 3"},
      {"/proc/sys/net/ipv4/ping_group_range", "0 65535"},
      {"/proc/sys/kernel/keys/gc_delay", "1"},
      {"/proc/sys/vm/oom_kill_allocating_task", "1"},
      {"/proc/sys/kernel/ctrl-alt-del", "0"},
      {"/proc/sys/kernel/cad_pid", mypid},
  };
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    if (!write_file(files[i].name, files[i].data))
      printf("write to %s failed: %s\n", files[i].name, strerror(errno));
  }
}

static void execute_one(void);

#define WAIT_FLAGS __WALL

static void loop(void)
{
  int iter = 0;
  for (;; iter++) {
    int pid = fork();
    if (pid < 0)
      exit(1);
    if (pid == 0) {
      setup_test();
      execute_one();
      exit(0);
    }
    int status = 0;
    uint64_t start = current_time_ms();
    for (;;) {
      if (waitpid(-1, &status, WNOHANG | WAIT_FLAGS) == pid)
        break;
      sleep_ms(1);
      if (current_time_ms() - start < 5000)
        continue;
      kill_and_wait(pid, &status);
      break;
    }
  }
}

void execute_one(void)
{
  syz_open_dev(0xc, 0xb4, 0);
  *(uint8_t*)0x20000000 = 0x12;
  *(uint8_t*)0x20000001 = 1;
  *(uint16_t*)0x20000002 = 0x110;
  *(uint8_t*)0x20000004 = 0xf7;
  *(uint8_t*)0x20000005 = 0xad;
  *(uint8_t*)0x20000006 = 0x68;
  *(uint8_t*)0x20000007 = 8;
  *(uint16_t*)0x20000008 = 0x711;
  *(uint16_t*)0x2000000a = 0x903;
  *(uint16_t*)0x2000000c = 0x661d;
  *(uint8_t*)0x2000000e = 1;
  *(uint8_t*)0x2000000f = 2;
  *(uint8_t*)0x20000010 = 3;
  *(uint8_t*)0x20000011 = 1;
  *(uint8_t*)0x20000012 = 9;
  *(uint8_t*)0x20000013 = 2;
  *(uint16_t*)0x20000014 = 0x10e;
  *(uint8_t*)0x20000016 = 3;
  *(uint8_t*)0x20000017 = 0x1d;
  *(uint8_t*)0x20000018 = 5;
  *(uint8_t*)0x20000019 = 0x20;
  *(uint8_t*)0x2000001a = 0;
  *(uint8_t*)0x2000001b = 9;
  *(uint8_t*)0x2000001c = 4;
  *(uint8_t*)0x2000001d = 0xe5;
  *(uint8_t*)0x2000001e = 1;
  *(uint8_t*)0x2000001f = 0;
  *(uint8_t*)0x20000020 = 0x6c;
  *(uint8_t*)0x20000021 = 0xea;
  *(uint8_t*)0x20000022 = 0x6c;
  *(uint8_t*)0x20000023 = 0;
  *(uint8_t*)0x20000024 = 2;
  *(uint8_t*)0x20000025 = 0x24;
  *(uint8_t*)0x20000026 = 5;
  *(uint8_t*)0x20000027 = 0x24;
  *(uint8_t*)0x20000028 = 6;
  *(uint8_t*)0x20000029 = 0;
  *(uint8_t*)0x2000002a = 1;
  *(uint8_t*)0x2000002b = 5;
  *(uint8_t*)0x2000002c = 0x24;
  *(uint8_t*)0x2000002d = 0;
  *(uint16_t*)0x2000002e = 9;
  *(uint8_t*)0x20000030 = 0xd;
  *(uint8_t*)0x20000031 = 0x24;
  *(uint8_t*)0x20000032 = 0xf;
  *(uint8_t*)0x20000033 = 1;
  *(uint32_t*)0x20000034 = 0xfff;
  *(uint16_t*)0x20000038 = 5;
  *(uint16_t*)0x2000003a = 0xff;
  *(uint8_t*)0x2000003c = 7;
  *(uint8_t*)0x2000003d = 6;
  *(uint8_t*)0x2000003e = 0x24;
  *(uint8_t*)0x2000003f = 0x1a;
  *(uint16_t*)0x20000040 = 6;
  *(uint8_t*)0x20000042 = 0x20;
  *(uint8_t*)0x20000043 = 4;
  *(uint8_t*)0x20000044 = 0x24;
  *(uint8_t*)0x20000045 = 0x13;
  *(uint8_t*)0x20000046 = 0x1f;
  *(uint8_t*)0x20000047 = 5;
  *(uint8_t*)0x20000048 = 0x24;
  *(uint8_t*)0x20000049 = 0x15;
  *(uint16_t*)0x2000004a = -1;
  *(uint8_t*)0x2000004c = 0xe;
  *(uint8_t*)0x2000004d = 0x24;
  *(uint8_t*)0x2000004e = 7;
  *(uint8_t*)0x2000004f = 8;
  *(uint16_t*)0x20000050 = 7;
  *(uint16_t*)0x20000052 = 0x8241;
  *(uint16_t*)0x20000054 = 0x1ffe;
  *(uint16_t*)0x20000056 = 0x7b2f;
  *(uint16_t*)0x20000058 = 4;
  *(uint8_t*)0x2000005a = 0x15;
  *(uint8_t*)0x2000005b = 0x24;
  *(uint8_t*)0x2000005c = 0x12;
  *(uint16_t*)0x2000005d = 0x20;
  *(uint64_t*)0x2000005f = 0x14f5e048ba817a3;
  *(uint64_t*)0x20000067 = 0x2a397ecbffc007a6;
  *(uint8_t*)0x2000006f = 7;
  *(uint8_t*)0x20000070 = 0x24;
  *(uint8_t*)0x20000071 = 0x14;
  *(uint16_t*)0x20000072 = 3;
  *(uint16_t*)0x20000074 = 5;
  *(uint8_t*)0x20000076 = 8;
  *(uint8_t*)0x20000077 = 0x24;
  *(uint8_t*)0x20000078 = 0x1c;
  *(uint16_t*)0x20000079 = 0x7f;
  *(uint8_t*)0x2000007b = 4;
  *(uint16_t*)0x2000007c = 5;
  *(uint8_t*)0x2000007e = 9;
  *(uint8_t*)0x2000007f = 4;
  *(uint8_t*)0x20000080 = 0xd4;
  *(uint8_t*)0x20000081 = 3;
  *(uint8_t*)0x20000082 = 8;
  *(uint8_t*)0x20000083 = -1;
  *(uint8_t*)0x20000084 = 2;
  *(uint8_t*)0x20000085 = 0x17;
  *(uint8_t*)0x20000086 = 0x80;
  *(uint8_t*)0x20000087 = 9;
  *(uint8_t*)0x20000088 = 5;
  *(uint8_t*)0x20000089 = 0xc;
  *(uint8_t*)0x2000008a = 0x10;
  *(uint16_t*)0x2000008b = 0x10;
  *(uint8_t*)0x2000008d = 0x1f;
  *(uint8_t*)0x2000008e = 3;
  *(uint8_t*)0x2000008f = 0x88;
  *(uint8_t*)0x20000090 = 7;
  *(uint8_t*)0x20000091 = 0x25;
  *(uint8_t*)0x20000092 = 1;
  *(uint8_t*)0x20000093 = 1;
  *(uint8_t*)0x20000094 = 0x40;
  *(uint16_t*)0x20000095 = 0xe510;
  *(uint8_t*)0x20000097 = 9;
  *(uint8_t*)0x20000098 = 5;
  *(uint8_t*)0x20000099 = 0xd;
  *(uint8_t*)0x2000009a = 4;
  *(uint16_t*)0x2000009b = 8;
  *(uint8_t*)0x2000009d = 0xe1;
  *(uint8_t*)0x2000009e = 1;
  *(uint8_t*)0x2000009f = 0x20;
  *(uint8_t*)0x200000a0 = 2;
  *(uint8_t*)0x200000a1 = 0x22;
  *(uint8_t*)0x200000a2 = 9;
  *(uint8_t*)0x200000a3 = 5;
  *(uint8_t*)0x200000a4 = 5;
  *(uint8_t*)0x200000a5 = 2;
  *(uint16_t*)0x200000a6 = 0x400;
  *(uint8_t*)0x200000a8 = 6;
  *(uint8_t*)0x200000a9 = 0xbf;
  *(uint8_t*)0x200000aa = 0xb1;
  *(uint8_t*)0x200000ab = 2;
  *(uint8_t*)0x200000ac = 0x22;
  *(uint8_t*)0x200000ad = 2;
  *(uint8_t*)0x200000ae = 2;
  *(uint8_t*)0x200000af = 9;
  *(uint8_t*)0x200000b0 = 5;
  *(uint8_t*)0x200000b1 = 0xf;
  *(uint8_t*)0x200000b2 = 0x10;
  *(uint16_t*)0x200000b3 = 0x3ff;
  *(uint8_t*)0x200000b5 = 0xd7;
  *(uint8_t*)0x200000b6 = 2;
  *(uint8_t*)0x200000b7 = 4;
  *(uint8_t*)0x200000b8 = 7;
  *(uint8_t*)0x200000b9 = 0x25;
  *(uint8_t*)0x200000ba = 1;
  *(uint8_t*)0x200000bb = 0;
  *(uint8_t*)0x200000bc = 8;
  *(uint16_t*)0x200000bd = 8;
  *(uint8_t*)0x200000bf = 9;
  *(uint8_t*)0x200000c0 = 5;
  *(uint8_t*)0x200000c1 = 6;
  *(uint8_t*)0x200000c2 = 1;
  *(uint16_t*)0x200000c3 = 0x3ff;
  *(uint8_t*)0x200000c5 = 5;
  *(uint8_t*)0x200000c6 = 0x25;
  *(uint8_t*)0x200000c7 = -1;
  *(uint8_t*)0x200000c8 = 7;
  *(uint8_t*)0x200000c9 = 0x25;
  *(uint8_t*)0x200000ca = 1;
  *(uint8_t*)0x200000cb = 2;
  *(uint8_t*)0x200000cc = 0x18;
  *(uint16_t*)0x200000cd = 0x91;
  *(uint8_t*)0x200000cf = 7;
  *(uint8_t*)0x200000d0 = 0x25;
  *(uint8_t*)0x200000d1 = 1;
  *(uint8_t*)0x200000d2 = 1;
  *(uint8_t*)0x200000d3 = 5;
  *(uint16_t*)0x200000d4 = 0x81;
  *(uint8_t*)0x200000d6 = 9;
  *(uint8_t*)0x200000d7 = 5;
  *(uint8_t*)0x200000d8 = 5;
  *(uint8_t*)0x200000d9 = 0x10;
  *(uint16_t*)0x200000da = 0x400;
  *(uint8_t*)0x200000dc = 8;
  *(uint8_t*)0x200000dd = 1;
  *(uint8_t*)0x200000de = 0x81;
  *(uint8_t*)0x200000df = 9;
  *(uint8_t*)0x200000e0 = 5;
  *(uint8_t*)0x200000e1 = 3;
  *(uint8_t*)0x200000e2 = 0;
  *(uint16_t*)0x200000e3 = 0x40;
  *(uint8_t*)0x200000e5 = 0x81;
  *(uint8_t*)0x200000e6 = 0x59;
  *(uint8_t*)0x200000e7 = 6;
  *(uint8_t*)0x200000e8 = 2;
  *(uint8_t*)0x200000e9 = 0x30;
  *(uint8_t*)0x200000ea = 9;
  *(uint8_t*)0x200000eb = 5;
  *(uint8_t*)0x200000ec = 0xe;
  *(uint8_t*)0x200000ed = 0x10;
  *(uint16_t*)0x200000ee = 0x3ff;
  *(uint8_t*)0x200000f0 = 2;
  *(uint8_t*)0x200000f1 = 0x1f;
  *(uint8_t*)0x200000f2 = 0xe2;
  *(uint8_t*)0x200000f3 = 9;
  *(uint8_t*)0x200000f4 = 4;
  *(uint8_t*)0x200000f5 = -1;
  *(uint8_t*)0x200000f6 = 0x3f;
  *(uint8_t*)0x200000f7 = 3;
  *(uint8_t*)0x200000f8 = 0x49;
  *(uint8_t*)0x200000f9 = 0xbc;
  *(uint8_t*)0x200000fa = 0x6d;
  *(uint8_t*)0x200000fb = 9;
  *(uint8_t*)0x200000fc = 9;
  *(uint8_t*)0x200000fd = 5;
  *(uint8_t*)0x200000fe = 7;
  *(uint8_t*)0x200000ff = 8;
  *(uint16_t*)0x20000100 = 8;
  *(uint8_t*)0x20000102 = 0x1f;
  *(uint8_t*)0x20000103 = 0x80;
  *(uint8_t*)0x20000104 = 0x20;
  *(uint8_t*)0x20000105 = 9;
  *(uint8_t*)0x20000106 = 5;
  *(uint8_t*)0x20000107 = 0xe;
  *(uint8_t*)0x20000108 = 1;
  *(uint16_t*)0x20000109 = 8;
  *(uint8_t*)0x2000010b = 0x20;
  *(uint8_t*)0x2000010c = 0x85;
  *(uint8_t*)0x2000010d = 6;
  *(uint8_t*)0x2000010e = 7;
  *(uint8_t*)0x2000010f = 0x25;
  *(uint8_t*)0x20000110 = 1;
  *(uint8_t*)0x20000111 = 0x83;
  *(uint8_t*)0x20000112 = 5;
  *(uint16_t*)0x20000113 = 0xfff;
  *(uint8_t*)0x20000115 = 9;
  *(uint8_t*)0x20000116 = 5;
  *(uint8_t*)0x20000117 = 8;
  *(uint8_t*)0x20000118 = 2;
  *(uint16_t*)0x20000119 = 0x20;
  *(uint8_t*)0x2000011b = 0;
  *(uint8_t*)0x2000011c = 0x81;
  *(uint8_t*)0x2000011d = 5;
  *(uint8_t*)0x2000011e = 2;
  *(uint8_t*)0x2000011f = 0x30;
  syz_usb_connect(0, 0x120, 0x20000000, 0);
}
int main(void)
{
  syscall(__NR_mmap, 0x1ffff000ul, 0x1000ul, 0ul, 0x32ul, -1, 0ul);
  syscall(__NR_mmap, 0x20000000ul, 0x1000000ul, 7ul, 0x32ul, -1, 0ul);
  syscall(__NR_mmap, 0x21000000ul, 0x1000ul, 0ul, 0x32ul, -1, 0ul);
  setup_sysctl();
  for (procid = 0; procid < 6; procid++) {
    if (fork() == 0) {
      loop();
    }
  }
  sleep(1000000);
  return 0;
}
