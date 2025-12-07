/*
 * microtcp, a lightweight implementation of TCP for teaching,
 * and academic purposes.
 *
 * Copyright (C) 2015-2017  Manolis Surligas <surligas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "microtcp.h"
#include "../utils/crc32.h"

// Creates a TCP socket with domain AF_INET from the test which means that
// IPv4 IPs are used (Check parameters).
microtcp_sock_t microtcp_socket(int domain, int type, int protocol)
{
    int sock;
    if ((sock = socket(domain, type, protocol)) == -1)
    {
        perror("[ERROR] SOCKET COULD NOT BE OPENED");
        exit(EXIT_FAILURE);
    }
    microtcp_sock_t opened_socket;
    memset(&opened_socket, 0, sizeof(opened_socket));
    opened_socket.sd = sock;
    return opened_socket;
}

// Bind assigns an open socket to a specific interface and port
int microtcp_bind(microtcp_sock_t *socket, const struct sockaddr *address, socklen_t address_len)
{
    if (bind(socket->sd, address, address_len) == -1)
    {
        perror("[ERROR] SOCKET COULD NOT BE BINDED");
        return -1;
    }
    return 0;
}

// Initiates the connection with the receiver
int microtcp_connect(microtcp_sock_t *socket, const struct sockaddr *address, socklen_t address_len)
{

    srand(time(NULL));
    // First create the packet header
    microtcp_header_t header_syn;
    header_syn.seq_number = rand() % 1000;
    header_syn.ack_number = 0;
    header_syn.future_use0 = 0;
    header_syn.future_use1 = 0;
    header_syn.future_use2 = 0;
    header_syn.data_len = 0;
    header_syn.control = 1 << 14; // 15th bit is the SYN bit
    header_syn.window = MICROTCP_RECVBUF_LEN;
    header_syn.checksum = 0;
    header_syn.checksum = crc32(&header_syn, sizeof(header_syn));
    // Now initiate the socket variables
    socket->state = LISTEN;
    socket->init_win_size = MICROTCP_WIN_SIZE;
    socket->curr_win_size = MICROTCP_WIN_SIZE;
    socket->recvbuf = NULL;
    socket->buf_fill_level = 0;
    socket->cwnd = MICROTCP_INIT_CWND;
    socket->ssthresh = MICROTCP_INIT_SSTHRESH;
    socket->seq_number = header_syn.seq_number;
    socket->ack_number = socket->seq_number + 1;
    socket->packets_send = 0;
    socket->packets_received = 0;
    socket->packets_lost = 0;
    socket->bytes_send = 0;
    socket->bytes_received = 0;
    socket->bytes_lost = 0;
    // Now send the header to the receiver
    if (sendto(socket->sd, &header_syn, sizeof(header_syn), 0, address, address_len) < sizeof(header_syn))
    {
        perror("[ERROR microtcp_connect] sendto sent less bytes than it should\n");
    }
    printf("[CLIENT] sent SYN\n");
    socket->packets_send++;
    // Wait for the response SYN+ACK from the receiver
    microtcp_header_t header_received;
    ssize_t bytes_received = recvfrom(socket->sd, &header_received, sizeof(header_received), 0, address, &address_len);
    // Check correctness of received
    if (header_received.ack_number != socket->ack_number)
    {
        perror("[ERROR microtcp_connect] ACK number received is not correct\n");
    }
    uint16_t correct_sum = (1 << 12) + (1 << 14);
    printf("correct sum is %d\n", correct_sum);
    if (!((header_received.control & (1 << 14)) && (header_received.control & (1 << 12))))
    {
        perror("[ERROR microtcp_connect] Not received SYN+ACK\n");
    }
    else if (correct_sum != header_received.control)
    {
        perror("[ERROR microtcp_connect] Wrong bits on control\n");
    }
    uint32_t checksum_assigned_on_header_received = header_received.checksum;
    header_received.checksum = 0;
    if (checksum_assigned_on_header_received != crc32(&header_received, sizeof(header_received)))
    {
        perror("[ERROR microtcp_connect] Checksum is wrong\n");
    }
    // No need to check datalen since there is no data sent so its zero
    socket->packets_received++;
    socket->state = ESTABLISHED;
    socket->recvbuf = malloc(MICROTCP_RECVBUF_LEN);
    printf("[CLIENT] received SYN+ACK\n");

    // Now we reply back with ACK
    microtcp_header_t header_ack;
    header_ack.seq_number = socket->seq_number + 1;
    header_ack.ack_number = header_received.seq_number + 1;
    header_ack.future_use0 = 0;
    header_ack.future_use1 = 0;
    header_ack.future_use2 = 0;
    header_ack.data_len = 0;
    header_ack.control = 1 << 12; // 13th bit is the ACK bit
    header_ack.window = MICROTCP_RECVBUF_LEN;
    header_ack.checksum = 0;
    header_ack.checksum = crc32(&header_ack, sizeof(header_ack));
    // Now lets update other variables
    socket->seq_number = header_ack.seq_number;
    socket->ack_number = header_ack.seq_number + 1;
    socket->dest_address = address;
    socket->dests_address_len = address_len;
    // Now send the header to the receiver
    if (sendto(socket->sd, &header_ack, sizeof(header_ack), 0, address, address_len) < sizeof(header_ack))
    {
        perror("[ERROR microtcp_connect] sendto ACK sent less bytes than it "
               "should\n");
    }
    socket->packets_send++;
    printf("[CLIENT] connected successfuly\n");
    return 0;
}

int microtcp_accept(microtcp_sock_t *socket, struct sockaddr *address, socklen_t address_len)
{
    microtcp_header_t header_received; // This is the SYN header received from connect
    ssize_t bytes_received = recvfrom(socket->sd, &header_received, sizeof(header_received), 0, address, &address_len);
    // Check correctness of received
    uint16_t correct_sum = (1 << 14);
    printf("correct sum is %d\n", correct_sum);
    if (!(header_received.control & (1 << 14)))
    {
        perror("[ERROR microtcp_accept] Not received SYN\n");
    }
    else if (correct_sum != header_received.control)
    {
        perror("[ERROR microtcp_accept] Wrong bits on control\n");
    }
    uint32_t checksum_assigned_on_header_received = header_received.checksum;
    header_received.checksum = 0;
    if (checksum_assigned_on_header_received != crc32(&header_received, sizeof(header_received)))
    {
        perror("[ERROR microtcp_accept] Checksum is wrong\n");
    }

    socket->state = LISTEN;
    socket->init_win_size = MICROTCP_WIN_SIZE;
    socket->curr_win_size = MICROTCP_WIN_SIZE;
    socket->recvbuf = malloc(MICROTCP_RECVBUF_LEN);
    socket->buf_fill_level = 0;
    socket->cwnd = MICROTCP_INIT_CWND;
    socket->ssthresh = MICROTCP_INIT_SSTHRESH;
    socket->seq_number = rand() % 1000;
    socket->ack_number = socket->seq_number + 1;
    socket->packets_send = 0;
    socket->packets_received = 1;
    socket->packets_lost = 0;
    socket->bytes_send = 0;
    socket->bytes_received = 0;
    socket->bytes_lost = 0;

    microtcp_header_t header_syn_ack;
    header_syn_ack.seq_number = socket->seq_number;
    header_syn_ack.ack_number = header_received.seq_number + 1;
    header_syn_ack.future_use0 = 0;
    header_syn_ack.future_use1 = 0;
    header_syn_ack.future_use2 = 0;
    header_syn_ack.data_len = 0;
    header_syn_ack.control = (1 << 14) | (1 << 12); // 15th bit is the SYN bit
    header_syn_ack.window = MICROTCP_RECVBUF_LEN;
    header_syn_ack.checksum = 0;
    header_syn_ack.checksum = crc32(&header_syn_ack, sizeof(header_syn_ack));

    if (sendto(socket->sd, &header_syn_ack, sizeof(header_syn_ack), 0, address, address_len) < sizeof(header_syn_ack))
    {
        perror("[ERROR microtcp_connect] sendto SYNACK sent less bytes than it "
               "should\n");
    }
    printf("[SERVER] sent SYN+ACK\n");

    socket->packets_send++;

    microtcp_header_t header_received_2;
    ssize_t bytes_received_2 = recvfrom(socket->sd, &header_received_2, sizeof(header_received_2), 0, address, &address_len);
    // Check correctness of received
    if (header_received_2.ack_number != socket->ack_number)
    {
        perror("[ERROR microtcp_accept] ACK number received is not correct\n");
    }

    correct_sum = (1 << 12);
    printf("correct sum is %d\n", correct_sum);
    if (!((header_received_2.control & (1 << 12))))
    {
        perror("[ERROR microtcp_accept] Not received SYN+ACK\n");
    }
    else if (correct_sum != header_received_2.control)
    {
        perror("[ERROR microtcp_accept] Wrong bits on control\n");
    }
    checksum_assigned_on_header_received = header_received_2.checksum;
    header_received_2.checksum = 0;
    if (checksum_assigned_on_header_received != crc32(&header_received_2, sizeof(header_received_2)))
    {
        perror("[ERROR microtcp_accept] Checksum is wrong\n");
    }

    socket->packets_received++;
    socket->state = ESTABLISHED;
    socket->dest_address = address;
    socket->dests_address_len = address_len;
    // No need to check datalen since there is no data sent so its zero
    return 0;
}

int microtcp_shutdown(microtcp_sock_t *socket, int how)
{

    // microtcp_header_t header_syn_ack;
    // socket->seq_number++;
    // socket->ack_number = socket->seq_number + 1;

    // header_syn_ack.seq_number = socket->seq_number;
    // header_syn_ack.ack_number = socket->ack_number;
    // header_syn_ack.future_use0 = 0;
    // header_syn_ack.future_use1 = 0;
    // header_syn_ack.future_use2 = 0;
    // header_syn_ack.data_len = 0;
    // header_syn_ack.control = (1 << 15) | (1 << 12); // 15th bit is the SYN bit
    // header_syn_ack.window = MICROTCP_RECVBUF_LEN;
    // header_syn_ack.checksum = 0;
    // header_syn_ack.checksum = crc32(&header_syn_ack, sizeof(header_syn_ack));

    // if (sendto(socket->sd, &header_syn_ack, sizeof(header_syn_ack), 0, socket->dest_address,
    //            socket->dests_address_len) < sizeof(header_syn_ack)) {
    //   perror("[ERROR microtcp_connect] sendto SYNACK sent less bytes than it "
    //          "should\n");
    // }

    // socket->packets_send++;

    // microtcp_header_t
    //     header_received; // This is the SYN header received from connect
    // ssize_t bytes_received =
    //     recvfrom(socket->sd, &header_received, sizeof(header_received), 0,
    //              address, address_len);
    // // Check correctness of received
    // uint16_t correct_sum = (1 << 14);
    // printf("correct sum is %d\n", correct_sum);
    // if (!(header_received.control & (1 << 14))) {
    //   perror("[ERROR microtcp_accept] Not received SYN\n");
    // } else if (correct_sum != header_received.control) {
    //   perror("[ERROR microtcp_accept] Wrong bits on control\n");
    // }
    // uint32_t checksum_assigned_on_header_received = header_received.checksum;
    // header_received.checksum = 0;
    // if (checksum_assigned_on_header_received !=
    //     crc32(&header_received, sizeof(header_received))) {
    //   perror("[ERROR microtcp_accept] Checksum is wrong\n");
    // }
}

ssize_t microtcp_send(microtcp_sock_t *socket, const void *buffer, size_t length, int flags) { /* Your code here */ }

ssize_t microtcp_recv(microtcp_sock_t *socket, void *buffer, size_t length, int flags) { /* Your code here */ }
