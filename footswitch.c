// acquire exclusive access to RDing footswitch and send MIDI events

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <termios.h>
#include <signal.h>

#define KEY_RELEASE 0
#define KEY_PRESS   1
#define KEY_HOLD    2

char *symbolic_device_dir = "/dev/input/by-id/";
char *symbolic_device_name = "usb-RDing_FootSwitchV1.1-event-mouse";
char *reastream_id = "default";

int resolve_device_path(char* device) {
  char path_temp[PATH_MAX];
  strcpy(path_temp, symbolic_device_dir);
  strcat(path_temp, symbolic_device_name);
  ssize_t symbolic_device_dir_len = strlen(symbolic_device_dir);
  ssize_t resolved_len = readlink(
      path_temp,
      path_temp + symbolic_device_dir_len,
      sizeof(path_temp) - symbolic_device_dir_len);
  if (resolved_len == -1) {
    fprintf(stderr, "readlink failed, err %d\n", errno);
    return -1;
  }
  path_temp[symbolic_device_dir_len + resolved_len] = 0;
  if (realpath(path_temp, device) == NULL) {
    fprintf(stderr, "realpath failed, err %d\n", errno);
    return -1;
  }
  return 0;
}

int open_device(char* device) {
  int fevdev = open(device, O_RDONLY);
  if (fevdev == -1) {
    fprintf(stderr, "failed to open device, err %d\n", errno);
    return -1;
  }
  int result = ioctl(fevdev, EVIOCGRAB, 1);
  if (result == -1) {
    fprintf(stderr, "failed to get exclusive access, err %d\n", errno);
    return -1;
  }
  return fevdev;
}

int socket_fd;
struct sockaddr_in socket_addr;
int open_broadcast_socket() {
  socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_fd == -1) {
    fprintf(stderr, "failed to create socket, err %d\n", errno);
    return -1;
  }
  int broadcast_enable = 1;
  int result = setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST,
                          &broadcast_enable, sizeof(broadcast_enable));
  if (result == -1) {
    fprintf(stderr, "failed to broadcast enable socket, err %d\n", errno);
    return -1;
  }
  struct sockaddr_in source_addr;
  memset(&source_addr, 0, sizeof(source_addr));
  source_addr.sin_family = AF_INET;
  source_addr.sin_addr.s_addr = INADDR_ANY;
  source_addr.sin_port = htons(58710);
  result = bind(socket_fd, (struct sockaddr *)&source_addr,
                sizeof(source_addr));
  if (result == -1) {
    fprintf(stderr, "failed to bind to socket, err %d\n", errno);
    return -1;
  }
  memset(&socket_addr, 0, sizeof(socket_addr));
  socket_addr.sin_family = AF_INET;
  socket_addr.sin_addr.s_addr = inet_addr("255.255.255.255");
  socket_addr.sin_port = htons(58710);
  return 0;
}

int broadcast(char* message, int message_len) {
  int result = sendto(socket_fd, message, message_len, 0,
                      (struct sockaddr*)&socket_addr, sizeof(socket_addr));
  if (result == -1) {
    fprintf(stderr, "failed to broadcast message, err %d\n", errno);
    return -1;
  }
  return result;
}

char reastream_packet[0x48];
void init_reastream_packet() {
  memset(&reastream_packet, 0, sizeof(reastream_packet));
  strcpy(reastream_packet, "mRSR");
  reastream_packet[0x04] = 0x48;
  strcpy(reastream_packet + 0x08, reastream_id);
  reastream_packet[0x28] = 0x01;
  reastream_packet[0x2c] = 0x18;
}

void broadcast_midi_message(char key, int on) {
  reastream_packet[0x30] = 0xaa; // mystery byte?
  reastream_packet[0x40] = 0x90;
  reastream_packet[0x41] = key;
  reastream_packet[0x42] = on ? 0x7f : 0x00;
  broadcast(reastream_packet, 0x48);
}

int main(int argc, char* argv[]) {
  char device[PATH_MAX];
  if (resolve_device_path(device) == -1) {
    return 1;
  }
  int fevdev = open_device(device);
  if (fevdev == -1) {
    return 1;
  }
  if (open_broadcast_socket() == -1) {
    return 1;
  }
  init_reastream_packet();

  struct input_event ev;
  int ev_size = sizeof(struct input_event);
  int bytes_read;
  char note;
  while (1) {
    bytes_read = read(fevdev, &ev, ev_size);
    if (bytes_read < ev_size) {
      break;
    }
    if (ev.type == EV_MSC && ev.code == 4) {
      note = (char)(ev.value & 0xFF);
    } else if (ev.type == EV_KEY) {
      if (ev.value == KEY_HOLD) {
        continue;
      }
      printf("Note %d ", note);
      switch (ev.value) {
      case KEY_PRESS:
        printf("pressed\n");
        broadcast_midi_message(note, 1);
        break;
      case KEY_RELEASE:
        printf("released\n");
        broadcast_midi_message(note, 0);
        break;
      }
    }
  }

  close(fevdev);
  return 0;
}
