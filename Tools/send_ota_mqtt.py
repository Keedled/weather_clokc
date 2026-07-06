#!/usr/bin/env python3
import argparse
import binascii
import socket
import struct
import sys
import time


MQTT_CONNECT = 0x10
MQTT_CONNACK = 0x20
MQTT_PUBLISH = 0x30
MQTT_SUBSCRIBE = 0x82
MQTT_SUBACK = 0x90
MQTT_DISCONNECT = 0xE0


def mqtt_string(text):
    data = text.encode("utf-8")
    if len(data) > 0xFFFF:
        raise ValueError("MQTT string is too long")
    return struct.pack("!H", len(data)) + data


def encode_remaining_length(length):
    out = bytearray()
    while True:
        byte = length % 128
        length //= 128
        if length > 0:
            byte |= 0x80
        out.append(byte)
        if length == 0:
            break
    return bytes(out)


def send_packet(sock, packet_type, payload):
    sock.sendall(bytes([packet_type]) + encode_remaining_length(len(payload)) + payload)


def read_exact(sock, length, deadline):
    data = bytearray()
    while len(data) < length:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("timed out while reading MQTT packet")
        sock.settimeout(min(remaining, 1.0))
        try:
            chunk = sock.recv(length - len(data))
        except socket.timeout:
            continue
        if not chunk:
            raise ConnectionError("MQTT broker closed the connection")
        data.extend(chunk)
    return bytes(data)


def read_packet(sock, timeout_sec):
    deadline = time.monotonic() + timeout_sec
    first = read_exact(sock, 1, deadline)[0]

    multiplier = 1
    remaining_length = 0
    while True:
        byte = read_exact(sock, 1, deadline)[0]
        remaining_length += (byte & 0x7F) * multiplier
        if (byte & 0x80) == 0:
            break
        multiplier *= 128
        if multiplier > 128 * 128 * 128:
            raise ValueError("malformed MQTT remaining length")

    payload = read_exact(sock, remaining_length, deadline)
    return first, payload


class TinyMqttClient:
    def __init__(self, host, port, client_id, ack_topic, timeout_sec):
        self.host = host
        self.port = port
        self.client_id = client_id
        self.ack_topic = ack_topic
        self.timeout_sec = timeout_sec
        self.sock = None
        self.packet_id = 1

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout_sec)

        variable_header = mqtt_string("MQTT") + bytes([4, 2]) + struct.pack("!H", 60)
        payload = mqtt_string(self.client_id)
        send_packet(self.sock, MQTT_CONNECT, variable_header + payload)

        packet_type, payload = read_packet(self.sock, self.timeout_sec)
        if packet_type != MQTT_CONNACK or len(payload) != 2:
            raise RuntimeError("broker did not return a valid CONNACK")
        if payload[1] != 0:
            raise RuntimeError(f"broker rejected CONNECT, return code {payload[1]}")

    def subscribe_ack(self):
        packet_id = self._next_packet_id()
        payload = struct.pack("!H", packet_id) + mqtt_string(self.ack_topic) + bytes([0])
        send_packet(self.sock, MQTT_SUBSCRIBE, payload)

        packet_type, response = read_packet(self.sock, self.timeout_sec)
        if packet_type != MQTT_SUBACK or len(response) < 3:
            raise RuntimeError("broker did not return a valid SUBACK")
        response_id = struct.unpack("!H", response[:2])[0]
        if response_id != packet_id or response[-1] == 0x80:
            raise RuntimeError("broker rejected ACK topic subscription")

    def publish_text(self, topic, message):
        payload = mqtt_string(topic) + message.encode("utf-8")
        send_packet(self.sock, MQTT_PUBLISH, payload)

    def wait_ack(self, predicate, description, failure_prefixes):
        deadline = time.monotonic() + self.timeout_sec
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"timed out waiting for {description}")

            try:
                packet_type, payload = read_packet(self.sock, remaining)
            except TimeoutError as exc:
                raise TimeoutError(f"timed out waiting for {description}") from exc
            if (packet_type & 0xF0) != MQTT_PUBLISH:
                continue

            topic_len = struct.unpack("!H", payload[:2])[0]
            topic_start = 2
            topic_end = topic_start + topic_len
            topic = payload[topic_start:topic_end].decode("utf-8", errors="replace")
            message = payload[topic_end:].decode("utf-8", errors="replace")

            if topic != self.ack_topic:
                continue

            print(f"ack: {message}")
            for prefix in failure_prefixes:
                if message.startswith(prefix):
                    raise RuntimeError(f"device reported failure while waiting for {description}: {message}")

            if predicate(message):
                return message

    def disconnect(self):
        if self.sock is None:
            return
        try:
            send_packet(self.sock, MQTT_DISCONNECT, b"")
        except OSError:
            pass
        try:
            self.sock.close()
        finally:
            self.sock = None

    def _next_packet_id(self):
        packet_id = self.packet_id
        self.packet_id += 1
        if self.packet_id > 0xFFFF:
            self.packet_id = 1
        return packet_id


def parse_args():
    parser = argparse.ArgumentParser(description="Send STM32 OTA firmware through MQTT with ACK flow control.")
    parser.add_argument("--bin", dest="bin_path", required=True, help="raw firmware .bin path")
    parser.add_argument("--host", default="10.59.125.248", help="MQTT broker host")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker port")
    parser.add_argument("--command-topic", default="topic1", help="device command topic")
    parser.add_argument("--ack-topic", default="weather_clock/zgt6_001/ack", help="device ACK topic")
    parser.add_argument("--chunk-size", type=int, default=128, help="binary chunk size, max 128")
    parser.add_argument("--delay-ms", type=int, default=0, help="extra delay after each accepted chunk")
    parser.add_argument("--ack-timeout-sec", type=float, default=15.0, help="ACK timeout per command")
    parser.add_argument("--version", type=int, default=0, help="firmware version number")
    parser.add_argument("--client-id", default="", help="MQTT client id; default auto-generates one")
    return parser.parse_args()


def bytes_to_hex(data):
    return data.hex().upper()


def main():
    args = parse_args()

    if args.chunk_size < 1 or args.chunk_size > 128:
        raise ValueError("--chunk-size must be in range 1..128")
    if args.delay_ms < 0:
        raise ValueError("--delay-ms cannot be negative")
    if args.ack_timeout_sec <= 0:
        raise ValueError("--ack-timeout-sec must be positive")

    with open(args.bin_path, "rb") as f:
        firmware = f.read()

    size = len(firmware)
    crc = binascii.crc32(firmware) & 0xFFFFFFFF
    crc_text = f"{crc:08X}"
    client_id = args.client_id or f"zgt6-ota-{int(time.time())}"

    print(f"broker: {args.host}:{args.port}")
    print(f"command topic: {args.command_topic}")
    print(f"ack topic: {args.ack_topic}")
    print(f"file: {args.bin_path}")
    print(f"size: {size}")
    print(f"crc32: {crc_text}")
    print(f"chunk: {args.chunk_size} bytes")

    client = TinyMqttClient(args.host, args.port, client_id, args.ack_topic, args.ack_timeout_sec)
    try:
        print("mqtt: connecting")
        client.connect()
        print("mqtt: connected")
        client.subscribe_ack()
        print("mqtt: subscribed ACK topic")

        client.publish_text(args.command_topic, f"ota_begin:{size}:{crc_text}:{args.version}")
        print("sent: ota_begin")
        client.wait_ack(lambda msg: msg == "ota_begin_ok",
                        "ota_begin_ok",
                        ("ota_begin_fail", "ota_begin_bad"))

        offset = 0
        while offset < size:
            count = min(args.chunk_size, size - offset)
            expected = offset + count
            chunk_hex = bytes_to_hex(firmware[offset:expected])

            client.publish_text(args.command_topic, f"ota_chunk:{offset}:{chunk_hex}")
            client.wait_ack(lambda msg, expected=expected: msg == f"ota_chunk_ok:{expected}",
                            f"ota_chunk_ok:{expected}",
                            ("ota_chunk_fail", "ota_chunk_bad"))

            offset = expected
            print(f"progress: {offset}/{size}")

            if args.delay_ms > 0:
                time.sleep(args.delay_ms / 1000.0)

        client.publish_text(args.command_topic, "ota_end")
        print("sent: ota_end")
        client.wait_ack(lambda msg: msg == f"ota_ready:{size}:{crc_text}",
                        f"ota_ready:{size}:{crc_text}",
                        ("ota_end_fail",))

        print(f"Done. Device reported ota_ready:{size}:{crc_text}.")
        return 0
    finally:
        client.disconnect()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
