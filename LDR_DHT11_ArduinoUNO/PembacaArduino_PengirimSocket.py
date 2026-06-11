#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk, messagebox
import serial
import socket
import threading
import time
import sys

class AppAkusisiData:
    def __init__(self, root):
        self.root = root
        # Ubah judul aplikasi utama.
        self.root.title("Akusisi Data Sensor Arduino - Pengirim Data Socket TCP")
        self.root.geometry("500x450")
        
        self.serial_conn = None
        self.tcp_client = None
        self.is_reading = False
        
        self.var_lux = tk.StringVar(value="0 Lux")
        self.var_temp = tk.StringVar(value="0 °C")
        self.var_humi = tk.StringVar(value="0 %")
        self.var_local_ip = tk.StringVar()
        
        self.var_local_ip.set(self.dapatkan_ip_lokal())
        self.buat_antarmuka()

    def dapatkan_ip_lokal(self):
        """
        Fungsi deteksi IP jaringan.
        Mencari alamat IP aktif pada antarmuka wlan0 (Linux/Raspberry Pi).
        Gunakan fallback soket standar untuk Windows.
        """
        if sys.platform.startswith('linux'):
            try:
                import fcntl
                import struct
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                return socket.inet_ntoa(fcntl.ioctl(
                    s.fileno(),
                    0x8915, 
                    struct.pack('256s', b'wlan0'[:15])
                )[20:24])
            except Exception:
                pass
                
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip_aktif = s.getsockname()[0]
            s.close()
            return ip_aktif
        except Exception:
            return "127.0.0.1"

    def buat_antarmuka(self):
        """
        Fungsi pembuat antarmuka pengguna (GUI).
        Gunakan pendekatan modular dengan memisahkan area aplikasi menjadi frame.
        """
        # Frame Serial
        frame_serial = ttk.LabelFrame(self.root, text=" Konfigurasi Serial Arduino ")
        frame_serial.pack(fill="x", padx=15, pady=10)
        
        ttk.Label(frame_serial, text="Port:").pack(side="left", padx=5, pady=5)
        
        default_port = "/dev/ttyUSB0" if sys.platform.startswith('linux') else "COM3"
        self.ent_port = ttk.Entry(frame_serial, width=15)
        self.ent_port.insert(0, default_port)
        self.ent_port.pack(side="left", padx=5, pady=5)
        
        self.btn_serial = ttk.Button(frame_serial, text="Hubungkan", command=self.kontrol_koneksi_serial)
        self.btn_serial.pack(side="left", padx=5, pady=5)

        # Frame Data Sensor
        # Label "Terkalibrasi" dihapus sesuai instruksi.
        frame_sensor = ttk.LabelFrame(self.root, text=" Data Real-Time ")
        frame_sensor.pack(fill="x", padx=15, pady=10)
        
        ttk.Label(frame_sensor, text="Intensitas Cahaya :").grid(row=0, column=0, sticky="w", padx=10, pady=8)
        ttk.Label(frame_sensor, textvariable=self.var_lux, font=('Helvetica', 12, 'bold')).grid(row=0, column=1, sticky="w", padx=10)
        
        ttk.Label(frame_sensor, text="Suhu Lingkungan    :").grid(row=1, column=0, sticky="w", padx=10, pady=8)
        ttk.Label(frame_sensor, textvariable=self.var_temp, font=('Helvetica', 12, 'bold')).grid(row=1, column=1, sticky="w", padx=10)
        
        ttk.Label(frame_sensor, text="Kelembapan Udara :").grid(row=2, column=0, sticky="w", padx=10, pady=8)
        ttk.Label(frame_sensor, textvariable=self.var_humi, font=('Helvetica', 12, 'bold')).grid(row=2, column=1, sticky="w", padx=10)

        # Frame Jaringan TCP
        frame_net = ttk.LabelFrame(self.root, text=" Penerusan Data TCP Socket ")
        frame_net.pack(fill="both", expand=True, padx=15, pady=10)
        
        ttk.Label(frame_net, text="IP Perangkat Anda:").pack(anchor="w", padx=10, pady=2)
        ent_local_ip = ttk.Entry(frame_net, textvariable=self.var_local_ip, state="readonly", background="#f0f0f0")
        ent_local_ip.pack(fill="x", padx=10, pady=2)
        
        ttk.Label(frame_net, text="IP & Port Tujuan TCP Server (Format IP:PORT):").pack(anchor="w", padx=10, pady=2)
        self.ent_target_tcp = ttk.Entry(frame_net)
        self.ent_target_tcp.insert(0, "192.168.43.10:2222")
        self.ent_target_tcp.pack(fill="x", padx=10, pady=2)
        
        self.btn_tcp = ttk.Button(frame_net, text="Perbarui Target TCP", command=self.konfigurasi_socket_tcp)
        self.btn_tcp.pack(pady=10)

    def kontrol_koneksi_serial(self):
        """
        Fungsi kontrol komunikasi serial.
        Mulai thread baru untuk membaca data agar antarmuka tidak hang.
        """
        if not self.is_reading:
            try:
                port_target = self.ent_port.get().strip()
                self.serial_conn = serial.Serial(port_target, 9600, timeout=1)
                self.is_reading = True
                self.btn_serial.config(text="Putuskan")
                
                self.thread_baca = threading.Thread(target=self.prosedur_pembacaan_serial, daemon=True)
                self.thread_baca.start()
            except Exception as e:
                messagebox.showerror("Gagal Serial", f"Tidak dapat membuka port.\n{e}")
        else:
            self.is_reading = False
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
            self.btn_serial.config(text="Hubungkan")

    def konfigurasi_socket_tcp(self):
        """
        Fungsi pengaturan parameter TCP.
        Validasi format penulisan IP dan Port dari pengguna.
        """
        raw_target = self.ent_target_tcp.get().strip()
        if ":" not in raw_target:
            messagebox.showwarning("Format Salah", "Gunakan format IP:PORT")
            return
        
        try:
            ip, port = raw_target.split(":")
            int(port)
            messagebox.showinfo("Berhasil", "Target TCP diperbarui.")
        except ValueError:
            messagebox.showerror("Error", "Port harus angka valid.")

    def prosedur_pembacaan_serial(self):
        """
        Fungsi utama pengolahan string data.
        Membaca data serial. Memisahkan string berdasarkan pemisah. 
        Memperbarui tampilan. Mengirim data mentah ke soket TCP.
        """
        while self.is_reading:
            if self.serial_conn and self.serial_conn.is_open:
                try:
                    if self.serial_conn.in_waiting > 0:
                        raw_line = self.serial_conn.readline().decode('utf-8', errors='ignore').strip()
                        
                        if not raw_line or ":" not in raw_line:
                            continue
                            
                        # Pisahkan label dan data
                        _, data_part = raw_line.split(":", 1)
                        data_part = data_part.strip()
                        
                        # Ekstrak variabel
                        variabel_split = data_part.split(",")
                        if len(variabel_split) >= 3:
                            lux_raw = variabel_split[0].strip()
                            temp_raw = variabel_split[1].strip()
                            humi_raw = variabel_split[2].strip()
                            
                            # Tampilkan langsung ke GUI. Tidak ada perhitungan kalibrasi di Python.
                            self.var_lux.set(f"{lux_raw} Lux")
                            self.var_temp.set(f"{temp_raw} °C")
                            self.var_humi.set(f"{humi_raw} %")
                            
                            # Teruskan data asli serial ke koneksi TCP.
                            self.kirim_paket_tcp(lux_raw, temp_raw, humi_raw)
                            
                except Exception:
                    pass
            time.sleep(0.1)

    def kirim_paket_tcp(self, lux, temp, humi):
        """
        Fungsi pengiriman data jaringan.
        Membuka koneksi soket. Mengirim paket string. Menutup koneksi.
        """
        try:
            target_raw = self.ent_target_tcp.get().strip()
            if ":" in target_raw:
                ip, port = target_raw.split(":")
                
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(0.8)
                
                sock.connect((ip, int(port)))
                
                # Format payload menyesuaikan data serial yang sudah dikalibrasi di Arduino.
                payload = f"DATALUX:{lux};TEMP:{temp};HUMI:{humi}\n"
                sock.sendall(payload.encode('utf-8'))
                sock.close()
        except Exception:
            pass

if __name__ == "__main__":
    root_window = tk.Tk()
    aplikasi = AppAkusisiData(root_window)
    root_window.mainloop()
