#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <math.h>
#include <float.h>
#include <assert.h>

#define DEFAULT_PORT        "3100"
#define DEFAULT_META_PORT   "3101"
#define BACKLOG 10
#define BUF_LEN 80
#define MAX_REPLY_LEN 1000

#define handle_error(msg) \
           do { perror(msg); exit(-1); } while(0)


#define MAX2(a,b) ((a)>(b) ? (a) : (b))
#define MAX3(a,b,c) MAX2(MAX2((a), (b)), (c)) // to determine the largest file descriptor

// to remove leading spaces
char* removeLeadingSpaces(char* strorg, char* strnew)
{
    int count = 0, j, k;

    while (strorg[count] == ' ') {
        count++;
    }

    for (j = count, k = 0; strorg[j] != '\0'; j++, k++) {
        strnew[k] = strorg[j];
    }

    strnew[k] = '\0';

    return strnew;
}

// mean function
double compute_mean(double sum, int count)
{
   return (double)(sum / count);
}

// standard deviation function
double compute_stddev(double sum, double square_sum, int count)
{
   double countbuf = (double) count;
   double x = (1/(countbuf-1));
   double stddev = sqrt(x * (square_sum - ((sum * sum)/count)));
   return stddev;
}

int main(int argc, char* argv[])
{
   // if no command line arguments are passed, use the default ports
   char* port = DEFAULT_PORT;
   char* meta_port = DEFAULT_META_PORT;
   if (argc>1)
      port = argv[1];
   if (argc>2)
      meta_port = argv[2];

   // Acquire socket information
   /*---------------------------------------------------------*/
   struct addrinfo hints;
   memset(&hints, 0, sizeof hints);
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;

   struct addrinfo *servinfo;
   int res = getaddrinfo(NULL, port, &hints, &servinfo);
   if (res != 0)
      handle_error("server: getaddrinfo error");
   
   int listen_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
   if (listen_fd == -1)
      handle_error("server: socket error");
   
   int yes = 1;
   res = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
   if (res == -1)
      handle_error("server: setsockopt error");
   
   res = bind(listen_fd, servinfo->ai_addr, servinfo->ai_addrlen);
   if (res == -1)
      handle_error("server: bind error");
   
   freeaddrinfo(servinfo);
   
   res = getaddrinfo(NULL, meta_port, &hints, &servinfo);
   if (res != 0)
      handle_error("metaserver: getaddrinfo error");

   int meta_listen_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
   if (meta_listen_fd == -1)
      handle_error("metaserver: socket error");
    
   res = setsockopt(meta_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
   if(res == -1)
      handle_error("metaserver: setsockopt error");

   // bind socket to server address
   res = bind(meta_listen_fd, servinfo->ai_addr, servinfo->ai_addrlen);
   if(res == -1)
      handle_error("metaserver: bind error");
    
   freeaddrinfo(servinfo);

   res = listen(listen_fd, BACKLOG);
   if (res == -1)
      handle_error("server: listen error");
    
   res = listen(meta_listen_fd, BACKLOG);
   if(res == -1)
      handle_error("metaserver: listen error");

    /*-------------------------------------------------------------*/
   //Here ends most of the stuff we need to do for handling setting up the port connections

   // pipe for collecting means from child processes
   // pipe is declared in the main function
   int pipe_fd[2];
   if (pipe(pipe_fd) != 0)
        handle_error("metaserver: piping error");
   
   // initialize a file descriptor set for the select call
   fd_set master;
   FD_ZERO(&master);

   // we want to listen to three file descriptors to determine which one is ready: 
   // the main server port connection, the meta server port and the read end of the pipe
   FD_SET(listen_fd, &master);
   FD_SET(meta_listen_fd, &master);
   FD_SET(pipe_fd[0], &master);
           
   // call the macro MAX3 to determine the largest file descriptor among the three 
   // the value fdmax will be needed for the select call 
   int fdmax = MAX3(listen_fd, meta_listen_fd, pipe_fd[0]);
   
   // declare the count of means, sum of means and squared sum of means outside of the main server loop 
   int means_count = 0;
   double means_sum = 0.0;
   double means_sumsq = 0.0;
   
   // main server loop for processing 
   while(1)
   {
        
        // file descriptor set (contains the file descriptors we want to get info from) 
        // the select call with parameters 
        fd_set reading_fds = master;
        select(fdmax+1, &reading_fds, NULL, NULL, NULL);
        
        // the main server listening file descriptor has input 
        if (FD_ISSET(listen_fd, &reading_fds)) {
                   
            int comm_fd = accept(listen_fd, NULL, NULL);
            if (comm_fd == -1)
                handle_error("server: accept error");
            
            char welcomemsg[] = "Stats server ready\n";
            write(comm_fd, welcomemsg, strlen(welcomemsg));
                   
            fprintf(stderr, "Accepted new connection\n");
            
            // fork a child process to handle the new request 
            pid_t pid = fork();
            
            // the child process 
            if (pid == 0) {
                
                // close the listening file descriptor 
                close(listen_fd);
                
                // our running variables for this given connection
                int double_count = 0;
                double running_sum = 0;
                double sum_square = 0;

                while(1)
                {
                    char str[MAX_REPLY_LEN+1];

                    int bytes = read(comm_fd, str, sizeof(str));

                    char strbuffer[MAX_REPLY_LEN+1];

                    removeLeadingSpaces(str, strbuffer);

                    if (bytes > 80) {

                    char errormsg[] = "Error: long line\n";
                    write(comm_fd, errormsg, strlen(errormsg));
                    
                    } else if (strncmp(strbuffer, "count", 5) == 0) {

                    char countbuffer[MAX_REPLY_LEN];
                    sprintf(countbuffer, "%i", double_count);
                    write(comm_fd, countbuffer, strlen(countbuffer));
                    write(comm_fd, "\n", 1);

                    } else if (strncmp(strbuffer, "sum", 3) == 0) {

                    char sumbuffer[MAX_REPLY_LEN];
                    sprintf(sumbuffer, "%.2lf", running_sum);
                    write(comm_fd, sumbuffer, strlen(sumbuffer));
                    write(comm_fd, "\n", 1);

                    } else if (strncmp(strbuffer, "mean", 4) == 0) {    
                    
                    char meanbuffer[MAX_REPLY_LEN];
                    double mean = compute_mean(running_sum, double_count);
                    sprintf(meanbuffer, "%.2lf", mean);
                    write(comm_fd, meanbuffer, strlen(meanbuffer));
                    write(comm_fd, "\n", 1);

                    } else if (strncmp(strbuffer, "stddev", 6) == 0) {

                    char stddevbuffer[MAX_REPLY_LEN];
                    double stddev = compute_stddev(running_sum, sum_square, double_count);
                    sprintf(stddevbuffer, "%.2lf", stddev);
                    write(comm_fd, stddevbuffer, strlen(stddevbuffer));
                    write(comm_fd, "\n", 1);

                    } else if (strncmp(strbuffer, "exit", 4) == 0) {

                    char exitbuffer[MAX_REPLY_LEN];
                    int x = double_count;
                    double y = compute_mean(running_sum, double_count);
                    double z = compute_stddev(running_sum, sum_square, double_count);
                    sprintf(exitbuffer, "EXIT STATS: count %d mean %.2lf stddev %.2lf", x, y, z);
                    write(comm_fd, exitbuffer, strlen(exitbuffer));
                    write(comm_fd, "\n", 1);
                    
                    // calculate and send the mean of this particular connection through the pipe when we close the connection with exit 
                    double mean = running_sum/double_count;
                    int rv = write(pipe_fd[1], &mean, sizeof(mean));
                    assert(rv == sizeof(mean));

                    break;

                    } else if (atof(strbuffer) != 0) {
                    
                    // if a number is entered rather than a command, we update the variables for this connection 
                    double current_num = atof(strbuffer);
                    double_count++;
                    running_sum += current_num;
                    sum_square += (current_num * current_num);

                    } else {

                    char error_message[] = "Error: unrecognized command\n";
                    write(comm_fd, error_message, strlen(error_message));

                    }

                }
                
                close(comm_fd);
                exit(0);
            }
                   
            // the parent process 
            else {

                close(comm_fd);

                int status;
                pid_t deadChild;
                
                // reaping child processes 
                do {
                    deadChild = waitpid(-1, &status, WNOHANG);
                    if (deadChild == -1)
                    handle_error("server: waitpid");
                } while (deadChild > 0);
                
            }
       }
       
       // the reading end of the pipe has input 
       if (FD_ISSET(pipe_fd[0], &reading_fds)) {
           
           double mean;

           int rv = read(pipe_fd[0], &mean, sizeof(mean));
           assert(rv == sizeof(mean));
           
           // update the global variables with what was sent through the pipe from the terminated connection 
           means_count++;
           means_sum += mean;
           means_sumsq += mean*mean;
       }

       // the alternative connection file descriptor has input 
       if (FD_ISSET(meta_listen_fd, &reading_fds)) {

         int meta_comm_fd = accept(meta_listen_fd, NULL, NULL);
         if( meta_comm_fd == -1 )
            handle_error("metaserver: accept error");

         fprintf(stderr, "Accepted new meta-stats server connection\n");

         // send connection count, mean of means, and stdandard deviation of means; no need to fork a child
         // declare a char array of the maximum potential length 
         char reply[MAX_REPLY_LEN];
         
         // print our formatted output into the reply buffer array 
         // value calculation is done within this function call (parameters being the buffer we write to, the maximum length, and the formatted output)
         snprintf(reply, MAX_REPLY_LEN, "means_count %d mean_of_means %.2lf stddev_of_means %.2lf\n", means_count, means_sum/means_count, sqrt((means_sumsq - means_sum * means_sum / means_count) / (means_count - 1)));
         
         // send the contents of the buffer through the alternative connection 
         int rv = write(meta_comm_fd, reply, strlen(reply));
         assert(rv == strlen(reply));
         
         // close the connection at the end 
         close(meta_comm_fd);
       }
   }

   exit(0);

}
