# meta_stats_server

This project involves implementing a meta statistics server that provides two functions:

  1. The server communicates with remote clients via TCP/IP sockets and maintains simple statistics on the values sent by clients. A default port is provided, but alternative ports can be entered via command line.
  
  2. The server also listens on a secondary port which can also be altered via command line. When a client connects to the secondary port the server sends back a single line containing three quantities: the number of means sent by the server in response to exit commands issued by clients on the primary port, the mean of those means, and the standard deviation of those means.

Primary Port Communication:

Communication between client and server occurs by exchanging newline terminated lines of text. Server ignores leading spaces.

Server enters an infinite loop and has the capability to handle multiple clients simultaneously by forking child processes to handle communication with clients. Dead child processes are reaped periodically. 

In each line, the client can either send a single floating point value or one of several one-word commands:
  1. count - returns the number of floating point values received
  2. sum - sends back the sum of the floating points
  3. mean - sends back the mean of the floating points
  4. stddev - sends back the standard deviation of the floating points
  5. exit - send back a line with the final count, mean and standard deviation (after processing an exit command, the child process forked by the server closes the socket and terminates execution)

If leading spaces are not following by a valid floating point number the server sends the client an error message.

Secondary Port Communication: 

The secondary aspect of the server involves using pipes to send data from child processes and select to determine which file descriptors are ready.
The three file descriptors of interest are the main socket, the secondary socket and the read end of the pipe. When a connection is detected on the main port, the program executes the instructions for the primary port connection. When an exit command is processed, the mean data is sent through the pipe where it is processed and used to update global variables holding information on the connections. 

When a connection is accepted on the secondary port, the server sends information about past connections on the primary port. 
 
