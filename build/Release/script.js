
var spawn = require('child_process').spawn;
var debug = require('debug')('l2cap-ble');

var ATT_OP_ERROR                    = 0x01;
var ATT_OP_MTU_REQ                  = 0x02;
var ATT_OP_MTU_RESP                 = 0x03;
var ATT_OP_FIND_INFO_REQ            = 0x04;
var ATT_OP_FIND_INFO_RESP           = 0x05;
var ATT_OP_READ_BY_TYPE_REQ         = 0x08;
var ATT_OP_READ_BY_TYPE_RESP        = 0x09;
var ATT_OP_READ_REQ                 = 0x0a;
var ATT_OP_READ_RESP                = 0x0b;
var ATT_OP_READ_BLOB_REQ            = 0x0c;
var ATT_OP_READ_BLOB_RESP           = 0x0d;
var ATT_OP_READ_BY_GROUP_REQ        = 0x10;
var ATT_OP_READ_BY_GROUP_RESP       = 0x11;
var ATT_OP_WRITE_REQ                = 0x12;
var ATT_OP_WRITE_RESP               = 0x13;
var ATT_OP_HANDLE_NOTIFY            = 0x1b;
var ATT_OP_HANDLE_IND               = 0x1d;
var ATT_OP_HANDLE_CNF               = 0x1e;
var ATT_OP_WRITE_CMD                = 0x52;

var ATT_ECODE_SUCCESS               = 0x00;
var ATT_ECODE_INVALID_HANDLE        = 0x01;
var ATT_ECODE_READ_NOT_PERM         = 0x02;
var ATT_ECODE_WRITE_NOT_PERM        = 0x03;
var ATT_ECODE_INVALID_PDU           = 0x04;
var ATT_ECODE_AUTHENTICATION        = 0x05;
var ATT_ECODE_REQ_NOT_SUPP          = 0x06;
var ATT_ECODE_INVALID_OFFSET        = 0x07;
var ATT_ECODE_AUTHORIZATION         = 0x08;
var ATT_ECODE_PREP_QUEUE_FULL       = 0x09;
var ATT_ECODE_ATTR_NOT_FOUND        = 0x0a;
var ATT_ECODE_ATTR_NOT_LONG         = 0x0b;
var ATT_ECODE_INSUFF_ENCR_KEY_SIZE  = 0x0c;
var ATT_ECODE_INVAL_ATTR_VALUE_LEN  = 0x0d;
var ATT_ECODE_UNLIKELY              = 0x0e;
var ATT_ECODE_INSUFF_ENC            = 0x0f;
var ATT_ECODE_UNSUPP_GRP_TYPE       = 0x10;
var ATT_ECODE_INSUFF_RESOURCES      = 0x11;

var GATT_PRIM_SVC_UUID              = 0x2800;
var GATT_INCLUDE_UUID               = 0x2802;
var GATT_CHARAC_UUID                = 0x2803;

var GATT_CLIENT_CHARAC_CFG_UUID     = 0x2902;
var GATT_SERVER_CHARAC_CFG_UUID     = 0x2903;


var disc_services = {};
var disc_characteristics = {};
var disc_descriptors = {};

var discoverServices = function(uuids, servicesDiscovered) {
  var services = [];

  var callback = function(data) {
    var opcode = data[0];
    var i = 0;

    if (opcode === ATT_OP_READ_BY_GROUP_RESP) {
      var type = data[1];
      var num = (data.length - 2) / type;

      for (i = 0; i < num; i++) {
        services.push({
          startHandle: data.readUInt16LE(2 + i * type + 0),
          endHandle: data.readUInt16LE(2 + i * type + 2),
          uuid: (type == 6) ? data.readUInt16LE(2 + i * type + 4).toString(16) : data.slice(2 + i * type + 4).slice(0, 16).toString('hex').match(/.{1,2}/g).reverse().join('')
        });
      }
    }

    if (opcode !== ATT_OP_READ_BY_GROUP_RESP || services[services.length - 1].endHandle === 0xffff) {
      var serviceUuids = [];
      for (i = 0; i < services.length; i++) {
        if (uuids.length === 0 || uuids.indexOf(services[i].uuid) !== -1) {
          serviceUuids.push(services[i].uuid);
        }

        disc_services[services[i].uuid] = services[i];
	debug("added service " + disc_services[services[i].uuid].uuid + " with startHandle " + disc_services[services[i].uuid].startHandle + " and endHandle " + disc_services[services[i].uuid].endHandle );
	debug("services are now " + Object.keys(disc_services));
	
      }
      servicesDiscovered();
    } else {
      queueCommand(readByGroupRequest(services[services.length - 1].endHandle + 1, 0xffff, GATT_PRIM_SVC_UUID), callback);
    }
  }

  queueCommand(readByGroupRequest(0x0001, 0xffff, GATT_PRIM_SVC_UUID), callback);
};

var commandQueue = [];
var currentCommand = null;

var queueCommand = function(buffer, callback, writeCallback) {
  commandQueue.push({
    buffer: buffer,
    callback: callback,
    writeCallback: writeCallback
  });

  if (currentCommand === null) {
    while (commandQueue.length) {
      currentCommand = commandQueue.shift();

      debug(address + ': write: ' + currentCommand.buffer.toString('hex'));
      l2capBle_process.stdin.write(currentCommand.buffer.toString('hex') + '\n');

      if (currentCommand.callback) {
        break;
      } else if (currentCommand.writeCallback) {
        currentCommand.writeCallback();

        currentCommand = null;
      }
    }
  }
};

var readByGroupRequest = function(startHandle, endHandle, groupUuid) {
  var buf = new Buffer(7);

  buf.writeUInt8(ATT_OP_READ_BY_GROUP_REQ, 0);
  buf.writeUInt16LE(startHandle, 1);
  buf.writeUInt16LE(endHandle, 3);
  buf.writeUInt16LE(groupUuid, 5);

  return buf;
};

readByTypeRequest = function(startHandle, endHandle, groupUuid) {
  var buf = new Buffer(7);

  buf.writeUInt8(ATT_OP_READ_BY_TYPE_REQ, 0);
  buf.writeUInt16LE(startHandle, 1);
  buf.writeUInt16LE(endHandle, 3);
  buf.writeUInt16LE(groupUuid, 5);

  return buf;
};

discoverCharacteristics = function(serviceUuid, characteristicUuids, discoverDone) {
  var service = disc_services[serviceUuid];
  var characteristics = [];

  disc_characteristics[serviceUuid] = {};
  disc_descriptors[serviceUuid] = {};

  var callback = function(data) {
    var opcode = data[0];
    var i = 0;

    if (opcode === ATT_OP_READ_BY_TYPE_RESP) {
      var type = data[1];
      var num = (data.length - 2) / type;

      for (i = 0; i < num; i++) {
        characteristics.push({
          startHandle: data.readUInt16LE(2 + i * type + 0),
          properties: data.readUInt8(2 + i * type + 2),
          valueHandle: data.readUInt16LE(2 + i * type + 3),
          uuid: (type == 7) ? data.readUInt16LE(2 + i * type + 5).toString(16) : data.slice(2 + i * type + 5).slice(0, 16).toString('hex').match(/.{1,2}/g).reverse().join('')
        });
      }
    }

    if (opcode !== ATT_OP_READ_BY_TYPE_RESP || characteristics[characteristics.length - 1].valueHandle === service.endHandle) {

      var characteristicsDiscovered = [];
      for (i = 0; i < characteristics.length; i++) {
        var properties = characteristics[i].properties;

        var characteristic = {
          properties: [],
          uuid: characteristics[i].uuid
        };

        if (i !== 0) {
          characteristics[i - 1].endHandle = characteristics[i].startHandle - 1;
        }

        if (i === (characteristics.length - 1)) {
          characteristics[i].endHandle = service.endHandle;
        }

        disc_characteristics[serviceUuid][characteristics[i].uuid] = characteristics[i];

        if (properties & 0x01) {
          characteristic.properties.push('broadcast');
        }

        if (properties & 0x02) {
          characteristic.properties.push('read');
        }

        if (properties & 0x04) {
          characteristic.properties.push('writeWithoutResponse');
        }

        if (properties & 0x08) {
          characteristic.properties.push('write');
        }

        if (properties & 0x10) {
          characteristic.properties.push('notify');
        }

        if (properties & 0x20) {
          characteristic.properties.push('indicate');
        }

        if (properties & 0x40) {
          characteristic.properties.push('authenticatedSignedWrites');
        }

        if (properties & 0x80) {
          characteristic.properties.push('extendedProperties');
        }

        if (characteristicUuids.length === 0 || characteristicUuids.indexOf(characteristic.uuid) !== -1) {
          characteristicsDiscovered.push(characteristic);
	  debug("added characteristic " + characteristic.uuid);
        }
      }

      //this.emit('characteristicsDiscover', this._address, serviceUuid, characteristicsDiscovered);
      discoverDone(serviceUuid);
    } else {
      queueCommand(readByTypeRequest(characteristics[characteristics.length - 1].valueHandle + 1, service.endHandle, GATT_CHARAC_UUID), callback);
    }
  }

  queueCommand(readByTypeRequest(service.startHandle, service.endHandle, GATT_CHARAC_UUID), callback);
};

var write = function(serviceUuid, characteristicUuid, data, withoutResponse) {
  var characteristic = disc_characteristics[serviceUuid][characteristicUuid];

  if (withoutResponse) {
    queueCommand(writeRequest(characteristic.valueHandle, data, true), null, function() {
      //this.emit('write', this._address, serviceUuid, characteristicUuid);
      debug("write queued");
    });
  } else {
    queueCommand(writeRequest(characteristic.valueHandle, data, false), function(data) {
      var opcode = data[0];

      if (opcode === ATT_OP_WRITE_RESP) {
        //this.emit('write', this._address, serviceUuid, characteristicUuid);
	debug("got write response");
      }
    });
  }
};

writeRequest = function(handle, data, withoutResponse) {
  var buf = new Buffer(3 + data.length);

  buf.writeUInt8(withoutResponse ? ATT_OP_WRITE_CMD : ATT_OP_WRITE_REQ , 0);
  buf.writeUInt16LE(handle, 1);

  for (var i = 0; i < data.length; i++) {
    buf.writeUInt8(data.readUInt8(i), i + 3);
  }

  return buf;
};

// Running the C-program and piping output to console.log
var l2capBle_dir = "/home/johan/noble/build/Release/l2cap-ble";
//var address = "EE:A3:64:61:F2:72";
var address = "00:1B:DC:07:2D:12";
//var address = "12:2D:07:DC:1B:00";
var addressType = "public";
var l2capBle_process = spawn(l2capBle_dir, [address, addressType]);

l2capBle_process.stdout.on('data', function(data){
  console.log(data.toString());
  var line = data.toString();
  line = line.substring(0, line.length-1);
  debug("trying to find in " + line);
  if ((found = line.match(/^data (.*)$/))){
      debug("found="+found);
      var lineData = new Buffer(found[1], 'hex');

      if (currentCommand && lineData.toString('hex') === currentCommand.buffer.toString('hex')) {
        debug(address + ': echo ... echo ... echo ...');
      } else if (lineData[0] % 2 === 0) {
        debug(address + ': ignoring request/command ...');
      } else if (lineData[0] === ATT_OP_HANDLE_NOTIFY || lineData[0] === ATT_OP_HANDLE_IND) {
       /* var valueHandle = lineData.readUInt16LE(1);
        var valueData = lineData.slice(3);

        if (lineData[0] === ATT_OP_HANDLE_IND) {
          queueCommand(this.handleConfirmation(), null, function() {
            this.emit('handleConfirmation', this._address, valueHandle);
          }.bind(this));
        }

        for (var serviceUuid in this._services) {
          for (var characteristicUuid in this._characteristics[serviceUuid]) {
            if (this._characteristics[serviceUuid][characteristicUuid].valueHandle === valueHandle) {
              this.emit('notification', this._address, serviceUuid, characteristicUuid, valueData);
            }
          }
        }*/
      } else if (!currentCommand) {
        debug(address + ': uh oh, no current command');
      } else {
       /* if (lineData[0] === ATT_OP_ERROR &&
            (lineData[4] === ATT_ECODE_AUTHENTICATION || lineData[4] === ATT_ECODE_AUTHORIZATION || lineData[4] === ATT_ECODE_INSUFF_ENC) &&
            this._security !== 'medium') {
          this.upgradeSecurity();

          return;
        }*/

        currentCommand.callback(lineData);

        currentCommand = null;

        while(commandQueue.length) {
          currentCommand = commandQueue.shift();

          debug(address + ': write: ' + currentCommand.buffer.toString('hex'));
          l2capBle_process.stdin.write(currentCommand.buffer.toString('hex') + '\n');

          if (currentCommand.callback) {
            break;
          } else if (currentCommand.writeCallback) {
            currentCommand.writeCallback();

            currentCommand = null;
          }
        }
      }
    
}
});

discoverServices([], function(){
  debug("services discovered: " + Object.keys(disc_services));
  Object.keys(disc_services).forEach(function(key) {
    debug("discover characteristics for service " + key);
    discoverCharacteristics(key, [], function(serviceUuid){
      debug("done discovering characteristics for service " + serviceUuid);
      if(serviceUuid == "1337"){
	debug("has " + disc_characteristics[serviceUuid]["1338"].uuid);
	var data = new Buffer("0123456789012345678901234567890123456789", "hex");
	for(var i = 0; i < 1000; i++){
	  write(serviceUuid, disc_characteristics[serviceUuid]["1338"].uuid, data, true);
	  console.log("" + (20*8*i) + "bits written");
	}
	console.log("Done writing");
      }
    });
  });
  
});

