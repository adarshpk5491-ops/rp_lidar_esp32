#!/usr/bin/env python3
"""
RPLIDAR A2 M8 Data Reader
Reads and decodes real-time 360° LIDAR scan data
"""

import serial
import struct
import time
from collections import defaultdict

class RPLidarA2Reader:
    def __init__(self, port='COM5', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.buffer = bytearray()
        self.scan_data = defaultdict(lambda: {'angle': 0, 'distance': 0, 'quality': 0})
        
    def connect(self):
        """Connect to LIDAR serial port"""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=1)
            print(f"✓ Connected to {self.port} at {self.baudrate} baud")
            time.sleep(0.5)
            return True
        except Exception as e:
            print(f"✗ Failed to connect: {e}")
            return False
    
    def read_packet(self):
        """Read and parse one complete RPLIDAR packet"""
        # Skip any non-binary garbage (boot messages, etc.)
        while len(self.buffer) < 1000:
            data = self.ser.read(256)
            if not data:
                return None
            self.buffer.extend(data)
        
        # Find sync pattern (0xA5 0x5A)
        while len(self.buffer) >= 2:
            # Check for text garbage (boot messages start with printable ASCII)
            if self.buffer[0] > 0x7F or (self.buffer[0] < 0x20 and self.buffer[0] not in [0xA5, 0x0D, 0x0A]):
                # This looks like binary header start
                if self.buffer[0] == 0xA5 and len(self.buffer) > 1 and self.buffer[1] == 0x5A:
                    break
            
            # Skip garbage bytes
            self.buffer.pop(0)
        
        if len(self.buffer) < 2 or self.buffer[0] != 0xA5 or self.buffer[1] != 0x5A:
            return None
        
        # Read header (need at least 7 bytes: sync + size + type + checksum)
        while len(self.buffer) < 7:
            data = self.ser.read(256)
            if not data:
                return None
            self.buffer.extend(data)
        
        # Parse header
        sync1, sync2 = self.buffer[0], self.buffer[1]
        size_l, size_h = self.buffer[2], self.buffer[3]
        packet_type = self.buffer[4]
        
        packet_size = size_l | (size_h << 8)
        
        # Validate packet size (A2 M8 packets are typically 5, 40, 81 bytes)
        if packet_size == 0 or packet_size > 512:
            self.buffer.pop(0)
            return None
        
        # Read full payload
        total_needed = 5 + packet_size  # sync(2) + size(2) + type(1) + payload
        while len(self.buffer) < total_needed:
            data = self.ser.read(256)
            if not data:
                return None
            self.buffer.extend(data)
        
        # Extract packet
        packet = bytes(self.buffer[:total_needed])
        self.buffer = self.buffer[total_needed:]
        
        return {
            'sync': (sync1, sync2),
            'size': packet_size,
            'type': packet_type,
            'data': packet[5:5+packet_size]
        }
    
    def parse_scan_packet(self, packet):
        """Parse scan data packet (type 0x81)"""
        if packet['type'] != 0x81:
            return []
        
        measurements = []
        data = packet['data']
        
        # Each measurement is 5 bytes
        for i in range(0, len(data), 5):
            if i + 4 >= len(data):
                break
            
            byte0 = data[i]
            angle_lo = data[i+1]
            angle_hi = data[i+2]
            dist_lo = data[i+3]
            dist_hi = data[i+4]
            
            # Extract fields
            quality = (byte0 >> 1) & 0x7F  # Bits 1-7
            start_bit = byte0 & 0x01        # Bit 0
            
            # Combine angle (in 1/64 degree units)
            angle_raw = angle_lo | (angle_hi << 8)
            angle_deg = angle_raw / 64.0
            
            # Combine distance (in mm)
            distance_mm = dist_lo | (dist_hi << 8)
            
            measurements.append({
                'quality': quality,
                'start': start_bit,
                'angle': angle_deg,
                'distance': distance_mm
            })
        
        return measurements
    
    def run(self):
        """Main loop - read and display scan data"""
        if not self.connect():
            return
        
        print("\n" + "="*70)
        print("RPLIDAR A2 M8 - Real-time Data Reader")
        print("="*70)
        print(f"{'Angle':>8} | {'Dist(mm)':>8} | {'Quality':>7} | Points")
        print("-"*70)
        
        packet_count = 0
        point_count = 0
        last_print = time.time()
        no_packet_count = 0
        
        try:
            while True:
                packet = self.read_packet()
                if not packet:
                    no_packet_count += 1
                    if no_packet_count % 100 == 0:
                        print(f"[DEBUG] Waiting for packets... (buffer: {len(self.buffer)} bytes)")
                    continue
                
                no_packet_count = 0
                packet_count += 1
                
                # Debug: Show packet info
                if packet_count <= 3:
                    print(f"[DEBUG] Got packet #{packet_count}: type=0x{packet['type']:02X}, size={packet['size']}")
                
                measurements = self.parse_scan_packet(packet)
                
                if measurements:
                    # Display every 20th measurement
                    for m in measurements:
                        point_count += 1
                        if point_count % 20 == 0:
                            print(f"{m['angle']:>8.2f}° | {m['distance']:>8} | {m['quality']:>7} | {point_count}")
                
                # Summary every 2 seconds
                now = time.time()
                if now - last_print > 2.0:
                    rate = point_count / (now - last_print) if (now - last_print) > 0 else 0
                    print(f"\n[INFO] {packet_count} packets | {point_count} points @ {rate:.0f} Hz")
                    print("-"*70)
                    point_count = 0
                    last_print = now
                    
        except KeyboardInterrupt:
            print("\n\n✓ Reader stopped")
        finally:
            if self.ser:
                self.ser.close()
                print("✓ Port closed")

if __name__ == '__main__':
    reader = RPLidarA2Reader(port='COM5', baudrate=115200)
    reader.run()
