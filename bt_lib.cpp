#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h> //internet address library
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <math.h>

#include <openssl/sha.h> //hashing pieces

#include <iostream>
#include <vector>
#include <fstream>

#include "bencode.h"
#include "bt_lib.h"
#include "bt_setup.h"

using namespace std;
//Use queuing algorithm
vector<bt_request_t> requestQue;
int requestCount;
vector<bool> seedBitfield, myBitfield;
bt_piece_t currPiece;

void calc_id(char * ip, unsigned short port, char *id) {

    char data[256];
    int len;

    //format print
    len = snprintf(data, 256, "%u", port);

    //id is just the SHA1 of the ip and port string
    SHA1((unsigned char *) data, len, (unsigned char *) id);
}

/**
 * init_peer(peer_t * peer, int id, char * ip, unsigned short port) -> int
 *
 *
 * initialize the peer_t structure peer with an id, ip address, and a
 * port. Further, it will set up the sockaddr such that a socket
 * connection can be more easily established.
 *
 * Return: 0 on success, negative values on failure. Will exit on bad
 * ip address.
 *
 **/
int init_peer(peer_t *peer, char * id, char* ip, unsigned short port) {
    struct hostent * hostinfo;
    //set the host id and port for referece
    memcpy(peer->id, id, ID_SIZE);
    peer->port = port;
    //get the host by name

    //zero out the sock address
    bzero(&(peer->sockaddr), sizeof (peer->sockaddr));
    //set the family to AF_INET, i.e., Iternet Addressing
    peer->sockaddr.sin_family = AF_INET;
    //copy the address to the right place
    peer->sockaddr.sin_addr = *((in_addr *) ip);
    //encode the port
    peer->sockaddr.sin_port = htons(port);

    //bt_msg_t - request setup
    requestCount = 0;
    currPiece.index = 0;
    currPiece.begin = 0;
    currPiece.block = '\0';
    return 0;
}

/**
 * print_peer(peer_t *peer) -> void
 *
 * print out debug info of a peer
 *
 **/
void print_peer(peer_t *peer) {
    int i;

    if (peer) {
        printf("peer: %s:%u ",
                inet_ntoa(peer->sockaddr.sin_addr),
                peer->port);
        printf("id: ");
        for (i = 0; i < ID_SIZE; i++) {
            printf("%02x", peer->id[i]);
        }
        printf("\n");
    }
}

void _be_dump1(be_node *node, ssize_t indent, bt_info_t *torrInfo) {
    size_t i;
    int k;

    switch (node->type) {
        case BE_STR:
            printf("str = %s (len = %lli)\n", node->val.s, be_str_len(node));
            break;

        case BE_INT:
            printf("int = %lli\n", node->val.i);
            break;

        case BE_LIST:
            puts("list [");

            for (i = 0; node->val.l[i]; ++i)
                _be_dump1(node->val.l[i], indent + 1, torrInfo);
            puts("]");
            break;

        case BE_DICT:
            puts("dict {");
            for (i = 0; node->val.d[i].val; ++i) {
                //_be_dump_indent(indent + 1);
                printf("%s => ", node->val.d[i].key);

                if (!strcmp("announce", node->val.d[i].key)) {
                    strcpy(torrInfo->announce, node->val.d[i].val->val.s);
                } else if (!strcmp("length", node->val.d[i].key)) {
                    torrInfo->length = node->val.d[i].val->val.i;
                } else if (!strcmp("name", node->val.d[i].key)) {
                    strcpy(torrInfo->name, node->val.d[i].val->val.s);
                } else if (!strcmp("piece length", node->val.d[i].key)) {
                    torrInfo->piece_length = node->val.d[i].val->val.i;
                } else if (!strcmp("pieces", node->val.d[i].key)) {
                    //Since each piece is a 20 byte SHA1 hash value.
                    printf("\n%lld\n", be_str_len(node->val.d[i].val));
                    torrInfo->num_pieces = be_str_len(node->val.d[i].val) / HashSize;
                    //Allocating memory to each SHA1 piece
                    torrInfo->piece_hashes = (char **) malloc(sizeof (*torrInfo->piece_hashes) * torrInfo->num_pieces);
                    for (k = 0; k < torrInfo->num_pieces; k++) {
                        (torrInfo->piece_hashes[k]) = (char *) malloc(HashSize);
                        bcopy(node->val.d[i].val->val.s, torrInfo->piece_hashes[k], HashSize);
                    }
                }
                _be_dump1(node->val.d[i].val, -(indent + 1), torrInfo);
            }
            puts("}");
            break;
    }
}

void be_dump1(be_node *node, bt_info_t *torrInfo) {
    _be_dump1(node, 0, torrInfo);
}

//Contact the tracker and update bt_args with info learned,
// such as peer list

int contact_tracker(bt_args_t * bt_args) {
    return 0;
}

//setup socket for data exchange

struct sockaddr_in socketSetup(int & serv_sock) {
    struct sockaddr_in serv_addr; //, client_addr;

    unsigned short port = 6667; //hardcoding: read from the torrent file
    //open the socket
    serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (serv_sock == -1) {
        printf("\n Cannot create socket!\n");
        exit(EXIT_FAILURE);
    }
    printf("clearing addr\n");
    memset(&serv_addr, 0, sizeof (serv_addr));
    //Set the struct sockaddr
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    //optionally bind() the sock
    printf("ready to bind\n");
    int x = (int) bind(serv_sock, (struct sockaddr*) &serv_addr, sizeof (serv_addr));
    if (x == -1) {
        printf("Failed to bind the socket!");
        close(serv_sock);
        exit(EXIT_FAILURE);
    }
    return serv_addr;
}

void sendInitialInterest(int socketFD) {
    bt_msg_t initialInterest;
    initialInterest.length  = 4;
    initialInterest.bt_type = BT_INTERSTED;
    initialInterest.payload.data[0] = NULL;
    send(socketFD, &initialInterest, sizeof (bt_msg), 0);
    cout << "Message Interest" << endl;
}

void sendBitfield(int socketFD){
    
    bt_msg_t bitFieldMessage;
    bitFieldMessage.bt_type = BT_BITFILED;
    bitFieldMessage.length = 1 + myBitfield.size();
    bitFieldMessage.payload.bitfield.size = myBitfield.size();
    bitFieldMessage.payload.bitfield.bitfield = (char *)&myBitfield;
    
    send_message(socketFD, bitFieldMessage);
}

void requestPiece(int socketFD){
   
    bt_msg_t requestMessage;
        //Fill up the request structure and message structure for requests
    bt_request_t sendRequest;
    sendRequest.index           = currPiece.index;
    sendRequest.begin           = currPiece.begin;
    sendRequest.length          = RequestSize; //Most of the clients implement -- 2^14 & advised
    requestQue[requestCount++]  = sendRequest;
   
    requestMessage.bt_type         = BT_REQUEST;
    requestMessage.length          = 13;
    requestMessage.payload.request = sendRequest;
   
    send_message(socketFD, requestMessage);
}

//Recvd Interested
void sendUnchoke(int socketFD){
    bt_msg_t unchokeMsg;
        //I can connect to this peer.
    unchokeMsg.length  = 4;
        // Send 'unchoked' msg.bt_type = 1
    unchokeMsg.bt_type = BT_UNCHOKE;
    unchokeMsg.payload.data[0] = NULL;
    send_message(socketFD, unchokeMsg);
}

bt_msg_t createPieceToSend(bt_args_t bt_args, int index){

    string line;
    long long fileLength = bt_args.bt_info->length;
    long long numPieces  = bt_args.bt_info->num_pieces;
    int pieceLength      = ceil(fileLength/numPieces);
    currPiece.index = index;

    string seedF = bt_args.seedFile;
    const char* seedFile = seedF.c_str();
    ifstream myFile;
    myFile.open(seedFile);
    
    bt_msg_t pieceMessage;
    pieceMessage.bt_type = BT_PIECE;
    
    char *block;
    
    if (pieceLength > RequestSize){
        int remBytes = pieceLength - currPiece.begin;
        if(remBytes < RequestSize){
            block = (char*)malloc(remBytes);
            myFile.read(block, remBytes);
            myBitfield[index] = true;
            pieceMessage.length  = 9 + remBytes;
            pieceMessage.payload.piece.block = block;
        } else {
            block = (char*)malloc(RequestSize);
            currPiece.begin += RequestSize;
            myFile.read(block, RequestSize);
            pieceMessage.length = 9 + RequestSize;
        }
    } else {
        currPiece.begin = 0;
            //file read and copy block of size piecelength
        block = (char*)malloc(pieceLength);
        myFile.read(block, pieceLength);
        myBitfield[index] = true;
        pieceMessage.length = 9 + RequestSize;
    }
    pieceMessage.payload.piece.block = block;
    return pieceMessage;
}

int getBlockIndex(){
    int blockIndex = floor(currPiece.begin/RequestSize);
    return blockIndex;
}

void updateCurrentPiece(int offset, int index, char *block){
    currPiece.begin = offset;
    currPiece.index = index;
    currPiece.block = block;
}

void receivedPiece(bt_args_t bt_args, int blockLength, char *block, int index){
    
    long long fileLength = bt_args.bt_info->length;
    long long numPieces  = bt_args.bt_info->num_pieces;
    int pieceLength      = ceil(fileLength/numPieces);
    char pieceIndex[10], blockIndex[10];
    currPiece.index = index;
    ofstream outfile;
    
    string saveF = bt_args.seedFile;
    const char* saveFile = saveF.c_str();
    char pieceFileName[FILE_NAME_MAX];
    strcpy(pieceFileName, bt_args.save_file);
    
    strcat(pieceFileName, "_piece");
    sprintf(pieceIndex, "%d", currPiece.index);
    strcat(pieceFileName, pieceIndex);
    sprintf(blockIndex, "%d", getBlockIndex());
    strcat(pieceFileName, blockIndex);
        //FileOp
    outfile.open(pieceFileName);
    outfile.write(block, blockLength);
    
    if(currPiece.begin > bt_args.bt_info->piece_length){
        myBitfield[index] = 1;
    }
}

void init_bitfield(bt_args_t * bt_args, bool init_val) {
    long num_pieces = bt_args->bt_info->num_pieces;
    bt_args->bt_info->bit_field.resize(num_pieces, 0);
    long unsigned i;
    bt_args->bt_info->bit_field.assign(num_pieces, init_val);
}

void set_bit_field(bt_args_t * bt_args, long position, bool val) {
    bt_args->bt_info->bit_field[position] = val;
}

void recieve_message(int sock, bt_msg_t * bt_msg) {
    memset(bt_msg, 0, sizeof (bt_msg_t));

    recv(sock, &(bt_msg->length), sizeof (int), 0);
    int length = bt_msg->length;
    char * rcv_message = (char*) malloc(length);
    if (bt_msg->length > 0) {
        recv(sock, rcv_message, bt_msg->length, 0);
        bt_msg->bt_type = *((unsigned int *) rcv_message);
        rcv_message += sizeof (unsigned int);
        length -= sizeof (unsigned int);
        switch (bt_msg->bt_type) {
            case BT_CHOKE:
                break;
            case BT_UNCHOKE:
                //unchoke
                break;
            case BT_INTERSTED:
                //interested
                break;
            case BT_NOT_INTERESTED:
                //not interested
                break;
            case BT_HAVE:
                //have
                break;
            case BT_BITFILED:
                //bitfield
                bt_msg->payload.bitfield.size = *((size_t *) rcv_message);
                rcv_message += sizeof (size_t);
                length -= sizeof (size_t);
                bt_msg->payload.bitfield.bitfield = (char *) malloc(length);
                bcopy(rcv_message, bt_msg->payload.bitfield.bitfield, bt_msg->payload.bitfield.size);
                break;
            case BT_REQUEST:
                //request
                bt_msg->payload.request.index = *((int *) rcv_message);
                rcv_message += sizeof (int);
                bt_msg->payload.request.begin = *((int *) rcv_message);
                rcv_message += sizeof (int);
                bt_msg->payload.cancel.length = *((int *) rcv_message);
                rcv_message += sizeof (int);
                break;
            case BT_PIECE:
                //piece
                bt_msg->payload.piece.index = *((int *) rcv_message);
                rcv_message += sizeof (int);
                length -= sizeof (int);
                bt_msg->payload.piece.begin = *((int *) rcv_message);
                rcv_message += sizeof (int);
                length -= sizeof (int);
                bt_msg->payload.piece.block = (char *) malloc(length);
                bcopy(rcv_message, bt_msg->payload.piece.block, length);
                break;
            case BT_CANCEL:
                //cancel
                bt_msg->payload.cancel.index = *((int *) rcv_message);
                rcv_message += sizeof (int);
                bt_msg->payload.cancel.begin = *((int *) rcv_message);
                rcv_message += sizeof (int);
                bt_msg->payload.cancel.length = *((int *) rcv_message);
                rcv_message += sizeof (int);
            case 9:
            default:
                //port
                break;
        }
    }
}

void send_message(int sock, bt_msg_t bt_msg) {
    char * send_message = (char *) malloc(bt_msg.length + sizeof (bt_msg.length));
    bcopy((char *) &(bt_msg.length), send_message, sizeof (bt_msg.length));
    int length = bt_msg.length;
    char * head = send_message + sizeof (bt_msg.length);
    bcopy((char *) &(bt_msg.bt_type), head, sizeof (bt_msg.bt_type));
    head = head + sizeof (bt_msg.bt_type);
    length -= sizeof (bt_msg.bt_type);
    printf("%s\n", send_message);
    switch (bt_msg.bt_type) {
        case BT_CHOKE:
            //Choke
            break;
        case BT_UNCHOKE:
            //unchoke
            break;
        case BT_INTERSTED:
            //interested
            break;
        case BT_NOT_INTERESTED:
            //not interested
            break;
        case BT_HAVE:
            //have
            break;
        case BT_BITFILED:
            //bitfield
            bcopy((char *) &(bt_msg.payload.bitfield.size), head, sizeof (bt_msg.payload.bitfield.size));
            head = head + sizeof (bt_msg.payload.bitfield.size);
            length -= sizeof (bt_msg.payload.bitfield.size);

            //            length -= sizeof (size_t);
            bcopy(bt_msg.payload.bitfield.bitfield, head, length);
            break;
        case BT_REQUEST:
            //request
            bcopy((char *) &(bt_msg.payload.request.index), head, sizeof (bt_msg.payload.request.index));
            head = head + sizeof (bt_msg.payload.request.index);
            length -= sizeof (bt_msg.payload.request.index);

            bcopy((char *) &(bt_msg.payload.request.begin), head, sizeof (bt_msg.payload.request.begin));
            head = head + sizeof (bt_msg.payload.request.begin);
            length -= sizeof (bt_msg.payload.request.begin);

            bcopy((char *) &(bt_msg.payload.request.length), head, sizeof (bt_msg.payload.request.length));
            head = head + sizeof (bt_msg.payload.request.length);
            length -= sizeof (bt_msg.payload.request.length);
        case BT_PIECE:
            //piece
            bcopy((char *) &(bt_msg.payload.piece.index), head, sizeof (bt_msg.payload.piece.index));
            head = head + sizeof (bt_msg.payload.piece.index);
            length -= sizeof (bt_msg.payload.piece.index);

            bcopy((char *) &(bt_msg.payload.piece.begin), head, sizeof (bt_msg.payload.piece.begin));
            head = head + sizeof (bt_msg.payload.piece.begin);
            length -= sizeof (bt_msg.payload.piece.begin);

            bcopy((bt_msg.payload.piece.block), head, length);
            break;
        case BT_CANCEL:
            //cancel
            bcopy((char *) &(bt_msg.payload.cancel.index), head, sizeof (bt_msg.payload.cancel.index));
            bcopy((char *) &(bt_msg.payload.cancel.begin), head, sizeof (bt_msg.payload.cancel.begin));
            bcopy((char *) &(bt_msg.payload.cancel.length), head, sizeof (bt_msg.payload.cancel.length));

            break;
        case 9:
        default:
            //port
            break;


    }
    send(sock, send_message, bt_msg.length + sizeof (bt_msg.length), 0);
}

void take_action(bt_args_t &bt_args, bt_msg_t &bt_msg, int current_peer, int sock, int &iam_choked) {
    switch (bt_msg.bt_type) {
        case BT_CHOKE:
            iam_choked = 1;
            break;
        case BT_UNCHOKE:
            iam_choked = 0;
            break;
        case BT_INTERSTED:
            bt_args.peers[current_peer]->interested = 1;
            break;
        case BT_NOT_INTERESTED:
            bt_args.peers[current_peer]->interested = 0;
            break;
        case BT_HAVE:
            //have
            break;
        case BT_BITFILED:
            break;
        case BT_REQUEST:
            //request
            if (bt_args.peers[current_peer]->choked ==0 && bt_args.peers[current_peer]->interested == 1 && iam_choked == 0){
                bt_msg_t send_msg = createPieceToSend(bt_args,bt_msg.payload.request.index);;
            }
            break;
        case BT_PIECE:
            //piece
            
            break;
        case BT_CANCEL:
            //cancel
            break;
        case 9:
        default:
            //port
            break;

    }
}

int nextNeededPiece(){
    for (int i=0; i<myBitfield.size(); i++) {
        if (!myBitfield[i]) {
            return i;
        }
    }
    return -1;
}



