#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <string.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#include <stdint.h>

/*/
for trouble shooting:

    enable kernel dynamic debug logging via dmsg for ble:
echo "file net/bluetooth/hci_conn.c =flmtp" > /sys/kernel/debug/dynamic_debug/control
    and repeat with hci_core.c, lcap_sock.c, lcap.core.c or others

    disable with
echo "file net/bluetooth/hci_conn.c =_" > /sys/kernel/debug/dynamic_debug/control

    check with
grep "\[bluetooth\][a-z_]* =[^_ ]\+" /sys/kernel/debug/dynamic_debug/control

wildcards did not work for me, ymmv.
/*/

#define ATT_CID 4

#define BDADDR_LE_PUBLIC       0x01
#define BDADDR_LE_RANDOM       0x02


#define ATT_OP_READ_BY_TYPE_REQ		0x08
#define ATT_OP_READ_BY_TYPE_RESP	0x09
#define ATT_OP_READ_BY_GROUP_REQ	0x10
#define ATT_OP_READ_BY_GROUP_RESP	0x11
#define ATT_OP_WRITE_REQ 		0x12
#define ATT_OP_WRITE_RESP 		0x13
#define ATT_OP_WRITE_CMD		0x52
#define GATT_PRIM_SVC_UUID		0x2800
#define GATT_CHARAC_UUID 		0x2803

struct sockaddr_l2 {
  sa_family_t    l2_family;
  unsigned short l2_psm;
  bdaddr_t       l2_bdaddr;
  unsigned short l2_cid;
  uint8_t        l2_bdaddr_type;
};

#define L2CAP_CONNINFO  0x02
struct l2cap_conninfo {
  uint16_t       hci_handle;
  uint8_t        dev_class[3];
};

int lastSignal = 0;

static void signalHandler(int signal) {
  lastSignal = signal;
}

int connect_to_peripheral(char* addr, char* addr_type) {
  char *hciDeviceIdOverride = NULL;
  int hciDeviceId = 0;
  char controller_address[18];
  struct hci_dev_info device_info;
  int hciSocket = -1;

  int l2capSock = -1;
  struct sockaddr_l2 sockAddr;
  struct l2cap_conninfo l2capConnInfo;
  socklen_t l2capConnInfoLen;
  int hciHandle;
  int result;

  // remove buffering
// setbuf(stdin, NULL);
// setbuf(stdout, NULL);
// setbuf(stderr, NULL);
  
  // use the first available device
  hciDeviceId = hci_get_route(NULL);

  if (hciDeviceId < 0) {
    hciDeviceId = 0; // use device 0, if device id is invalid
  }

  // open controller
  hciSocket = hci_open_dev(hciDeviceId);
  if (hciSocket == -1) {
    printf("connect hci_open_dev(hci%i): %s\n", hciDeviceId, strerror(errno));
    goto done;
  }

  // get local controller address
  result = hci_devinfo(hciDeviceId, &device_info);
  if (result == -1) {
    printf("connect hci_deviceinfo(hci%i): %s\n", hciDeviceId, strerror(errno));
    goto done;
  }
  ba2str(&device_info.bdaddr, controller_address);
  printf("info using %s@hci%i\n", controller_address, hciDeviceId);

  // create socket
  l2capSock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
  if (l2capSock  == -1) {
    printf("connect socket(hci%i): %s\n", hciDeviceId, strerror(errno));
    goto done;
  }

  // bind
  memset(&sockAddr, 0, sizeof(sockAddr));
  sockAddr.l2_family = AF_BLUETOOTH;
  // Bind socket to the choosen adapter by using the controllers BT-address as source
  // see l2cap_chan_connect source and hci_get_route in linux/net/bluetooth
  bacpy(&sockAddr.l2_bdaddr, &device_info.bdaddr);
  sockAddr.l2_cid = htobs(ATT_CID);
  result = bind(l2capSock, (struct sockaddr*)&sockAddr, sizeof(sockAddr));
  if (result == -1) {
    printf("connect bind(hci%i): %s\n", hciDeviceId, strerror(errno));
    goto done;
  }

  // connect
  memset(&sockAddr, 0, sizeof(sockAddr));
  sockAddr.l2_family = AF_BLUETOOTH;
  str2ba(addr, &sockAddr.l2_bdaddr);
  sockAddr.l2_bdaddr_type = strcmp(addr_type, "random") == 0 ? BDADDR_LE_RANDOM : BDADDR_LE_PUBLIC;
  sockAddr.l2_cid = htobs(ATT_CID);

  result = connect(l2capSock, (struct sockaddr *)&sockAddr, sizeof(sockAddr));
  if (result == -1) {
    char buf[1024] = { 0 };
    ba2str( &sockAddr.l2_bdaddr, buf );
    printf("connect connect(hci%i): %s\n", hciDeviceId, strerror(errno));
    goto done;
  }

  // get hci_handle
  l2capConnInfoLen = sizeof(l2capConnInfo);
  result = getsockopt(l2capSock, SOL_L2CAP, L2CAP_CONNINFO, &l2capConnInfo, &l2capConnInfoLen);
  if (result == -1) {
    printf("connect getsockopt(hci%i): %s\n", hciDeviceId, strerror(errno));
    goto done;
  }
  hciHandle = l2capConnInfo.hci_handle;

  printf("connect success\n");
  goto success;
  
  done:
  if (l2capSock != -1)
    close(l2capSock);
  if (hciSocket != -1)
    close(hciSocket);
  printf("disconnect\n");
  
  success:  
  /*
  result = hci_le_conn_update(hciSocket, hciHandle, 
		     htobs(0x0006), // min interval
		     htobs(0x0006),  // max interbval
		     0,  // latency
		     htobs(0x0100), // supervision to
		     10000 // to
		    );
  
  if(result != 0){
      printf("ERROR: Conn update failed: %d. %s\n", result, strerror(errno));
      exit(1);
  }
  */
  return l2capSock;
}

void writeUInt8(uint8_t* buf, uint8_t val)
{
  buf[0] = val;
}

void writeUInt16LE(uint8_t* buf, uint16_t val)
{
  buf[0] = val & 0xff;
  buf[1] = (val >> 8) & 0xff;
}

uint16_t readUInt16LE(uint8_t* buf)
{
  return (buf[0] & 0xff) | (buf[1] << 8);
}

uint8_t readUInt8(uint8_t* buf)
{
  return buf[0];
}

int readByGroupRequest(uint8_t* buf, uint16_t startHandle, uint16_t endHandle, uint16_t groupUuid) {
  writeUInt8(&buf[0], ATT_OP_READ_BY_GROUP_REQ);
  writeUInt16LE(&buf[1], startHandle);
  writeUInt16LE(&buf[3], endHandle);
  writeUInt16LE(&buf[5], groupUuid);
  return 7;
};

int readByTypeRequest(uint8_t* buf, uint16_t startHandle, uint16_t endHandle,uint16_t groupUuid) {
  writeUInt8(&buf[0], ATT_OP_READ_BY_TYPE_REQ);
  writeUInt16LE(&buf[1], startHandle);
  writeUInt16LE(&buf[3], endHandle);
  writeUInt16LE(&buf[5], groupUuid);
  return 7;
};

#define COMMAND_QUEUE_SIZE	1024
#define MAX_COMMAND_LENGTH	128

uint8_t command_queue[COMMAND_QUEUE_SIZE][MAX_COMMAND_LENGTH];
int command_length[COMMAND_QUEUE_SIZE];
int queue_start = 0;
int queue_end = 0;
int queue_length = 0;

int queue_command(uint8_t* command, int cmd_length)
{
  if(queue_length < COMMAND_QUEUE_SIZE) // There is space left on the command queueu
  {
    memcpy(command_queue[queue_end], command, cmd_length);
    command_length[queue_end] = cmd_length;
    queue_end = (queue_end + 1) % COMMAND_QUEUE_SIZE;
    queue_length++;
    return 0;
  }
  else 
  {
    return -1; // Queue full 
  }
}


char l2capSockBuf[256];
char stdinBuf[256 * 2 + 1];

int write_to_socket(char* stdinBuf, int length, int l2capSock)
{
	

	int len;
	int i = 0;
	unsigned int data;
	while(stdinBuf[i] != '\n') {
	  sscanf(&stdinBuf[i], "%02x", &data);

	  l2capSockBuf[i / 2] = data;

	  i += 2;
	}

	len = write(l2capSock, l2capSockBuf, (len - 1) / 2);

	printf("write = %s\n", (len == -1) ? strerror(errno) : "success");
      return 0;
}

int writeRequest(uint8_t* buf, uint16_t handle, uint8_t* data, int len, int withoutResponse) {
  writeUInt8(&buf[0], withoutResponse ? ATT_OP_WRITE_CMD : ATT_OP_WRITE_REQ);
  writeUInt16LE(&buf[1], handle);
  int i;
  for (i = 0; i < len; i++) {
    writeUInt8(&buf[i+3], readUInt8(&data[i]));
  }

  return len+3;
};


  
  uint8_t buffer[128];




#define MAX_SERVICES		32
#define MAX_CHARACTERISTICS	32

uint16_t services_startHandle[MAX_SERVICES];
uint16_t services_endHandle[MAX_SERVICES];
uint16_t services_uuid[MAX_SERVICES];
int services_length = 0;

uint16_t characteristics_startHandle[MAX_CHARACTERISTICS];
uint16_t characteristics_valueHandle[MAX_CHARACTERISTICS];
uint16_t characteristics_uuid[MAX_CHARACTERISTICS];
uint8_t characteristics_properties[MAX_CHARACTERISTICS];
int characteristics_length;

uint8_t write_data[] = {0x01,0x23,0x45,0x67,0x89,0x01,0x23,0x45,0x67,0x89,0x01,0x23,0x45,0x67,0x89,0x01,0x23,0x45,0x67,0x89};
int write_data_len = 20;

int write_characteristic(uint16_t characteristic, uint8_t* data, int data_length, int withoutResponse) {

  if (withoutResponse) {
    int len = writeRequest(buffer, characteristics_valueHandle[characteristic] , data, data_length, 1);
    queue_command(buffer, len);
    printf("write char queued\n");
  }/* else {
    queueCommand(writeRequest(characteristic.valueHandle, data, false), function(data) {
      var opcode = data[0];

      if (opcode === ATT_OP_WRITE_RESP) {
        //this.emit('write', this._address, serviceUuid, characteristicUuid);
	debug("got write response");
      }
    });
  }*/
};

int handle_read_data(uint8_t* data, int len)
{
  uint8_t att_opcode = data[0];
  uint8_t type;
  int num;
  int i;
  switch(att_opcode)
  {
    case ATT_OP_READ_BY_GROUP_RESP:
      printf("Got response\n");
      type = data[1];
      num = (len - 2) / type;
      for (i = 0; i < num; i++) {
	services_startHandle[services_length] = readUInt16LE(&data[2 + i * type + 0]);
	services_endHandle[services_length] = readUInt16LE(&data[2 + i * type + 2]);
	if(type != 6)
	{
	    printf("Error, type is not 6\n");
	    exit(1);
	}
	services_uuid[services_length] = readUInt16LE(&data[2 + i * type + 4]);
	printf("Added service %02x with start handle %02x and end handle %02x\n", services_uuid[services_length], services_startHandle[services_length], services_endHandle[services_length]);
	
	// Now discover characteristics for 1337
	if(services_uuid[services_length] == 0x1337)
	{
	  printf("Discovering characterisstics\n");
	  int length = readByTypeRequest(buffer, services_startHandle[services_length], services_endHandle[services_length], GATT_CHARAC_UUID);
	    int err = queue_command(buffer, length);
	    if(err != 0)
	    {
	      printf("Error queueing command!\n");
	      return 1;
	    }
	}
	services_length++;
      }
      break;
    case ATT_OP_READ_BY_TYPE_RESP:
      type = data[1];
      num = (len - 2) / type;

      for (i = 0; i < num; i++) {
	characteristics_startHandle[characteristics_length] = readUInt16LE(&data[2 + i * type + 0]);
	characteristics_properties[characteristics_length] = readUInt8(&data[2 + i * type + 2]);
	characteristics_valueHandle[characteristics_length] = readUInt16LE(&data[2 + i * type + 3]);
	if(type != 7)
	{
	   printf("Error, type is not 7\n");
	   exit(1);
	}
	characteristics_uuid[characteristics_length] = readUInt16LE(&data[2 + i * type + 5]);
	printf("Added characteristics %02x with start handle %02x, properties %02x, valueHandle %02x\n", characteristics_uuid[characteristics_length], characteristics_startHandle[characteristics_length], characteristics_properties[characteristics_length], characteristics_valueHandle[characteristics_length]);
	
	if(characteristics_uuid[characteristics_length] == 0x1338)
	{
	  // We can now write to this characteristic
	  int j;
	  for(j = 0; j < 1000; j++)
	  {
	    write_characteristic(characteristics_length, write_data, write_data_len, 1);
	  }
	  
	}
	
	characteristics_length++;
      }
      break;
    default:
      printf("Unknown opcode: %u\n", att_opcode);
  }
}

char peripheral_addr[] = "00:1B:DC:07:2D:12";
char peripheral_addr_type[] = "public";

int main(int argc, const char* argv[]) {

  int l2capSock = connect_to_peripheral(peripheral_addr, peripheral_addr_type);
  
  // Discover services request
  size_t length = readByGroupRequest(buffer, 0x0001, 0xffff, GATT_PRIM_SVC_UUID);
  int err = queue_command(buffer, length);
  if(err != 0)
  {
    printf("Error queueing command!\n");
    return 1;
  }

  fd_set rfds;
  struct timeval tv;
  int len;
  unsigned int data;
  int result;
  int i;
  
  while(1) {
    
      FD_ZERO(&rfds);
      FD_SET(l2capSock, &rfds);
      
    if(queue_length > 0) // There is a command waiting to be written
    {
      
      len = write(l2capSock, command_queue[queue_start], command_length[queue_start]);
      queue_start = (queue_start+1) % COMMAND_QUEUE_SIZE;
      queue_length--;
      printf("write = %s\n", (len == -1) ? strerror(errno) : "success");
      
      // Check if there is something to read without blocking      
      tv.tv_sec = 0;
      tv.tv_usec = 0;

      result = select(l2capSock + 1, &rfds, NULL, NULL, &tv);

      if (-1 == result) {
	printf("result -1\n");
      } else if (result) {
	
	if (FD_ISSET(l2capSock, &rfds)) {
	  len = read(l2capSock, l2capSockBuf, sizeof(l2capSockBuf));
	  
	  if (len <= 0) {
	    //printf("breaking\n");
	    continue; //break;
	  }
	  
	  printf("1: data read from l2capSock: ");
	  for(i = 0; i < len; i++) {
	    printf("%02x", ((int)l2capSockBuf[i]) & 0xff);
	  }
	  printf("\n");
	  
	  handle_read_data(l2capSockBuf, len);
	}
      }
      
    }
    else 
    {
      tv.tv_sec = 1;
      tv.tv_usec = 0;
      
      result = select(l2capSock + 1, &rfds, NULL, NULL, &tv);
      
      if (-1 == result) {
	printf("result -1\n");
      } else if (result) {
	
	if (FD_ISSET(l2capSock, &rfds)) {
	  len = read(l2capSock, l2capSockBuf, sizeof(l2capSockBuf));
	  
	  if (len <= 0) {
	    //printf("breaking\n");
	    continue; //break;
	  }
	  
	  printf("2: data read from l2capSock: ");
	  for(i = 0; i < len; i++) {
	    printf("%02x", ((int)l2capSockBuf[i]) & 0xff);
	  }
	  printf("\n");
	  
	  handle_read_data(l2capSockBuf, len);
	}
      }
    }
    
  }
  
  return 0;
  
}