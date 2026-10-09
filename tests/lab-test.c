
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "harness/unity.h"
#include "../src/lab.h"

void setUp(void) {
}

void tearDown(void) {
}

// Test the Internet checksum
void test_checksum(void) {
    uint8_t data[] = {
        0x00, 0x01, 0xf2, 0x03,
        0xf4, 0xf5, 0xf6, 0xf7
    };

    TEST_ASSERT_EQUAL_HEX16(0x220d, checksum(data, 8));

    // Test an odd number of bytes
    uint8_t odd[] = {0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL_HEX16(0xfbfd, checksum(odd, 3));
}

// Test encoding and decoding packets
void test_packet_encoding(void) {
    Packet original = {0};
    original.type = DATA;
    original.seq = 2;
    original.length = 3;
    memcpy(original.payload, "Hi!", 3);

    uint8_t buffer[MAX_PACKET];

    int size = encode_packet(&original, buffer);
    TEST_ASSERT_EQUAL_INT(13, size);

    Packet decoded = {0};

    TEST_ASSERT_EQUAL_INT(0,
        decode_packet(buffer, (size_t)size, &decoded));

    TEST_ASSERT_EQUAL_INT(DATA, decoded.type);
    TEST_ASSERT_EQUAL_UINT32(2, decoded.seq);
    TEST_ASSERT_EQUAL_INT(3, decoded.length);
    TEST_ASSERT_EQUAL_MEMORY("Hi!", decoded.payload, 3);
}

// Test damaged and invalid packets
void test_invalid_packets(void) {
    Packet packet = {0};
    packet.type = DATA;
    packet.length = 3;
    memcpy(packet.payload, "ABC", 3);

    uint8_t buffer[MAX_PACKET];
    int size = encode_packet(&packet, buffer);

    Packet result = {0};

    // Packet is too short
    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(buffer, 5, &result));

    // Incorrect packet length
    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(buffer, (size_t)(size - 1), &result));

    // Corrupt one byte
    buffer[10] ^= 1;
    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(buffer, (size_t)size, &result));
    buffer[10] ^= 1;

    // Unknown packet type
    buffer[0] = 5;
    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(buffer, (size_t)size, &result));
    buffer[0] = DATA;

    // Reserved byte must be zero
    buffer[1] = 1;
    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(buffer, (size_t)size, &result));

    // Payload cannot exceed 1024 bytes
    packet.length = 1025;
    TEST_ASSERT_EQUAL_INT(-1,
        encode_packet(&packet, buffer));
}

// Test ACK and FIN packets
void test_control_packets(void) {
    Packet packet = {0};
    Packet decoded = {0};
    uint8_t buffer[MAX_PACKET];

    packet.type = ACK;
    packet.seq = 3;

    int size = encode_packet(&packet, buffer);
    TEST_ASSERT_EQUAL_INT(10, size);
    TEST_ASSERT_EQUAL_INT(0,
        decode_packet(buffer, (size_t)size, &decoded));
    TEST_ASSERT_EQUAL_INT(ACK, decoded.type);
    TEST_ASSERT_EQUAL_UINT32(3, decoded.seq);

    // FIN has no payload
    packet.type = FIN;
    packet.seq = 5;

    size = encode_packet(&packet, buffer);
    TEST_ASSERT_EQUAL_INT(10, size);
    TEST_ASSERT_EQUAL_INT(0,
        decode_packet(buffer, (size_t)size, &decoded));
    TEST_ASSERT_EQUAL_INT(FIN, decoded.type);
}

// Test the sender's window
void test_sender_window(void) {
    Sender sender;
    sender_init(&sender, 4, 250);

    TEST_ASSERT_EQUAL_UINT32(0, sender.base);
    TEST_ASSERT_EQUAL_UINT32(0, sender.next);
    TEST_ASSERT_TRUE(sender_can_send(&sender));

    // Fill the window
    sender.next = 4;
    TEST_ASSERT_FALSE(sender_can_send(&sender));

    // An ACK opens the window
    sender_ack(&sender, 2, 100);

    TEST_ASSERT_EQUAL_UINT32(2, sender.base);
    TEST_ASSERT_TRUE(sender_can_send(&sender));

    // Duplicate ACK should be ignored
    sender_ack(&sender, 2, 200);
    TEST_ASSERT_EQUAL_UINT32(2, sender.base);

    // ACK cannot go beyond sent packets
    sender_ack(&sender, 10, 300);
    TEST_ASSERT_EQUAL_UINT32(2, sender.base);

    // All packets acknowledged
    sender_ack(&sender, 4, 400);
    TEST_ASSERT_EQUAL_UINT32(4, sender.base);
    TEST_ASSERT_EQUAL_INT64(-1, sender.timer_start);
}

// Test sender timeouts
void test_sender_timeout(void) {
    Sender sender;
    sender_init(&sender, 4, 250);

    sender.next = 2;
    sender.timer_start = 100;

    // Timer has not expired
    TEST_ASSERT_EQUAL_INT(0,
        sender_timeout(&sender, 200));

    // Timer expires
    TEST_ASSERT_EQUAL_INT(1,
        sender_timeout(&sender, 350));

    TEST_ASSERT_EQUAL_INT(1, sender.retries);

    // Give up after 10 timeouts
    int result = 0;

    for (int i = 0; i < 9; i++) {
        result = sender_timeout(
            &sender, sender.timer_start + 250);
    }

    TEST_ASSERT_EQUAL_INT(-1, result);
}

// Test in-order receiving
void test_receiver(void) {
    Receiver receiver;
    receiver_init(&receiver);

    Packet packet = {0};
    packet.type = DATA;
    packet.seq = 0;

    TEST_ASSERT_EQUAL_INT(1,
        receiver_packet(&receiver, &packet));
    TEST_ASSERT_EQUAL_UINT32(1, receiver.expected);

    // Duplicate packet
    TEST_ASSERT_EQUAL_INT(0,
        receiver_packet(&receiver, &packet));
    TEST_ASSERT_EQUAL_UINT32(1, receiver.expected);

    // Out-of-order packet
    packet.seq = 3;
    TEST_ASSERT_EQUAL_INT(0,
        receiver_packet(&receiver, &packet));

    // Correct next packet
    packet.seq = 1;
    TEST_ASSERT_EQUAL_INT(1,
        receiver_packet(&receiver, &packet));
    TEST_ASSERT_EQUAL_UINT32(2, receiver.expected);

    // FIN ends the transfer
    packet.type = FIN;
    packet.seq = 2;

    TEST_ASSERT_EQUAL_INT(2,
        receiver_packet(&receiver, &packet));
    TEST_ASSERT_TRUE(receiver.finished);
    TEST_ASSERT_EQUAL_UINT32(3, receiver.expected);

    // Repeated FIN
    TEST_ASSERT_EQUAL_INT(0,
        receiver_packet(&receiver, &packet));
}

// Additional tests for missing coverage
void test_missing_coverage(void) {
    Packet packet = {0};
    Packet decoded = {0};
    uint8_t buffer[MAX_PACKET];

    // ACK packets cannot contain a payload
    packet.type = ACK;
    packet.length = 1;

    TEST_ASSERT_EQUAL_INT(-1,
        encode_packet(&packet, buffer));

    // Decode a packet with an invalid control payload
    packet.type = DATA;
    packet.length = 1;
    packet.payload[0] = 'A';

    int size = encode_packet(&packet, buffer);
    TEST_ASSERT_EQUAL_INT(11, size);

    buffer[0] = ACK;

    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(buffer, (size_t)size, &decoded));

    // Timeout with no active timer
    Sender sender;
    sender_init(&sender, 4, 250);

    TEST_ASSERT_EQUAL_INT(0,
        sender_timeout(&sender, 1000));

    // Timeout when no packets are outstanding
    sender.next = 2;
    sender.base = 2;
    sender.timer_start = 100;

    TEST_ASSERT_EQUAL_INT(0,
        sender_timeout(&sender, 1000));

    // Receiver should ignore ACK packets
    Receiver receiver;
    receiver_init(&receiver);

    packet.type = ACK;
    packet.seq = 0;
    packet.length = 0;

    TEST_ASSERT_EQUAL_INT(0,
        receiver_packet(&receiver, &packet));
}

// Test remaining validation branches
void test_extra_branches(void) {
    Packet packet = {0};
    Packet decoded = {0};
    uint8_t buffer[MAX_PACKET] = {0};

    // Check NULL inputs
    TEST_ASSERT_EQUAL_INT(-1,
        encode_packet(NULL, buffer));
    TEST_ASSERT_EQUAL_INT(-1,
        encode_packet(&packet, NULL));

    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(NULL, 10, &decoded));
    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(buffer, 10, NULL));

    // Reject unknown packet types
    packet.type = 5;
    TEST_ASSERT_EQUAL_INT(-1,
        encode_packet(&packet, buffer));

    // Test the maximum allowed payload
    packet.type = DATA;
    packet.length = MAX_PAYLOAD;
    memset(packet.payload, 'A', MAX_PAYLOAD);

    int size = encode_packet(&packet, buffer);

    TEST_ASSERT_EQUAL_INT(MAX_PACKET, size);
    TEST_ASSERT_EQUAL_INT(0,
        decode_packet(buffer, (size_t)size, &decoded));

    TEST_ASSERT_EQUAL_UINT16(MAX_PAYLOAD, decoded.length);

    // Reject oversized packets
    uint8_t oversized[MAX_PACKET + 1] = {0};

    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(oversized, sizeof(oversized), &decoded));

    // Reject invalid declared payload lengths
    buffer[8] = 0x04;
    buffer[9] = 0x01;

    TEST_ASSERT_EQUAL_INT(-1,
        decode_packet(buffer, (size_t)size, &decoded));

    // FIN packets cannot contain data
    packet.type = FIN;
    packet.length = 1;

    TEST_ASSERT_EQUAL_INT(-1,
        encode_packet(&packet, buffer));
}

// Run all tests
int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_checksum);
    RUN_TEST(test_packet_encoding);
    RUN_TEST(test_invalid_packets);
    RUN_TEST(test_control_packets);
    RUN_TEST(test_sender_window);
    RUN_TEST(test_sender_timeout);
    RUN_TEST(test_receiver);
    RUN_TEST(test_missing_coverage);
    RUN_TEST(test_extra_branches);

    return UNITY_END();
}
