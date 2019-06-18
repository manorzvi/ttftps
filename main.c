//
// Created by Manor on 6/16/2019.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // for close
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define WAIT_FOR_PACKET_TIMEOUT 3
#define NUMBER_OF_FAILURES 7
#define MAX_CLIENTS 1
#define WRQ_MAX 516
#define WRQ_MAX_DATA 514 /* WRQ = | opcode | filename | octat   |
                          *       | 2B     |   (*)    | 514-(*) | */
#define MAX_DATA 512
#define ACK_OPCODE 4
#define DATA_OPCODE 3
#define WRQ_OPCODE 2

void error(char *msg) {
    perror(msg);
    exit(1);
}

typedef struct WRQ_ {
    unsigned short opcode;
    char buffer[WRQ_MAX_DATA];
}__attribute__((packed)) WRQ;

typedef struct ACK_ {
    unsigned short opcode;
    unsigned short block_num;
}__attribute__((packed)) ACK;

typedef struct DATA_ {
    unsigned short opcode;
    unsigned short block_num;
    char data_in[MAX_DATA];
}__attribute__((packed)) DATA;

void unpack_wrq(WRQ wrq, unsigned short* opcode, char* filename, char* mode){
    int i=0, j=0;
    *(opcode) = ntohs(wrq.opcode);
    while(wrq.buffer[i]!='\0'){
        filename[i]=wrq.buffer[i];
        i++;
    }
    filename[i++]='\0';
    while(wrq.buffer[i]!='\0'){
        mode[j++]=wrq.buffer[i++];
    }
    mode[j]='\0';
}

int main(int argc, char *argv[]) {
    int sock;                        /* Socket                     */
    struct sockaddr_in echoServAddr; /* Local address              */
    struct sockaddr_in echoClntAddr; /* Client address             */
    unsigned int cliAddrLen;         /* Length of incoming message */
    unsigned short echoServPort;     /* Server port                */
    int WrqMsgSize;                 /* Size of received message   */
    int DataMsgSize;
    int LastWriteSize;
    WRQ wrq;
    ACK ack;
    unsigned short wrq_opcode;
    char filename[WRQ_MAX_DATA];
    char mode[WRQ_MAX_DATA];
    int select_result;
    unsigned short last_block_num = 0;
    int InnerStopIteration = 1;
    int OuterStopIteration = 1;
    int timeoutExpiredCount = 0;

    if(argc<2)
        error("TTFTP_ERROR: Not enough arguments");
    echoServPort=(unsigned short)atoi(argv[1]);

    /* Create socket for sending/receiving datagrams */
    if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
        error("TTFTP_ERROR: socket() failed");

    /* Construct local address structure */
    /* Zero out structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));
    /* Internet address family */
    echoServAddr.sin_family = AF_INET;
    /* Any incoming interface */
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    /* Local port */
    echoServAddr.sin_port = htons(echoServPort);
    /* Bind to the local address */
    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        error("TTFTP_ERROR: bind() failed");

    for (;;) { /* Run forever */
        /* Set the size of the in-out parameter */
	    last_block_num = 0;
        cliAddrLen = sizeof(echoClntAddr);
        /* Block until receive message from a client */
        if ((WrqMsgSize = recvfrom(sock, &wrq, sizeof(wrq), 0, (struct sockaddr *) &echoClntAddr, &cliAddrLen)) < 0) {
            close(sock);
            error("TTFTP_ERROR: recvfrom() failed");
        }

        unpack_wrq(wrq, &wrq_opcode, filename, mode); /* unpack filename, opcode & mode from wrq */

        if (wrq_opcode != (unsigned short)WRQ_OPCODE) {
//            error("TTFTP_ERROR: not an WRQ recieved");
            printf("[DEBUG] TTFTP_ERROR: not an WRQ recieved. opcode=%d, filename=%s, mode=%s\n",wrq_opcode, filename, mode);
        }
        printf("IN: WRQ, %s, %s\n", filename, mode);

        /* Send received datagram back to the client */
        ack.opcode    = htons(ACK_OPCODE);
        ack.block_num = htons(last_block_num);
        if (sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr)) < 0) {
            close(sock);
            error("TTFTP_ERROR: sendto() failed");
        }
        printf("OUT: ACK, %d\n", ack.block_num);

        FILE* fd=fopen(filename,"w");
        if (fd==NULL) {
            close(sock);
            error("TTFTP_ERROR: Can't open file");
        }

        struct timeval timeout;
        timeout.tv_sec=WAIT_FOR_PACKET_TIMEOUT;
        timeout.tv_usec=0;
        fd_set rfds;
        DATA data;

        do {
            OuterStopIteration = 1;
            do {
                do { /* Wait WAIT_FOR_PACKET_TIMEOUT to see if something
                      * appears for us at the socket (we are waiting for DATA) */
                    InnerStopIteration = 1;
                    FD_ZERO(&rfds);
                    FD_SET(sock, &rfds);
                    select_result = select(sock+1, &rfds, NULL, NULL, &timeout);
                    if ( select_result < 0) {
                        close(sock);
                        if(fclose(fd)!=0)
                            error("TTFTP_ERROR: Can't close file");

                        error("TTFTP_ERROR: select() failed");
                    }
                    if (select_result > 0) { /* if there was something at the socket
                                              * and we are here not because of a timeout */
                        /* Read the DATA packet from the socket (at least we hope this is a DATA packet) */
                        if ((DataMsgSize = recvfrom(sock, &data, sizeof(data), 0, (struct sockaddr *) &echoClntAddr, &cliAddrLen)) < 0) {
                            close(sock);
                            if(fclose(fd)!=0){
                                error("TTFTP_ERROR: Can't close file");
                            }
                            error("TTFTP_ERROR:recvfrom() failed");
                        }
                        printf("IN: DATA, %d, %d\n", ntohs(data.block_num), DataMsgSize);
                        InnerStopIteration = 0;
                    }
                    if (select_result == 0) { /* Time out expired while waiting for data to appear at the socket */
                        /* Send another ACK for the last packet */
                        timeoutExpiredCount++;
                        ack.opcode    = htons(ACK_OPCODE);
                        ack.block_num = htons(last_block_num);
                        if (sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr)) < 0) {
                            close(sock);
                            if(fclose(fd)!=0){
                                error("TTFTP_ERROR: Can't close file");
                            }
                            error("TTFTP_ERROR: sendto() failed");
                        }
                        printf("OUT:ACK, %d\n", ack.block_num);
                    }
                    if (timeoutExpiredCount>= NUMBER_OF_FAILURES) {
                        /* FATAL ERROR BAIL OUT */
                        error("FLOWERROR: too many timeouts\n");
                    }
                } while (InnerStopIteration); /* Continue while some socket was ready but recvfrom somehow failed to read the data */

                if (ntohs(data.opcode) != (unsigned short)DATA_OPCODE) { /* We got something else but DATA */
                    /* FATAL ERROR BAIL OUT */
                    close(sock);
                    if(fclose(fd)!=0){
                        error("TTFTP_ERROR: Can't close file\n");
                    }
//                    error("FLOWERROR: Packet is not DATA.\n");
                    printf("FLOWERROR: Packet is not DATA. pkt.opcode=%d != %d\n", ntohs(data.opcode), (unsigned short)DATA_OPCODE);
                    exit(1);
                }
                if (ntohs(data.block_num) != (unsigned short)(last_block_num+1)) { /* The incoming block number is not what we have expected,
                                                                                    * i.e. this is a DATA pkt but the block number
                                                                                    * in DATA was wrong (not last ACK’s block number + 1) */
                    /* FATAL ERROR BAIL OUT */
                    close(sock);
                    if(fclose(fd)!=0){
                        error("TTFTP_ERROR: Can't close file\n");
                    }
//                    error("FLOWERROR: DATA Packets Ordering Rules Violation\n");
                    printf("FLOWERROR: Packet out of order. pkt.block_num=%d != (last_blocknum+1)=%d\n",
                           ntohs(data.block_num), (unsigned short)(last_block_num+1));
                    exit(1);
                }
            } while (0); /* TODO: Ask Shai: why this while needed? it doesn't do anything */
            timeoutExpiredCount = 0;
            //InnerStopIteration  = 1;
            last_block_num ++;
            LastWriteSize = fwrite(data.data_in, sizeof(char), DataMsgSize-4, fd); /* write next bulk of data */
            printf("WRITING: %d\n", LastWriteSize);
            if (LastWriteSize < MAX_DATA)
                OuterStopIteration = 0;
            /* send ACK packet to the client */
            ack.opcode    = htons(ACK_OPCODE);
            ack.block_num = htons(last_block_num);
            if (sendto(sock, &ack, sizeof(ack), 0, (struct sockaddr *) &echoClntAddr, sizeof(echoClntAddr)) < 0) {
                close(sock);
                if(fclose(fd)!=0){
                    error("TTFTP_ERROR: Can't close file");
                }
                error("TTFTP_ERROR: sendto() failed");
            }
            printf("OUT:ACK,%d\n", last_block_num);
        } while (OuterStopIteration == 1); /* Have blocks left to be read from client (not end of transmission) */
        if(fclose(fd) != 0) {
            close(sock);
            error("TTFTP_ERROR: Can't close file\n");
        }
        printf("RECVOK\n");
    }
    /* NOT REACHED */
}

