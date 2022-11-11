#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"


// Global varibale used to keep track if the system is mounted or not
// 1 = is_mounted; 0 = is_umounted
int is_mounted = 0;

// Global varibale used to keep track if the system has permission to be written on
// 1 = allowed; 0 = not allowed
int is_written = 0;

/**
  op_creator:
   parameters:
    blockID: the ID of the block that the operator will execute the command
    diskID: the ID of the disk in which the oeprator will execute the command
    cmd: command that will be executed in the block and disk passed as parameters
    reserved: unused bits
   
   description: this function returns the operator in the form of a unsigned integer of 32 bits, 
   using the first 8 bits for the blockID, the next 4 bits for the diskID, the next 6 bits for
   the command, and the rest of the bits (14 bits) for the reserved data.
 */
uint32_t op_creator(uint32_t blockID, uint32_t diskID, uint32_t cmd, uint32_t reserved){
  uint32_t op = 0x0, tempBlockID, tempDiskID, tempCMD, tempReserved;

  tempBlockID = blockID&0xff;
  tempDiskID = (diskID&0xf) << 8;
  tempCMD = (cmd&0x7f) << 12;
  tempReserved = (reserved&0x3fff) << 18;

  op = tempBlockID | tempDiskID | tempCMD | tempReserved;

  return op;
  
}

/**
  mdadm_mount: 
   description: gives the device driver the operation with the command to mount the data into the JBOD and
   make them ready to serve commands
*/
int mdadm_mount(void) {
  uint32_t cmd = op_creator(0, 0, JBOD_MOUNT, 0);

  if(is_mounted == 0){
    if(jbod_operation(cmd, NULL) == 0){
      is_mounted = 1;
      return 1;
    }
  }
  return -1;
 }


/**
  mdadm_unmount:
   description: gives the device driver the operation with the command to unmount all of the data form 
   the JBOD. After that command is given, executing other commands will not be allowed.
*/
int mdadm_unmount(void) {
  uint32_t cmd = op_creator(0, 0, JBOD_UNMOUNT, 0);

  if(is_mounted == 1){
    if(jbod_operation(cmd, NULL) == 0){
      is_mounted = 0;
      return 1;
    }
  }
  return -1;
 }

/**
  mdadm_write_permission:
    function: gives the device driver the operation with the command to allow writting on the system.
    After that command is given, writing on the system is allowed.
*/
int mdadm_write_permission(void){
  
  uint32_t cmd = op_creator(0,0,JBOD_WRITE_PERMISSION, 0);

  if(is_written == 0){
    if(jbod_operation(cmd, NULL) == 0){
      is_written = 1;
      return 0;
    }
    return -1;
  }
  return -1;
}

/**
  mdadm_revoke_write_permission:
    description: gives the device driver the operation with the command to revoke permission of writting on the system.
    After that command is given, writing on the system is NOT allowed.
 */
int mdadm_revoke_write_permission(void){
  uint32_t cmd = op_creator(0, 0, JBOD_REVOKE_WRITE_PERMISSION, 0);

  if(is_written == 1){
    if(jbod_operation(cmd, NULL) == 0){
      is_written = 0;
      return 0;
    }
    return -1;
  }
  return -1;
}

/**
  getDiskID:
    parameters: 
      start_addr: starting address of the system
    description: helper function that returns the disk ID from the starting address
 */
uint8_t getDiskID(uint32_t start_addr){
  return (start_addr)/(JBOD_BLOCK_SIZE * JBOD_NUM_BLOCKS_PER_DISK);
}

/**
  getBlockID:
    parameters:
      start_addr: starting address of the system
      diskId: the disk ID of the current disk
    description: helper function that returns the block ID
 */
uint8_t getBlockID(uint32_t start_addr, uint8_t diskId){
  return (start_addr - ((diskId) * JBOD_BLOCK_SIZE * JBOD_NUM_BLOCKS_PER_DISK))/(JBOD_BLOCK_SIZE);
}

/**
  mdadm_read:
    parameters:
      start_addr: address from where we will start reading data
      read_len: the number of bytes that need to be read
      read_buf: the address of the buffer we will read the data into

    description: reads read_len bytes into read_buf starting at the start_addr
 */
int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)  {

  // If the read_len is zero and the read_buf is NULL, it shoud return 0 (length read)
  if(read_len == 0 && read_buf == NULL)
    return 0;
  if(is_mounted == 0                         // If the disk are not mounted, the function fails
     || start_addr > 1048576                 // If the starting address is greater than the availabe space, the function fails
     || read_len > 2048                      // If the bytes to read is greater than 2048 bytes, the funtion fails
     || start_addr + read_len > 1048576      // If executing the read will go over the disk space, the function fails
     || read_buf == NULL)                    // If the buffer where the data will be read is NULL, the function fails
    return -1;

  /* Setting up the needed variables:
       read_bytes: the bytes that need to be readed
       current_pos: how many bytes have been read (or the current position int the read_buf)
       diskId: the starting disk ID
       blockId: the starting block ID
       offset: the number of bytes from the beginning of the current block that the read will start from
       tempBuffer: 
 */
  uint32_t read_bytes = read_len;
  uint8_t *current_pos = read_buf;
  
  uint8_t diskId =  getDiskID(start_addr);
  uint8_t blockId = getBlockID(start_addr,diskId);
  uint32_t offset = start_addr % 256;

  uint8_t *tempBuffer = (uint8_t *)malloc(256);

  // While loop that will iteerate until all of the bytes were read
  
  while(read_bytes != 0){
    
    // Set the I/O to the disk ID given
    uint32_t cmd = op_creator(blockId, diskId, JBOD_SEEK_TO_DISK, 0);
    jbod_operation(cmd, NULL);

    // Set the I/O to the block ID given
    cmd = op_creator(blockId, diskId, JBOD_SEEK_TO_BLOCK, 0);
    jbod_operation(cmd, NULL);

    // Read the full block with disk ID "diskId" and block ID "blockId"
    // If the cache is enabled, use it to search for the block. If the block is not found, add it to the cache
    // and read using the JBOD command
    if(cache_enabled()){
      if(cache_lookup(diskId, blockId, tempBuffer) != 1){
	cmd = op_creator(blockId, diskId, JBOD_READ_BLOCK, 0);
	jbod_operation(cmd, tempBuffer);

	cache_insert(diskId, blockId, tempBuffer);
      }
    }else{
      cmd = op_creator(blockId, diskId, JBOD_READ_BLOCK, 0);
      jbod_operation(cmd, tempBuffer);
    }
    
    

    if(offset == 0){
      if(read_bytes < JBOD_BLOCK_SIZE){              // Reading a partial block starting from the begining
	memcpy(current_pos, tempBuffer, read_bytes);
	current_pos += read_bytes;
	read_bytes = 0;
      }else{                                         // Reading a complete block
	memcpy(current_pos, tempBuffer, JBOD_BLOCK_SIZE);
	current_pos += JBOD_BLOCK_SIZE;
	read_bytes -= JBOD_BLOCK_SIZE;
      }
    }else if(offset != 0){                   
      if(read_bytes < (JBOD_BLOCK_SIZE - offset)){   // Reading a partial block starting from an offsetted position and ending before the end of the block
	memcpy(current_pos, tempBuffer + offset, read_bytes);
	current_pos += read_bytes;
	read_bytes = 0;
      }else {                                        // Reading a partial block starting from an offsetted position and reading to the end of the block
	memcpy(current_pos, tempBuffer + offset, (JBOD_BLOCK_SIZE - offset));
	current_pos += (JBOD_BLOCK_SIZE - offset);
	read_bytes -= JBOD_BLOCK_SIZE - offset;
      }
    }

    // After the first read, the offset will always be 0;
    offset = 0;

    // Update the disk and block IDs
    if(blockId == 255){    // If we are at the last block in a disk, set block ID to 0 and disk ID to the next ID
      blockId = 0;
      diskId += 1;
    }else{                 // Otherwise, just increase the block ID
      blockId += 1;
    }

  }

  // Free the memory allocated
  free(tempBuffer);
  tempBuffer = NULL;
  
  return read_len;
}


/**
  mdadm_write:
    Parameters:
      start_addr: starting address of the system
      write_len: the length we will be writing in bytes
      write_buf: the buffer that contains the data that will be written on the system
    Description: writes the data in the corresponding address of a multi disk, each with multiple 
    blocks of data, storage system
 */
int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf) {

  // If statemetns to catch invalid parameter inputs
  if(write_len == 0 && write_buf == NULL)
    return 0;
  if(is_mounted == 0
     || is_written == 0
     || write_len > 2048
     || start_addr + write_len > 1048576
     || write_buf == NULL)
    return -1;

  /* 
     Set up of local variables to implement the function:
       diskId: the disk from the storage system that we are currently looking at
       blockId: the block of 256 bytes from the current disk that we are currently lookin at
       offset: the offset from the begining of blockId
       written_len: copy of write_len, to be able to return the number of bytes written
       write_buff_offset: keeps track of the position in write_buf that we are currently writting
       tempBuffer: temprary buffer to hold the data to be copyed onto the system
  */
  uint8_t diskId = getDiskID(start_addr);
  uint8_t blockId = getBlockID(start_addr, diskId);
  uint8_t offset  = start_addr % 256;

  uint32_t written_len = write_len;
  uint32_t write_buff_offset = 0;

  uint8_t *tempBuffer = (uint8_t *) malloc(256);

  // Loop that will iterate until all of the bytes are written
  while(write_len != 0){

    // set the current I/O location(diskId and blockId) in the system
    uint32_t cmd = op_creator(blockId, diskId, JBOD_SEEK_TO_DISK, 0);
    jbod_operation(cmd, NULL);

    cmd = op_creator(blockId, diskId, JBOD_SEEK_TO_BLOCK, 0);
    jbod_operation(cmd, NULL);

    // variable to tell the program if it needs to update the contents of a block inside the cache
    int cache_updated = 0;

    // Read the whole block with disk ID "diskId" and block ID "blockId"
    // If the cache is enabled, serach for the block in the cache first. If it is found, pass its conents to the temprary Buffer.
    // Otherwise read the block with the JBOD command
    if(cache_enabled()){
      if(cache_lookup(diskId, blockId, tempBuffer) != 1){
	cmd = op_creator(blockId, diskId, JBOD_READ_BLOCK, 0);
	jbod_operation(cmd, tempBuffer);
	
	cmd = op_creator(blockId, diskId, JBOD_SEEK_TO_DISK, 0);
	jbod_operation(cmd, NULL);

	cmd = op_creator(blockId, diskId, JBOD_SEEK_TO_BLOCK, 0);
	jbod_operation(cmd, NULL);
	
      }else{
	cache_updated = 1;
      }
    }else{
      cmd = op_creator(blockId, diskId, JBOD_READ_BLOCK, 0);
      jbod_operation(cmd, tempBuffer);
    
      cmd = op_creator(blockId, diskId, JBOD_SEEK_TO_DISK, 0);
      jbod_operation(cmd, NULL);

      cmd = op_creator(blockId, diskId, JBOD_SEEK_TO_BLOCK, 0);
      jbod_operation(cmd, NULL);
    }
     
	
    
    if(offset == 0){
      
      if(write_len >= 256){         // Writting a complete block
	cmd = op_creator(blockId, diskId, JBOD_WRITE_BLOCK, 0);
	
	memcpy(tempBuffer, write_buf + write_buff_offset, 256);
	
	jbod_operation(cmd, tempBuffer);
	write_len = write_len - 256;
	write_buff_offset += 256;

      }else{                        // Writing a partial block from the beggining and reading less than 256 bytes
	
	memcpy(tempBuffer, write_buf + write_buff_offset, write_len);

	cmd = op_creator(blockId, diskId, JBOD_WRITE_BLOCK, 0);
	jbod_operation(cmd, tempBuffer);
	write_buff_offset = write_buff_offset + write_len;
	write_len = 0;
      }
      
    }else if(offset != 0){
      if(write_len >= 256- offset){ // Writing a partial block from an offseted position to the end
	
	memcpy(tempBuffer + offset, write_buf + write_buff_offset, 256 - offset);

	cmd = op_creator(blockId, diskId, JBOD_WRITE_BLOCK, 0);
	jbod_operation(cmd, tempBuffer);
	write_len = write_len - 256 + offset;
	write_buff_offset = write_buff_offset + 256 - offset;
	
      }else{                        // Writing a partial block from an offseted position and stoping before the end
	
	memcpy(tempBuffer + offset, write_buf, write_len);

	cmd = op_creator(blockId, diskId, JBOD_WRITE_BLOCK, 0);
	jbod_operation(cmd, tempBuffer);
	write_buff_offset = write_len;
	write_len = 0;
	
      }
    }

    // If the block already was in the cache,  update its contents. Otherwiese insert the block with the written data
    if(cache_updated == 1){
      cache_update(diskId, blockId, tempBuffer);
    }else{
      cache_insert(diskId, blockId, tempBuffer);
    }
    
    // Only the first write in the iteration can have an offset that is not equal to zero
    offset = 0;

    // Move to the nest position (disk and/or block) in the I/O 
    if(blockId == 255){
      blockId = 0;
      diskId += 1;
    }else{
      blockId += 1;
    }
    
  }

  // free the memory allocated
  free(tempBuffer);
  tempBuffer = NULL;
  
  return written_len;
}

