/*
   implementation of API, by Andrew McMahon
*/

#include "def.h"
//custom comparator macro for minimal element, given 2 values X and Y, returns the lesser one (like Math.min in Java, for example)
#define min(X,Y) (((X) < (Y)) ? (X) : (Y))

pthread_mutex_t mutex_for_fs_stat;

//initialize file system - should be called as the first thing before accessing this file system 
int RSFS_init(){

   //initialize data blocks
   for(int i=0; i<NUM_DBLOCKS; i++){
     void *block = malloc(BLOCK_SIZE); //a data block is allocated from memory
     if(block==NULL){
       printf("[init] fails to init data_blocks\n");
       return -1;
     }
     data_blocks[i] = block;  
   } 

   //initialize bitmaps
   for(int i=0; i<NUM_DBLOCKS; i++) data_bitmap[i]=0;
   pthread_mutex_init(&data_bitmap_mutex,NULL);
   for(int i=0; i<NUM_INODES; i++) inode_bitmap[i]=0;
   pthread_mutex_init(&inode_bitmap_mutex,NULL);    

   //initialize inodes
   for(int i=0; i<NUM_INODES; i++){
       inodes[i].length=0;
       for(int j=0; j<NUM_POINTER; j++) 
           inodes[i].block[j]=-1; //pointer value -1 means the pointer is not used
       inodes[i].num_current_reader=0;
       pthread_mutex_init(&inodes[i].rw_mutex,NULL);
       pthread_mutex_init(&inodes[i].read_mutex,NULL);
   }
   pthread_mutex_init(&inodes_mutex,NULL); 

   //initialize open file table
   for(int i=0; i<NUM_OPEN_FILE; i++){
       struct open_file_entry entry=open_file_table[i];
       entry.used=0; //each entry is not used initially
       pthread_mutex_init(&entry.entry_mutex,NULL);
       entry.position=0;
       entry.access_flag=-1;
   }
   pthread_mutex_init(&open_file_table_mutex,NULL); 

   //initialize root directory
   root_dir.head = root_dir.tail = NULL;

   //initialize mutex_for_fs_stat
   pthread_mutex_init(&mutex_for_fs_stat,NULL);

   //return 0 means success
   return 0;
}

//create file
//if file does not exist, create the file and return 0;
//if file_name already exists, return -1; 
//otherwise, return -2.
int RSFS_create(char *file_name){

   //search root_dir for dir_entry matching provided file_name
   struct dir_entry *dir_entry = search_dir(file_name);

   if(dir_entry){//already exists
       printf("[create] file (%s) already exists.\n", file_name);
       return -1;
   }else{

       if(DEBUG) printf("[create] file (%s) does not exist.\n", file_name);

       //construct and insert a new dir_entry with given file_name
       dir_entry = insert_dir(file_name);
       if(DEBUG) printf("[create] insert a dir_entry with file_name:%s.\n", dir_entry->name);
       
       //access inode-bitmap to get a free inode 
       int inode_number = allocate_inode();
       if(inode_number<0){
           printf("[create] fail to allocate an inode.\n");
           return -2;
       } 
       if(DEBUG) printf("[create] allocate inode with number:%d.\n", inode_number);

       //save inode-number to dir-entry
       dir_entry->inode_number = inode_number;
       
       return 0;
   }
}

//open a file with RSFS_RDONLY or RSFS_RDWR flags
int RSFS_open(char *file_name, int access_flag){
    //sanity test: access_flag should be either RSFS_RDONLY or RSFS_RDWR
    if (access_flag != RSFS_RDONLY && access_flag != RSFS_RDWR){
        return -1;
    }
    
    //find dir_entry matching file_name
    struct dir_entry * dir_entry = search_dir(file_name);
    
    //base on the requested access_flag and the current "open" status of this file to block the caller if needed
    //find an unused open-file-entry in open-file-table and fill the fields of the entry properly
    //return the index of the open-file-entry in open-file-table as file descriptor
    //this method does all of the above!
    return allocate_open_file_entry(access_flag, dir_entry);
}

//append the content in buf to the end of the file of descriptor fd
int RSFS_append(int fd, void *buf, int size){
   //check the sanity of the arguments: fd should be in [0,NUM_OPEN_FILE] and size>0.
    if (fd < 0 || fd >= NUM_OPEN_FILE || size <= 0){
        return -1;
    }
    //get the open file entry corresponding to fd
    struct open_file_entry *open_file_entry = &open_file_table[fd];

    //now, lock this file entry so no other thread can read/write to it
    pthread_mutex_lock(&open_file_entry->entry_mutex);

    //check if the file is opened with RSFS_RDWR mode; otherwise return -1
    if (open_file_entry->access_flag != RSFS_RDWR){
        //release lock, as we're throwing an error and the file entry will no longer be written to
        pthread_mutex_unlock(&open_file_entry->entry_mutex);
        return -1;
    }

    //get the current position
    int pos = open_file_entry->position;

    //gets the corresponding inode via the corresponding directory entry via the open file entry
    struct inode * this_node = &inodes[open_file_entry->dir_entry->inode_number];
  
    //append the content in buf to the data blocks of the file from the end of the file; 
    //allocate new block(s) when needed - (refer to lecture L22 on how)
    int bytes_appended = 0;
    int remaining_size = size;
    //calculate the block number where appending starts
    int block_number = (pos + BLOCK_SIZE - 1) / BLOCK_SIZE;
    // Calculate the offset within the block
    int offset = pos % BLOCK_SIZE;

    //while there is still room available to append, append!
    while (remaining_size > 0){
        //allocate a new data block if needed
        if (offset == 0){
            //allocate a data block for this daa to be written
            this_node->block[block_number] = allocate_data_block();
            if (this_node->block[block_number] == -1){
                //can't allocate a new data block
                return bytes_appended; 
            }
        }

        //calculate the number of bytes to append to the current block
        int bytes_to_append = min(remaining_size, BLOCK_SIZE - offset);
        //copy data from buf to the current block
        memcpy(data_blocks[this_node->block[block_number]] + offset, buf + bytes_appended, bytes_to_append);

        //update variables
        bytes_appended += bytes_to_append;
        remaining_size -= bytes_to_append;
        //update the current position in open file entry
        pos += bytes_to_append;
        offset = 0; 
        block_number++; 
    }
    //return the number of bytes appended to the file
    return bytes_appended;
}


//update current position of the file (which is in the open_file_entry) to offset
int RSFS_fseek(int fd, int offset){
   //sanity test of fd    
    if (fd < 0 || fd >= NUM_OPEN_FILE || !open_file_table[fd].used){
        return -1;
    }
    //get the correspondng open file entry
    struct open_file_entry *open_file_entry = &open_file_table[fd];

    //get the current position
    int pos = open_file_entry->position;

    //gets the corresponding inode via the corresponding directory entry via the open file entry
    //get the file length
    struct inode * this_node = &inodes[open_file_entry->dir_entry->inode_number];
    int file_length = this_node->length;

    //check if argument offset is not within 0...length, do not proceed and return current position
    if (offset < 0 || offset > file_length){
        //offset is out of range, return current pos
        return pos; 
    }

    //update the current position to offset, and return the new current position
    open_file_entry->position = offset;

    //return the current pos
    return open_file_entry->position;
}

//read from file from the current position for up to size bytes
int RSFS_read(int fd, void *buf, int size){
    //sanity test of fd and size
    if (fd < 0 || fd >= NUM_OPEN_FILE || !open_file_table[fd].used || size < 0){
        return -1;
    }

    //get the corresponding open file entry
    struct open_file_entry *open_file_entry = &open_file_table[fd];
    //get the current position
    int pos = open_file_entry->position;

    //lock the open file entry
    pthread_mutex_lock(&open_file_entry->entry_mutex);
    
    //get the dir entry
    //get the inode
    struct inode *this_node = &inodes[open_file_entry->dir_entry->inode_number];    
    
    //read the data in from current pos to size bytes, and update current position
    for (int i = 0; i < min(size, this_node->length - open_file_entry->position); i++){
        int block = (i + open_file_entry->position) / BLOCK_SIZE;
        int offset = (i + open_file_entry->position) % BLOCK_SIZE;
        memcpy(buf + i, data_blocks[this_node->block[block]] + offset, 1);
    }
    int bytes_read = min(size, this_node->length - open_file_entry->position);
    open_file_entry->position += bytes_read;
    
    //unlock the open file entry
    pthread_mutex_unlock(&open_file_entry->entry_mutex);

    //return the cactual number of bytes read
    return bytes_read;
}

//close file: return 0 if succeed, or -1 if fd is invalid
int RSFS_close(int fd){
    //sanity test of fd
    if (fd < 0 || fd >= NUM_OPEN_FILE || !open_file_table[fd].used){
        return -1;
    }

    //free the open file entry
    free_open_file_entry(fd);
    //clos successful, return 0
    return 0;
}

//delete file with provided file name: return 0 if succeed, or -1 in case of error
int RSFS_delete(char *file_name){
    //find the dir_entry
    struct dir_entry * dir_entry = search_dir(file_name);
    if (dir_entry == NULL){
        //dir_entry doesn't exist, so return -1 to show that
        return -1;
    }

    //lock the entire filesystem, as we'll need to delete an inode, and that change needs to propogate up the entire file system tree
    pthread_mutex_lock(&root_dir.mutex);
    
    //check if the file given is already open, if it is, return -1 as we can't delete as it's in use
    //had to add this in as the concurrent test cases fail without a check in delete like this
    pthread_mutex_lock(&open_file_table_mutex);
    for (int i = 0; i < NUM_OPEN_FILE; i++){
        if (open_file_table[i].used){
            pthread_mutex_lock(&open_file_table[i].entry_mutex);
            if (open_file_table[i].dir_entry->inode_number == dir_entry->inode_number){
                //file given is open, so return release the mutex on the file system and return -1
                pthread_mutex_unlock(&open_file_table[i].entry_mutex);
                return -1;
            }
            pthread_mutex_unlock(&open_file_table[i].entry_mutex);
        }
    }
    pthread_mutex_unlock(&open_file_table_mutex);

    //find the corresponding inode
    struct inode * this_node = &inodes[dir_entry->inode_number];

    //free the inode in inode-bitmap
    pthread_mutex_lock(&data_bitmap_mutex);
    for (int i = 0; i < NUM_POINTER; i++){
        if (this_node->block[i] >= 0 && data_bitmap[this_node->block[i]]){
            free(data_blocks[this_node->block[i]]);
            data_bitmap[this_node->block[i]]=0;
            this_node->block[i] = -1;
        }
    }
    this_node->length = 0;
    //now, release all mutex locks and free the inode in inode-bitmap
    pthread_mutex_unlock(&data_bitmap_mutex);
    free_inode(dir_entry->inode_number);
    pthread_mutex_unlock(&root_dir.mutex);

    //free the dir entry, as there is no longer anything below/in it
    delete_dir(file_name);
    return 0;
}

//print status of the file system
void RSFS_stat(){

   pthread_mutex_lock(&mutex_for_fs_stat);

   printf("\nCurrent status of the file system:\n\n %16s%10s%10s\n", "File Name", "Length", "iNode #");

   //list files
   struct dir_entry *dir_entry = root_dir.head;
   while(dir_entry!=NULL){

       int inode_number = dir_entry->inode_number;
       struct inode *inode = &inodes[inode_number];
       
       printf("%16s%10d%10d\n", dir_entry->name, inode->length, inode_number);
       dir_entry = dir_entry->next;
   }
   
   //data blocks
   int db_used=0;
   for(int i=0; i<NUM_DBLOCKS; i++) db_used+=data_bitmap[i];
   printf("\nTotal Data Blocks: %4d,  Used: %d,  Unused: %d\n", NUM_DBLOCKS, db_used, NUM_DBLOCKS-db_used);

   //inodes
   int inodes_used=0;
   for(int i=0; i<NUM_INODES; i++) inodes_used+=inode_bitmap[i];
   printf("Total iNode Blocks: %3d,  Used: %d,  Unused: %d\n", NUM_INODES, inodes_used, NUM_INODES-inodes_used);

   //open files
   int of_num=0;
   for(int i=0; i<NUM_OPEN_FILE; i++) of_num+=open_file_table[i].used;
   printf("Total Opened Files: %3d\n\n", of_num);

   pthread_mutex_unlock(&mutex_for_fs_stat);
}

//write the content of size (bytes) in buf to the file (of descripter fd) from current position for up to size bytes 
int RSFS_write(int fd, void *buf, int size){
    //sanity test of fd and size
    if (fd < 0 || fd >= NUM_OPEN_FILE || !open_file_table[fd].used || size < 0){
        return -1;
    }

    //get the open file entry
    struct open_file_entry *open_file_entry = &open_file_table[fd];

    //lock the open file entry
    pthread_mutex_lock(&open_file_entry->entry_mutex);

    //get the dir entry
    struct dir_entry *dir_entry = open_file_entry->dir_entry;
    //get the inode
    struct inode *this_node = &inodes[dir_entry->inode_number];

    //initialize variables for conditional checks while writing
    int bytes_written = 0;
    int remaining_size = size;
    int buffer_offset = 0;

    //similarly to append, calculate which block the writing begins and at what offset
    int start_block = (open_file_entry->position) / BLOCK_SIZE;
    int start_offset = (open_file_entry->position) % BLOCK_SIZE;

    //similar to append, but here we write starting at the given position
    //(from append): allocate new block(s) when needed - (refer to lecture L22 on how)
    for (int i = start_block; i < NUM_POINTER && remaining_size; i++){
        if (this_node->block[i] < 0){
            //allocate a data block for this daa to be written
            int block_num = allocate_data_block();
            if (block_num == -1){
                //if no block was made, we've reached the end of where we can write, so break the loop
                break;
            }
            this_node->block[i] = block_num;
        }
        //now, given the amount of blocks to write, write to the file
        int bytes_to_write = min(remaining_size, BLOCK_SIZE - start_offset);
        memcpy(data_blocks[this_node->block[i]] + start_offset, buf + buffer_offset, bytes_to_write);

        //similar to append, update the current position in open file entry
        bytes_written += bytes_to_write;
        remaining_size -= bytes_to_write;
        buffer_offset += bytes_to_write;
        open_file_entry->position += bytes_to_write;
        start_offset = 0;
    }

    //if we overwrote, fix the position so to avoid memory overload errors ("segmentation fault" i believe)
    if(open_file_entry->position > this_node->length){
        this_node->length = open_file_entry->position;
    }

    //unlock our mutex lock on our open file entry, as writing has completed
    pthread_mutex_unlock(&open_file_entry->entry_mutex);

    //return the number of bytes written
    return bytes_written;
}

//cut the content from the current position for up to size (bytes) from the file of descriptor fd
int RSFS_cut(int fd, int size){
    //sanity check of fd and size
    if (fd < 0 || fd >= NUM_OPEN_FILE || !open_file_table[fd].used || size <= 0){
        return -1;
    }

    //get the open file entry for the given fd
    struct open_file_entry *open_file_entry = &open_file_table[fd];
    //lock the open file entry, as we'll be modifying its child
    pthread_mutex_lock(&open_file_entry->entry_mutex);

    //get the current position within the file
    int pos = open_file_entry->position;

    //get the corresponding inode via the corresponding directory entry via the open file entry
    struct inode * this_node = &inodes[open_file_entry->dir_entry->inode_number];
    //lock this inode so we can update the position as needed (aka cut the content specified)
    pthread_mutex_lock(&this_node->rw_mutex);

    //calculate the new length of the file after cut is called with our given params
    int new_pos = pos + size;

    //adjust the data blocks & update the file's length
    this_node->length = new_pos;

    //update the current position in the open file entry, if necessary (i.e. our position post-cut is not within the memory amount bounds for this file)
    if (open_file_entry->position > new_pos) {
        open_file_entry->position = new_pos;
    }

    //unlock the mutex locks on our inode we cut content out of, and the open file entry that our inode belongs to
    pthread_mutex_unlock(&this_node->rw_mutex);
    pthread_mutex_unlock(&open_file_entry->entry_mutex);

    //return the number of bytes cut from the file
    return size;
}