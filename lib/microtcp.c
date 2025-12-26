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

static inline int min3(int a, int b, int c)
{
    int m = (a < b) ? a : b;
    return (m < c) ? m : c;
}

static inline int min2(int a, int b)
{
    int m = (a < b) ? a : b;
    return m;
}

// Creates a TCP socket with domain AF_INET from the test which means that
// IPv4 IPs are used (Check parameters).
microtcp_sock_t microtcp_socket(int domain, int type, int protocol)
{
    int sock;
    if ((sock = socket(domain, type, protocol)) == -1)
    {
        printf("[ERROR] SOCKET COULD NOT BE OPENED\n");
        exit(EXIT_FAILURE);
    }

    // // Set timeout
    // struct timeval timeout;
    // timeout.tv_sec = 0;
    // timeout.tv_usec = MICROTCP_ACK_TIMEOUT_US;
    // if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval)) < 0)
    // {
    //     perror("[ERROR] SOCKET COULND'T HANDLE TIMEOUT\n");
    //     exit(EXIT_FAILURE);
    // }

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
        printf("[ERROR] SOCKET COULD NOT BE BINDED");
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
    header_syn.checksum = crc32((const uint8_t *)&header_syn, sizeof(header_syn));
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
    socket->client_or_server = 0;
    socket->dup_acks_count = 0;
    socket->last_received_ack = 0;

    // Now send the header to the receiver
    if (sendto(socket->sd, &header_syn, sizeof(header_syn), 0, address, address_len) < sizeof(header_syn))
    {
        printf("[ERROR microtcp_connect] sendto sent less bytes than it should\n");
    }
    printf("[CLIENT] sent SYN\n");
    socket->packets_send++;
    // Wait for the response SYN+ACK from the receiver
    microtcp_header_t header_received;
    ssize_t bytes_received = recvfrom(socket->sd, &header_received, sizeof(header_received), 0, address, &address_len);
    // Check correctness of received
    if (header_received.ack_number != socket->ack_number)
    {
        printf("[ERROR microtcp_connect] ACK number received is not correct\n");
    }
    uint16_t correct_sum = (1 << 12) + (1 << 14);
    printf("correct sum is %d\n", correct_sum);
    if (!((header_received.control & (1 << 14)) && (header_received.control & (1 << 12))))
    {
        printf("[ERROR microtcp_connect] Not received SYN+ACK\n");
    }
    else if (correct_sum != header_received.control)
    {
        printf("[ERROR microtcp_connect] Wrong bits on control\n");
    }
    uint32_t checksum_assigned_on_header_received = header_received.checksum;
    header_received.checksum = 0;
    if (checksum_assigned_on_header_received != crc32((const uint8_t *)&header_received, sizeof(header_received)))
    {
        printf("[ERROR microtcp_connect] Checksum is wrong\n");
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
    header_ack.checksum = crc32((const uint8_t *)&header_ack, sizeof(header_ack));
    // Now lets update other variables
    socket->seq_number = header_ack.seq_number;
    socket->ack_number = header_ack.seq_number + 1;
    socket->dest_address = address;
    socket->dests_address_len = address_len;
    // Now send the header to the receiver
    if (sendto(socket->sd, &header_ack, sizeof(microtcp_header_t), 0, address, address_len) < sizeof(header_ack))
    {
        printf("[ERROR microtcp_connect] sendto ACK sent less bytes than it "
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
        printf("[ERROR microtcp_accept] Not received SYN\n");
    }
    else if (correct_sum != header_received.control)
    {
        printf("[ERROR microtcp_accept] Wrong bits on control\n");
    }
    uint32_t checksum_assigned_on_header_received = header_received.checksum;
    header_received.checksum = 0;
    if (checksum_assigned_on_header_received != crc32((const uint8_t *)&header_received, sizeof(header_received)))
    {
        printf("[ERROR microtcp_accept] Checksum is wrong\n");
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
    socket->client_or_server = 1;
    socket->dup_acks_count = 0;
    socket->last_received_ack = 0;

    microtcp_header_t header_syn_ack;
    header_syn_ack.seq_number = socket->seq_number;
    header_syn_ack.ack_number = header_received.seq_number + 1;
    header_syn_ack.future_use0 = 0;
    header_syn_ack.future_use1 = 0;
    header_syn_ack.future_use2 = 0;
    header_syn_ack.data_len = 0;
    header_syn_ack.control = (1 << 14) | (1 << 12); // 14th bit is the SYN bit and 12th bit is the ACK bit
    header_syn_ack.window = MICROTCP_RECVBUF_LEN;
    header_syn_ack.checksum = 0;
    header_syn_ack.checksum = crc32((const uint8_t *)&header_syn_ack, sizeof(header_syn_ack));

    if (sendto(socket->sd, &header_syn_ack, sizeof(header_syn_ack), 0, address, address_len) < sizeof(header_syn_ack))
    {
        printf("[ERROR microtcp_connect] sendto SYNACK sent less bytes than it "
               "should\n");
    }
    printf("[SERVER] sent SYN+ACK\n");

    socket->packets_send++;

    microtcp_header_t header_received_2;
    ssize_t bytes_received_2 = recvfrom(socket->sd, &header_received_2, sizeof(header_received_2), 0, address, &address_len);
    // Check correctness of received
    if (header_received_2.ack_number != socket->ack_number)
    {
        printf("[ERROR microtcp_accept] ACK number received is not correct\n");
    }

    correct_sum = (1 << 12);
    printf("correct sum is %d\n", correct_sum);
    if (!((header_received_2.control & (1 << 12))))
    {
        printf("[ERROR microtcp_accept] Not received SYN+ACK\n");
    }
    else if (correct_sum != header_received_2.control)
    {
        printf("[ERROR microtcp_accept] Wrong bits on control\n");
    }
    checksum_assigned_on_header_received = header_received_2.checksum;
    header_received_2.checksum = 0;
    if (checksum_assigned_on_header_received != crc32((const uint8_t *)&header_received_2, sizeof(header_received_2)))
    {
        printf("[ERROR microtcp_accept] Checksum is wrong\n");
    }

    socket->packets_received++;
    socket->state = ESTABLISHED;
    socket->dest_address = address;
    socket->dests_address_len = address_len;
    socket->ack_number = header_received_2.seq_number;
    // No need to check datalen since there is no data sent so its zero
    return 0;
}

int microtcp_shutdown(microtcp_sock_t *socket, int how)
{

    if (socket->client_or_server == 0) // If client
    {
        microtcp_header_t header_fin_ack;
        socket->seq_number++;
        socket->ack_number = socket->seq_number + 1;

        header_fin_ack.seq_number = socket->seq_number;
        header_fin_ack.ack_number = socket->ack_number;
        header_fin_ack.future_use0 = 0;
        header_fin_ack.future_use1 = 0;
        header_fin_ack.future_use2 = 0;
        header_fin_ack.data_len = 0;
        header_fin_ack.control = (1 << 15) | (1 << 12); // 15th bit is the FIN bit and 12th bit is the ACK bit
        header_fin_ack.window = MICROTCP_RECVBUF_LEN;
        header_fin_ack.checksum = 0;
        header_fin_ack.checksum = crc32((const uint8_t *)&header_fin_ack, sizeof(header_fin_ack));

        if (sendto(socket->sd, &header_fin_ack, sizeof(header_fin_ack), 0, socket->dest_address, socket->dests_address_len) < sizeof(header_fin_ack))
        {
            printf("[ERROR microtcp_shutdown] sendto FINACK sent less bytes than it "
                   "should\n");
        }
        socket->packets_send++;

        microtcp_header_t header_received; // This is the ACK header received from server
        ssize_t bytes_received = recvfrom(socket->sd, &header_received, sizeof(header_received), 0, socket->dest_address, &socket->dests_address_len);

        // Check correctness of received
        uint16_t correct_sum = (1 << 12);
        printf("correct sum is %d\n", correct_sum);
        if (!(header_received.control & (1 << 12)))
        {
            printf("[ERROR microtcp_shutdown] Not received ACK\n");
        }
        else if (correct_sum != header_received.control)
        {
            printf("[ERROR microtcp_shutdown] Wrong bits on control\n");
        }
        uint32_t checksum_assigned_on_header_received = header_received.checksum;
        header_received.checksum = 0;
        if (checksum_assigned_on_header_received != crc32((const uint8_t *)&header_received, sizeof(header_received)))
        {
            printf("[ERROR microtcp_shutdown] Checksum is wrong\n");
        }
        socket->packets_received++;
        socket->state = CLOSING_BY_PEER;

        // Receive FIN ACK from server too
        bytes_received = recvfrom(socket->sd, &header_received, sizeof(header_received), 0, socket->dest_address, &socket->dests_address_len);

        // Check correctness of received
        correct_sum = (1 << 12) | (1 << 15);
        printf("correct sum is %d\n", correct_sum);
        if (!((header_received.control & (1 << 15)) && (header_received.control & (1 << 12))))
        {
            printf("[ERROR microtcp_shutdown] Not received FIN ACK (2)\n");
        }
        else if (correct_sum != header_received.control)
        {
            printf("[ERROR microtcp_shutdown] Wrong bits on control (2)\n");
        }
        checksum_assigned_on_header_received = header_received.checksum;
        header_received.checksum = 0;
        if (checksum_assigned_on_header_received != crc32((const uint8_t *)&header_received, sizeof(header_received)))
        {
            printf("[ERROR microtcp_shutdown] Checksum is wrong (2)\n");
        }
        socket->packets_received++;
        socket->state = CLOSING_BY_HOST;

        // Resend ACK now
        microtcp_header_t header_ack;
        socket->seq_number++;
        socket->ack_number = header_received.seq_number + 1;

        header_ack.seq_number = socket->seq_number;
        header_ack.ack_number = socket->ack_number;
        header_ack.future_use0 = 0;
        header_ack.future_use1 = 0;
        header_ack.future_use2 = 0;
        header_ack.data_len = 0;
        header_ack.control = (1 << 12); // 12th bit is the ACK bit
        header_ack.window = MICROTCP_RECVBUF_LEN;
        header_ack.checksum = 0;
        header_ack.checksum = crc32((const uint8_t *)&header_ack, sizeof(header_ack));

        if (sendto(socket->sd, &header_ack, sizeof(header_ack), 0, socket->dest_address, socket->dests_address_len) < sizeof(header_ack))
        {
            printf("[ERROR microtcp_shutdown] sendto ACK sent less bytes than it "
                   "should\n");
        }
        socket->packets_send++;
        socket->state = CLOSED;
    }
    else // If server
    {
        microtcp_header_t header_received; // We wait to receive FIN ACK again from client
        ssize_t bytes_received = recvfrom(socket->sd, &header_received, sizeof(header_received), 0, socket->dest_address, &socket->dests_address_len);

        // Check correctness of received
        uint16_t correct_sum = (1 << 12) | (1 << 15);
        printf("correct sum is %d\n", correct_sum);
        if (!((header_received.control & (1 << 15)) && (header_received.control & (1 << 12))))
        {
            printf("[ERROR microtcp_shutdown] Not received FIN ACK (2)\n");
        }
        else if (correct_sum != header_received.control)
        {
            printf("[ERROR microtcp_shutdown] Wrong bits on control (2)\n");
        }
        uint32_t checksum_assigned_on_header_received = header_received.checksum;
        header_received.checksum = 0;
        if (checksum_assigned_on_header_received != crc32((const uint8_t *)&header_received, sizeof(header_received)))
        {
            printf("[ERROR microtcp_shutdown] Checksum is wrong (2)\n");
        }
        socket->packets_received++;
        socket->state = CLOSING_BY_PEER;

        // Resend ACK now
        microtcp_header_t header_ack;
        socket->seq_number++;
        socket->ack_number = header_received.seq_number + 1;

        header_ack.seq_number = socket->seq_number;
        header_ack.ack_number = socket->ack_number;
        header_ack.future_use0 = 0;
        header_ack.future_use1 = 0;
        header_ack.future_use2 = 0;
        header_ack.data_len = 0;
        header_ack.control = (1 << 12); // 12th bit is the ACK bit
        header_ack.window = MICROTCP_RECVBUF_LEN;
        header_ack.checksum = 0;
        header_ack.checksum = crc32((const uint8_t *)&header_ack, sizeof(header_ack));

        if (sendto(socket->sd, &header_ack, sizeof(header_ack), 0, socket->dest_address, socket->dests_address_len) < sizeof(header_ack))
        {
            printf("[ERROR microtcp_shutdown] sendto ACK sent less bytes than it "
                   "should\n");
        }
        socket->packets_send++;
        socket->state = CLOSED;

        microtcp_header_t header_fin_ack;
        socket->seq_number++;
        socket->ack_number = socket->seq_number + 1;

        header_fin_ack.seq_number = socket->seq_number;
        header_fin_ack.ack_number = socket->ack_number;
        header_fin_ack.future_use0 = 0;
        header_fin_ack.future_use1 = 0;
        header_fin_ack.future_use2 = 0;
        header_fin_ack.data_len = 0;
        header_fin_ack.control = (1 << 15) | (1 << 12); // 15th bit is the FIN bit and 12th bit is the ACK bit
        header_fin_ack.window = MICROTCP_RECVBUF_LEN;
        header_fin_ack.checksum = 0;
        header_fin_ack.checksum = crc32((const uint8_t *)&header_fin_ack, sizeof(header_fin_ack));

        // Send FIN ACK back for client to close too
        if (sendto(socket->sd, &header_fin_ack, sizeof(header_fin_ack), 0, socket->dest_address, socket->dests_address_len) < sizeof(header_fin_ack))
        {
            printf("[ERROR microtcp_shutdown] sendto FINACK sent less bytes than it "
                   "should\n");
        }
        socket->packets_send++;

        // Receive ACK again
        bytes_received = recvfrom(socket->sd, &header_received, sizeof(header_received), 0, socket->dest_address, &socket->dests_address_len);

        // Check correctness of received
        correct_sum = (1 << 12);
        printf("correct sum is %d\n", correct_sum);
        if (!(header_received.control & (1 << 12)))
        {
            printf("[ERROR microtcp_shutdown] Not received ACK\n");
        }
        else if (correct_sum != header_received.control)
        {
            printf("[ERROR microtcp_shutdown] Wrong bits on control\n");
        }
        checksum_assigned_on_header_received = header_received.checksum;
        header_received.checksum = 0;
        if (checksum_assigned_on_header_received != crc32((const uint8_t *)&header_received, sizeof(header_received)))
        {
            printf("[ERROR microtcp_shutdown] Checksum is wrong\n");
        }
        socket->packets_received++;
        socket->state = CLOSED;
    }
    return 0;
}

ssize_t microtcp_send(microtcp_sock_t *socket, const void *buffer, size_t length, int flags)
{
    ssize_t data_sent = 0;
    ssize_t remaining = length;

    int first_seq_number = -1;

    while (data_sent < length)
    {
        ssize_t bytes_to_send = min3(socket->cwnd, remaining, socket->curr_win_size);
        ssize_t chunks = bytes_to_send / MICROTCP_MSS; // check this

        for (int i = 0; i < chunks; i++)
        {
            if (i == 0)
            {
                first_seq_number = socket->ack_number;
            }
            // Create the header
            microtcp_header_t *header_message = malloc(sizeof(microtcp_header_t) + MICROTCP_MSS);
            header_message->seq_number = socket->seq_number;
            header_message->ack_number = socket->ack_number;
            header_message->control = 0;
            header_message->data_len = MICROTCP_MSS;
            header_message->future_use0 = 0;
            header_message->future_use1 = 0;
            header_message->future_use2 = 0;
            header_message->checksum = 0;

            // Update the socket
            // socket->curr_win_size = MICROTCP_WIN_SIZE;
            // socket->cwnd = MICROTCP_INIT_CWND;         // check
            // socket->ssthresh = MICROTCP_INIT_SSTHRESH; // check
            socket->seq_number = header_message->seq_number + header_message->data_len;
            // socket->ack_number = socket->ack_number + header_message->data_len;
            socket->packets_send++;
            socket->bytes_send = socket->bytes_send + header_message->data_len;

            memcpy((uint8_t *)header_message + sizeof(microtcp_header_t), (const uint8_t *)buffer + data_sent, MICROTCP_MSS);

            header_message->checksum = crc32((const uint8_t *)header_message, sizeof(microtcp_header_t) + MICROTCP_MSS);

            printf("sends a packet with seq_number: %d and ack_number: %d and data_len is %d\n", header_message->seq_number, header_message->ack_number, header_message->data_len);
            if (sendto(socket->sd, header_message, sizeof(microtcp_header_t) + MICROTCP_MSS, 0, socket->dest_address, socket->dests_address_len) < sizeof(header_message))
            {
                printf("[ERROR microtcp_send] sendto SEND sent less bytes than it should\n");
                free(header_message);
                return -1;
            }
            free(header_message);
        }

        if (bytes_to_send % MICROTCP_MSS)
        {
            if (first_seq_number == -1)
            {
                first_seq_number = socket->ack_number;
            }
            chunks++;
            // Create the header
            microtcp_header_t *header_message = malloc(sizeof(microtcp_header_t) + (bytes_to_send % MICROTCP_MSS));
            header_message->seq_number = socket->seq_number;
            header_message->ack_number = socket->ack_number;
            header_message->control = 0;
            header_message->data_len = (bytes_to_send % MICROTCP_MSS);
            header_message->future_use0 = 0;
            header_message->future_use1 = 0;
            header_message->future_use2 = 0;
            header_message->checksum = 0;

            // Update the socket
            socket->seq_number = header_message->seq_number;
            // socket->ack_number = socket->ack_number + header_message->data_len;
            socket->packets_send++;
            socket->bytes_send = socket->bytes_send + header_message->data_len;

            memcpy((uint8_t *)header_message + sizeof(microtcp_header_t), (const uint8_t *)buffer + data_sent, (bytes_to_send % MICROTCP_MSS));

            // fwrite((uint8_t *)buffer + data_sent, 1, (bytes_to_send % MICROTCP_MSS), stdout);

            header_message->checksum = crc32((const uint8_t *)header_message, sizeof(microtcp_header_t) + (bytes_to_send % MICROTCP_MSS));
            printf("sends a packet with seq_number: %d and ack_number: %d and data_len is %d\n", header_message->seq_number, header_message->ack_number, header_message->data_len);

            if (sendto(socket->sd, header_message, sizeof(microtcp_header_t) + MICROTCP_MSS, 0, socket->dest_address, socket->dests_address_len) < sizeof(header_message))
            {
                printf("[ERROR microtcp_send] sendto SEND sent less bytes than it should\n");
                free(header_message);
                return -1;
            }
            free(header_message);
        }

        int have_received_dup_ack_flag = 0;
        for (int i = 0; i < chunks; i++)
        {
            microtcp_header_t header_received;
            ssize_t bytes_received = recvfrom(socket->sd, &header_received, sizeof(header_received), 0, socket->dest_address, &socket->dests_address_len);
            // Timeout happened
            if (bytes_received < 0 && socket->state == SLOW_START)
            {
                socket->cwnd = min2(MICROTCP_MSS, socket->ssthresh);
                socket->ssthresh = 64000;
                socket->dup_acks_count = 0;
            }
            else if (bytes_received < 0)
            {
                socket->state = SLOW_START;
                socket->ssthresh = socket->cwnd / 2;
                socket->cwnd = min2(MICROTCP_MSS, socket->ssthresh);
                socket->dup_acks_count = 0;
            }
            // Check correctness of received
            if (header_received.ack_number == socket->last_received_ack)
            {
                socket->dup_acks_count++;
                have_received_dup_ack_flag = 1;
                if (socket->dup_acks_count == 3)
                {
                    // Enable 3 dup acks count mechanism fast recovery
                    socket->state = FAST_RECOVERY;
                    //...
                    remaining -= (socket->last_received_ack - first_seq_number);
                    data_sent += (socket->last_received_ack - first_seq_number);
                    socket->packets_lost++;
                    socket->bytes_lost += MICROTCP_MSS; // check
                    socket->ssthresh = socket->cwnd / 2;
                    socket->cwnd = socket->ssthresh + 1;
                    socket->dup_acks_count = 0;
                    break;
                }
            }
            else if (header_received.ack_number > socket->last_received_ack) // Everything goes well
            {
                socket->last_received_ack = header_received.ack_number;
                have_received_dup_ack_flag = 0;
                if (socket->state == SLOW_START && socket->cwnd >= socket->ssthresh) // Go to congestion avoidance
                {
                    socket->cwnd += MICROTCP_MSS;
                    socket->state = CONGESTION_AVOIDANCE;
                }
            }
            // Update cwnd if we have received plain acks... etc...
            if (socket->state == CONGESTION_AVOIDANCE && !have_received_dup_ack_flag)
            {
                socket->cwnd += MICROTCP_MSS * (MICROTCP_MSS / socket->cwnd);
                socket->dup_acks_count = 0;
            }
            if (socket->state == FAST_RECOVERY && !have_received_dup_ack_flag)
            {
                socket->cwnd += MICROTCP_MSS;
            }
            else if (socket->state == FAST_RECOVERY) // Have received new ACK and in FAST_RECOVERY and goes to CONGESTION_AVOIDANCE
            {
                socket->cwnd = socket->ssthresh;
            }
            if (socket->state == SLOW_START && !have_received_dup_ack_flag)
            {
                socket->cwnd += MICROTCP_MSS;
            }
            socket->packets_received++;
        }

        if (socket->state != FAST_RECOVERY)
        {
            remaining -= bytes_to_send;
            data_sent += bytes_to_send;
        }
    }

    printf("SEND TO OKEY\n");
    return 0;
}

ssize_t microtcp_recv(microtcp_sock_t *socket, void *buffer, size_t length, int flags)
{

    while (1)
    {
        uint8_t packet[sizeof(microtcp_header_t) + MICROTCP_MSS];
        ssize_t bytes_received = recvfrom(socket->sd, packet, sizeof(packet), 0, socket->dest_address, &socket->dests_address_len);
        printf("expects a packet has seq_number: %d and ack_number: %d\n", socket->seq_number, socket->ack_number);

        uint8_t *payload = packet + sizeof(microtcp_header_t);

        microtcp_header_t *header_received = (microtcp_header_t *)packet;
        // If FIN ACK
        if (((header_received->control & (1 << 15)) && (header_received->control & (1 << 12))))
        {
            // Check correctness of received
            int correct_sum = (1 << 12) | (1 << 15);
            printf("correct sum is %d\n", correct_sum);
            // if (!((header_received.control & (1 << 15)) && (header_received.control & (1 << 12))))
            // {
            //     printf("[ERROR microtcp_recv] Not received FIN ACK\n");
            // }
            // if (correct_sum != header_received.control)
            // {
            //     printf("[ERROR microtcp_recv] Wrong bits on control\n");
            // }
            uint32_t checksum_assigned_on_header_received = header_received->checksum;
            header_received->checksum = 0;
            if (checksum_assigned_on_header_received != crc32((const uint8_t *)&header_received, sizeof(header_received)))
            {
                printf("[ERROR microtcp_recv] Checksum is wrong\n");
                return 0;
            }
            socket->packets_received++;
            socket->state = CLOSING_BY_PEER;
            microtcp_shutdown(socket, 0);

            printf("[ERROR microtcp_recv] received FIN ACK and executed shutdown code\n");
        }
        else
        {
            // Check numbers
            if (socket->ack_number != header_received->seq_number)
            {
                printf("we wait for %d and receive %d\n", socket->ack_number, header_received->seq_number);

                printf("[ERROR microtcp_recv] seq_number received is wrong\n");

                // Update the socket
                // socket->ack_number = header_received.seq_number + header_received.data_len;

                // Send one of the 3 dup acks now
                // Create the ACK header
                microtcp_header_t header_message;
                header_message.seq_number = socket->seq_number;
                header_message.ack_number = socket->ack_number; // Check
                header_message.control = 1 << 12;
                header_message.data_len = 0;
                header_message.future_use0 = 0;
                header_message.future_use1 = 0;
                header_message.future_use2 = 0;
                header_message.checksum = 0;
                header_message.checksum = crc32((const uint8_t *)&header_message, sizeof(microtcp_header_t));

                socket->seq_number += 1;
                socket->packets_send++;

                if (sendto(socket->sd, &header_message, sizeof(header_message), 0, socket->dest_address, socket->dests_address_len) < sizeof(header_message))
                {
                    printf("[ERROR microtcp_recv] sendto SEND sent less bytes than it should\n");
                    return -1;
                }
                // return 0;
            }
            else // Everything went OK
            {
                // ACK number is correct
                // Copy the message only the data
                socket->buf_fill_level += header_received->data_len;
                memcpy((uint8_t *)buffer + socket->buf_fill_level, payload, header_received->data_len);
                socket->curr_win_size = socket->buf_fill_level;

                // printf("%d sockbufffilllevel\n", header_received->data_len);
                // fwrite((uint8_t *)buffer, 1, header_received->data_len, stdout);

                // printf("%s\n", &header_received + sizeof(microtcp_header_t));
                // Send back the ACK of the message received

                // Create the ACK header
                microtcp_header_t header_message;
                header_message.seq_number = socket->seq_number;
                header_message.ack_number = socket->ack_number; // Check
                header_message.control = 1 << 12;
                header_message.data_len = 0;
                header_message.future_use0 = 0;
                header_message.future_use1 = 0;
                header_message.future_use2 = 0;
                header_message.checksum = 0;
                header_message.window = socket->curr_win_size;

                // Update the socket
                socket->ack_number = header_received->seq_number + header_received->data_len;
                socket->seq_number += 1;
                header_message.checksum = crc32((const uint8_t *)&header_message, sizeof(microtcp_header_t));

                if (sendto(socket->sd, &header_message, sizeof(header_message), 0, socket->dest_address, socket->dests_address_len) < sizeof(header_message))
                {
                    printf("[ERROR microtcp_recv] sendto SEND sent less bytes than it should\n");
                    return -1;
                }

                printf("RECEIVE FROM EVERYTHING OKEY\n");
                // return 0;
            }
        }
    }
}
