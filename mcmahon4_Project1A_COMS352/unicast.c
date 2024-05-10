#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

//define the format of a msg
#define MAX_NUM_RECEIVERS 10
#define MAX_MSG_SIZE 256

//used to store parent's message and which child should receive it
struct msg_t {
   int flag;
   int receiverId;
   char content[MAX_MSG_SIZE];
};

//error handling (taken from broadcast.c)
void panic(char *s) {
   fprintf(2, "%s\n", s);
   exit(1);
}

//creates a proccess (also from broadcast.c)
int fork1(void) {
   int pid;
   pid = fork();
   if(pid == -1)
       panic("fork");
   return pid;
}

//creates a pipe (also from broadcast.c)
void pipe1(int fd[2]) {
   int rc = pipe(fd);
   if(rc<0){
       panic("Fail to create a pipe.");
   }
}

//runner code
int main(int argc, char *argv[]) {
   if(argc < 3) {
       panic("Usage: broadcast <num_of_receivers> <msg_to_broadcast>");
   }
   int numReceiver = atoi(argv[1]);
   int comparableId = atoi(argv[2]);

   //create a pair of pipes for parent <--> child communication
   int channelToReceivers[2], channelFromReceivers[2];
   pipe1(channelToReceivers);
   pipe1(channelFromReceivers);

   for(int i = 0; i < numReceiver; i++) {
    
       // Create child process as receiver
       int retFork = fork1();
       if(retFork == 0) {

           /*following is the code for child process i*/
           int myId = i;
           printf("Child %d: start!\n", myId);
           struct msg_t msg;
           //read pipe to get the message from parent
           read(channelToReceivers[0], (void *)&msg, sizeof(struct msg_t));          
           printf("Child %d: get msg (%s) to Child %d \n", myId, msg.content, msg.receiverId);

           //check if the message is for this child
           if (comparableId == myId) {
               //send acknowledgment to parent, and set flag to represent the message's updated status
               msg.flag = 0;
               printf("Child %d: the msg is for me.\n", myId);
               printf("Child %d acknowledges to Parent: received!\n", myId);
               write(channelFromReceivers[1], "received!", 9);
              
           } else {
               //if it's not intended for the current child, send the message back to the pipe, and keep flag as = 0 (not received by intended child yet)
               msg.flag = 1;
               printf("Child %d: the msg is not for me.\n", myId);
               printf("Child %d: write the msg back to pipe.\n", myId);
               write(channelToReceivers[1], &msg, sizeof(msg));
           }

           //end of the child process
           exit(0);
       } else {
           printf("Parent: creates child process with id: %d\n", i);
       }
       sleep(1);
   }

   /*following is the parent's code*/

   //to broadcast message
   struct msg_t msg;
   msg.flag = 1;
   msg.receiverId = atoi(argv[2]);
   strcpy(msg.content, argv[3]);
   write(channelToReceivers[1], &msg, sizeof(struct msg_t));
   printf("Parent sends to Child %d: %s\n", msg.receiverId, msg.content); 

   //to receive acknowledgment
   char recvBuf[sizeof(struct msg_t)];        
   read(channelFromReceivers[0], &recvBuf, sizeof(struct msg_t));
   printf("Parent receives: %s\n", recvBuf);

   //end of parent process
   exit(0);
}