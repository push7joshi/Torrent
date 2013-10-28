#include <iostream>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip.h> //ip hdeader library (must come before ip_icmp.h)
#include <netinet/ip_icmp.h> //icmp header
#include <arpa/inet.h> //internet address library
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <typeinfo>
#include "bencode.h"
#include "bt_lib.h"
#include "bt_setup.h"

#include <fstream>


using namespace std;

int launch_seeder(bt_args_t &bt_args, be_node traverse_node) {
    printf("launching seeder\n");
    int seeder_sock, leecher_sock;
    /*client_addr_size*/;
    struct sockaddr_in seeder_addr, leecher_addr;
    hand_shake_msg_t handshake;

    //Setup socket for seeder
    seeder_addr = socketSetup(seeder_sock);

    bt_args.sockets[0] = seeder_sock;

    //set listen to up to 5 queued connections
    if (listen(seeder_sock, 5) == -1) {
        printf("Listen failed!");
        exit(EXIT_FAILURE);
    }
    if (bt_args.verbose) {
        printf("\nListening on the specified port....%u\n", ntohs(seeder_addr.sin_port));
    }
    cout << "waiting for connection\n";
    socklen_t cli_len = sizeof (leecher_addr);

    /*accept client connection*/
    if ((leecher_sock = accept(seeder_sock, (struct sockaddr *) &leecher_addr, &cli_len)) >= 0) {
        recv(leecher_sock, &handshake, sizeof (hand_shake_msg_t), 0);
        char hostname[256]; //, ipaddress[4];
        struct in_addr addr;
        if (gethostname(hostname, sizeof (hostname)) >= 0) {
            struct hostent *phe = gethostbyname(hostname);
            if (phe != NULL && phe->h_addr_list[0] != NULL) {
                memcpy(&addr, phe->h_addr_list[0], sizeof (struct in_addr));
            }
        }
        char seeder_id[20];
        /*compute seeder i.e own id*/

        calc_id((char *) &addr, ntohs(seeder_addr.sin_port), seeder_id);
        int i = 0;
        for (i = 0; i < 20; i++) {
            printf("%u", seeder_id[i]);
        }

        /*comparing hash*/
        for (i = 0; i < sizeof (handshake.info_hash); i++) {
            unsigned char x = *(*(bt_args.bt_info->piece_hashes) + i);
            if (handshake.info_hash[i] != x) {
                cerr << "hash did not match\n";
                close(leecher_sock);
                exit(EXIT_FAILURE);
            }
        }

        /* checking for free peers*/
        int current_peer = -1;
        for (i = 0; i < MAX_CONNECTIONS; i++) {
            if (bt_args.peers[i] == NULL) {
                current_peer = i;
            }
        }
        if (current_peer == -1) {
            cerr << "no free positions\n";
            close(leecher_sock);
            exit(EXIT_FAILURE);
        }
        //set peer values
        //connection starts as choked and not interested
        bt_args.peers[current_peer] = (peer_t *) malloc(sizeof (peer_t));
        char leecher_id[20];
        calc_id((char *) &(leecher_addr.sin_addr.s_addr), ntohs(leecher_addr.sin_port), leecher_id);
        strcpy((char *) bt_args.peers[current_peer]->id, leecher_id);
        bt_args.peers[current_peer]->port = leecher_addr.sin_port;
        bt_args.peers[current_peer]->sockaddr = leecher_addr;
        bt_args.peers[current_peer]->choked = 1;
        bt_args.peers[current_peer]->interested = 0;

        /*generate handshake message to be sent*/
        hand_shake_msg_t handshake_send; // = (char *) malloc(sizeof (hand_shake_msg_t));
        handshake_send.protocol[0] = (unsigned char) 19; // + "BitTorrent Protocol");
        strcpy((char *) (handshake_send.protocol + 1), "BitTorrent Protocol");
        int j;
        for (j = 0; j < 8; j++) {
            handshake_send.reserved[j] = static_cast<unsigned char> (0);
        }
        // strcpy((char *) handshake_msg.protocol, (char(19) + "BitTorrent Protocol"));
        strcpy((char *) handshake_send.info_hash, *(bt_args.bt_info->piece_hashes));
        //int i;
        for (i = 0; i < 20; i++) {
            handshake_send.peer_id[i] = seeder_id[i];
            cout << i;
        }
        send(leecher_sock, &handshake_send, sizeof (hand_shake_msg_t), 0);
        cout << "Handshake successful\n";
        //stuff after the handshake
        //cout << client_addr.sin_addr.s_addr << "\n";
        //close(client_sock);
        int iam_choked = 0;
        bt_msg_t bt_msg;
        recieve_message(leecher_sock, &bt_msg);
        //initial interested request
        if (bt_msg.bt_type == BT_INTERSTED) {
            take_action(bt_args, bt_msg, current_peer, leecher_sock, iam_choked);
            //send bitfield
        }
        while (true) {
            recieve_message(leecher_sock, &bt_msg);
            take_action(bt_args, bt_msg, current_peer, leecher_sock, iam_choked);
        }
    } else {
        cout << "accept failed";
    }
    return EXIT_SUCCESS;
}

void launch_leecher(bt_args_t bt_args, be_node traverse_node) {
    int i = 0;
    for (i = 0; i < MAX_CONNECTIONS; i++) {
        if (bt_args.peers[i] == NULL) {
            continue;
        } else {
            struct sockaddr_in stSockAddr;
            int socketFD = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (-1 == socketFD) {
                cerr << ("cannot create socket");
                exit(EXIT_FAILURE);
            }
            if (bt_args.verbose) {
                printf("\nClient socket created. Connecting...");
            }
            memset(&stSockAddr, 0, sizeof (stSockAddr));
            stSockAddr = bt_args.peers[i]->sockaddr;
            if (-1 == connect(socketFD, (struct sockaddr *) &stSockAddr, sizeof (stSockAddr))) {
                perror("connect failed");
                close(socketFD);
                exit(EXIT_FAILURE);
            } else if (bt_args.verbose) {
                printf("\nConnected to the server, will try to send the data...");
            }

            /*Creating message for handshake*/
            hand_shake_msg_t handshake_msg;
            handshake_msg.protocol[0] = (unsigned char) 19; // + "BitTorrent Protocol");
            strcpy((char *) (handshake_msg.protocol + 1), "BitTorrent Protocol");
            // strcpy((char *) handshake_msg.protocol, (char(19) + "BitTorrent Protocol"));
            //strcpy((char *) handshake_msg.peer_id, (char *) bt_args.peers[i]->id);
            strcpy((char *) handshake_msg.info_hash, *(bt_args.bt_info->piece_hashes));
            int j;
            for (j = 0; j < 8; j++) {
                handshake_msg.reserved[j] = static_cast<unsigned char> (0);
            }
            for (j = 0; j<sizeof (handshake_msg.peer_id); j++) {
                handshake_msg.peer_id[j] = bt_args.peers[i]->id[j];
            }

            /*sending connection*/
            size_t sendClient = send(socketFD, (void *) &handshake_msg, sizeof (hand_shake_msg_t), 0);
            if (bt_args.verbose) {
                printf("\nSending data..");
            }
            if (sendClient == -1) {
                printf("\nError sending!\n");
                exit(EXIT_FAILURE);
            } else {
                cout << "sent handshake\n";
            }

            /*wait for recieve handshake...*/
            hand_shake_msg_t handshake_rcv;
            recv(socketFD, &handshake_rcv, sizeof (hand_shake_msg_t), 0);
            //            int id_flag = 0; //strcmp((char *) handshake_rcv.peer_id, (char *) bt_args.peers[i]->id);
            //            int hash_flag = 0; //strcmp((char *) handshake_rcv.info_hash, *(bt_args.bt_info->piece_hashes));
            //b = 0;
            //int j;
            /*comparing hash*/
            for (j = 0; j < 20; j++) {
                unsigned char x = *(*(bt_args.bt_info->piece_hashes) + j);
                if (handshake_rcv.info_hash[j] != x) {
                    cerr << "hash did not match\n";
                    close(socketFD);
                    exit(EXIT_FAILURE);
                }
            }

            /*comapring seeder's peer_id*/
            for (j = 0; j < 20; j++) {
                unsigned char x = bt_args.peers[i]->id[j];
                if (handshake_rcv.peer_id[j] != x) {
                    cerr << "seeder did not match\n";
                    close(socketFD);
                    exit(EXIT_FAILURE);
                }
            }
            cout << "handshake success\n";

            //stuff after handshake
            sendInitialInterest(socketFD, i);
            bt_msg bt_msg;
            recieve_message(socketFD, bt_msg);
            bt_args.peers[i]->choked = 0;
            bt_args.peers[i]->interested = 1;
            int iam_choked;
            take_action(bt_args, bt_msg, i, socketFD, iam_choked);
            int is_incomplete = 0;
            while (is_incomplete) {
                int next_needed_piece = 0; //cal function
                long torrent_length = bt_args.bt_info->length;
                long finished = (next_needed_piece * RequestSize);
                int send_length;
                if (finished + RequestSize > torrent_length) {
                    send_length = RequestSize;
                } else {
                    send_length = torrent_length - send_length;
                }

                requestPiece();
            }
            (void) shutdown(socketFD, SHUT_RDWR);
            close(socketFD);
        }
    }
}

int main(int argc, char * argv[]) {

    bt_args_t bt_args;
    be_node * node; // top node in the bencoding
    int i;

    bt_info_t torrInfo;

    parse_args(&bt_args, argc, argv);
    cout << bt_args.verbose << "\n";
    //int a = 9/0;
    printf("peer problems");
    if (bt_args.verbose) {
        printf("Args:\n");
        printf("verbose: %d\n", bt_args.verbose);
        printf("save_file: %s\n", bt_args.save_file);
        printf("log_file: %s\n", bt_args.log_file);
        printf("torrent_file: %s\n", bt_args.torrent_file);

        for (i = 0; i < MAX_CONNECTIONS; i++) {
            if (bt_args.peers[i] != NULL) {
                printf("-----%d\n", i);
                print_peer(bt_args.peers[i]);
                printf("%daaaaaaaaaa\n", i);
            }
        }


    }

    //read and parse the torent file
    node = load_be_node(bt_args.torrent_file);

    if (bt_args.verbose) {
        be_dump(node);
    }

    memset(&torrInfo, sizeof (torrInfo), 0);
    //main client loop
    printf("Starting Main Loop\n");
    while (1) {

        be_node * traverseNode = node;
        //bt_info_t
        char *trackerURL = NULL;
        //get the URL :

        if (!strcmp((traverseNode->val.d)->key, "announce")) {
            trackerURL = (char *) malloc(100);
            strcpy(trackerURL, traverseNode->val.d->val->val.s);
        } else {
            puts("Tracker URL missing!");
            exit(EXIT_FAILURE);
        }

        be_dump1(node, &torrInfo);

        bt_args.bt_info = &torrInfo;
        //bt_args.peers[i] != NULL
        //printf("this can not print %u\n",bt_args.peers[0]->port);
        print_peer(bt_args.peers[0]);

        //-------------TODO---------------
        //createPieceToSend(bt_args, 2);
        //--------------------------------

        if (bt_args.peers[0] == NULL) {
            printf("going to launch seeder\n");
            launch_seeder(bt_args, *traverseNode);
        } else {
            printf("peers to connect to, Launching leecher\n");
            init_bitfield(&bt_args, false);
            launch_leecher(bt_args, *traverseNode);
        }

        /* //contact_tracker(&bt_args);
         //connect with the tracker to get the information from trackers

         //try to accept incoming connection from new peer


         //poll current peers for incoming traffic
         //   write pieces to files
         //   udpdate peers choke or unchoke status
         //   responses to have/havenots/interested etc.

         //for peers that are not choked
         //   request pieaces from outcoming traffic

         //check livelenss of peers and replace dead (or useless) peers
         //with new potentially useful peers

         //update peers,*/

    }
    return 0;
}
