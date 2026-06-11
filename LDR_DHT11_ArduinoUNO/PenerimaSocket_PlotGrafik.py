#!/usr/bin/env python3
import tkinter as tk
from tkinter import ttk, messagebox
import socket
import threading
import sys

# Pustaka Plotter
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

class AppPenerimaTCP:
    def __init__(self, root):
        self.root = root
        self.root.title("Penerima Data Stream TCP - Grafik Plotter")
        self.root.geometry("900x550")
        
        # Variabel Status
        self.is_listening = False
        self.server_socket = None
        self.worker_thread = None
        
        # Array Data Grafik. Maksimal 10 elemen.
        self.data_temp = []
        self.data_humi = []
        self.sumbu_x = []
        self.counter_x = 0
        
        # Variabel Antarmuka (Thread-safe)
        self.var_lux = tk.StringVar(value="0")
        self.var_temp = tk.StringVar(value="0 °C")
        self.var_humi = tk.StringVar(value="0 %")
        self.var_local_ip = tk.StringVar()
        
        self.var_local_ip.set(self.dapatkan_ip_lokal())
        self.buat_antarmuka()

    def dapatkan_ip_lokal(self):
        """
        Fungsi deteksi IP.
        Deteksi interface wlan0 untuk Linux. Fallback IP default untuk OS lain.
        """
        if sys.platform.startswith('linux'):
            try:
                import fcntl
                import struct
                s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                return socket.inet_ntoa(fcntl.ioctl(
                    s.fileno(), 0x8915, struct.pack('256s', b'wlan0'[:15])
                )[20:24])
            except Exception:
                pass
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            return "127.0.0.1"

    def buat_antarmuka(self):
        """
        Fungsi pembuat tata letak GUI.
        Dibagi 3 frame utama. Atas untuk jaringan. Tengah untuk teks. Bawah untuk visual.
        """
        # --- FRAME ATAS: JARINGAN ---
        frame_net = ttk.LabelFrame(self.root, text=" Konfigurasi Data Stream Socket TCP ")
        frame_net.pack(fill="x", padx=10, pady=5)
        
        ttk.Label(frame_net, text="IP Perangkat Anda (wlan0):").grid(row=0, column=0, sticky="w", padx=5, pady=2)
        ttk.Entry(frame_net, textvariable=self.var_local_ip, state="readonly").grid(row=0, column=1, sticky="w", padx=5, pady=2)
        
        ttk.Label(frame_net, text="IP & Port Pengirim TCP:").grid(row=1, column=0, sticky="w", padx=5, pady=2)
        self.ent_bind = ttk.Entry(frame_net, width=20)
        self.ent_bind.insert(0, f"{self.var_local_ip.get()}:2222")
        self.ent_bind.grid(row=1, column=1, sticky="w", padx=5, pady=2)
        
        self.btn_tcp = ttk.Button(frame_net, text="Perbarui Pengirim TCP", command=self.kontrol_server_tcp)
        self.btn_tcp.grid(row=1, column=2, padx=10)

        # --- FRAME TENGAH: DATA TEKS ---
        frame_teks = ttk.LabelFrame(self.root, text=" Data Real-Time ")
        frame_teks.pack(fill="x", padx=10, pady=5)
        
        ttk.Label(frame_teks, text="Intensitas Cahaya :").grid(row=0, column=0, sticky="w", padx=10, pady=5)
        ttk.Label(frame_teks, textvariable=self.var_lux, font=('Helvetica', 12, 'bold')).grid(row=0, column=1, sticky="w", padx=10)
        
        ttk.Label(frame_teks, text="Suhu Lingkungan    :").grid(row=1, column=0, sticky="w", padx=10, pady=5)
        ttk.Label(frame_teks, textvariable=self.var_temp, font=('Helvetica', 12, 'bold')).grid(row=1, column=1, sticky="w", padx=10)
        
        ttk.Label(frame_teks, text="Kelembapan Udara :").grid(row=2, column=0, sticky="w", padx=10, pady=5)
        ttk.Label(frame_teks, textvariable=self.var_humi, font=('Helvetica', 12, 'bold')).grid(row=2, column=1, sticky="w", padx=10)

        # --- FRAME BAWAH: VISUALISASI ---
        frame_visual = ttk.Frame(self.root)
        frame_visual.pack(fill="both", expand=True, padx=10, pady=5)
        
        # Kiri: Indikator Cahaya (Canvas)
        frame_kiri = ttk.LabelFrame(frame_visual, text=" Indikator Lux ")
        frame_kiri.pack(side="left", fill="y", padx=5)
        
        self.canvas_lux = tk.Canvas(frame_kiri, width=150, height=150, bg="#f0f0f0", highlightthickness=0)
        self.canvas_lux.pack(padx=20, pady=30)
        # Gambar lingkaran awal warna hitam
        self.lingkaran = self.canvas_lux.create_oval(25, 25, 125, 125, fill="#000000", outline="gray")

        # Tengah & Kanan: Grafik Plotter
        frame_grafik = ttk.LabelFrame(frame_visual, text=" Grafik Plotter Historis ")
        frame_grafik.pack(side="left", fill="both", expand=True, padx=5)
        
        self.inisialisasi_grafik(frame_grafik)

    def inisialisasi_grafik(self, parent):
        """
        Fungsi inisialisasi Matplotlib.
        Pisahkan pembuatan figur dan kanvas. Hindari kebocoran memori.
        """
        self.fig, (self.ax_temp, self.ax_humi) = plt.subplots(1, 2, figsize=(7, 3), dpi=100)
        
        self.ax_temp.set_title("Suhu (°C)")
        self.ax_temp.set_ylim(0, 50)
        self.ax_temp.grid(True)
        
        self.ax_humi.set_title("Kelembapan (%)")
        self.ax_humi.set_ylim(0, 100)
        self.ax_humi.grid(True)
        
        self.fig.tight_layout()
        
        self.canvas_plot = FigureCanvasTkAgg(self.fig, master=parent)
        self.canvas_plot.get_tk_widget().pack(fill="both", expand=True)

    def kalkulasi_warna_lux(self, nilai_lux):
        """
        Fungsi interpolasi warna RGB.
        Rentang 0 (Hitam) hingga 4000 (Kuning).
        """
        try:
            lux = float(nilai_lux)
        except ValueError:
            lux = 0
            
        batas = min(max(lux, 0), 4000) # Pastikan rentang 0-4000
        rasio = batas / 4000.0
        
        # Kuning = Merah (255) + Hijau (255). Biru (0).
        r = int(255 * rasio)
        g = int(255 * rasio)
        
        # Konversi desimal RGB ke format hex HTML
        return f"#{r:02x}{g:02x}00"

    def kontrol_server_tcp(self):
        """
        Fungsi kontrol koneksi TCP Server.
        Tutup soket lama sebelum membuka koneksi baru. Jalankan di thread terpisah.
        """
        raw_bind = self.ent_bind.get().strip()
        if ":" not in raw_bind:
            messagebox.showwarning("Error", "Gunakan format IP:PORT")
            return
            
        ip, port = raw_bind.split(":")
        
        # Hentikan server lama jika berjalan
        self.is_listening = False
        if self.server_socket:
            try:
                self.server_socket.close()
            except Exception:
                pass
                
        # Mulai server baru
        self.is_listening = True
        self.worker_thread = threading.Thread(target=self.proses_terima_tcp, args=(ip, int(port)), daemon=True)
        self.worker_thread.start()
        
        self.btn_tcp.config(text="Menunggu Koneksi...")

    def proses_terima_tcp(self, ip, port):
        """
        WORKER THREAD: Server Socket.
        Menerima koneksi klien Arduino. Menangani paket string. Menjadwalkan update GUI.
        """
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.bind((ip, port))
            self.server_socket.listen(5)
            self.server_socket.settimeout(1.0) # Timeout agar loop tidak terkunci
            
            # Gunakan lambda untuk kembali ke main thread Tkinter saat update tombol
            self.root.after(0, lambda: self.btn_tcp.config(text="Server Aktif (Ubah IP/Port)"))
            
            while self.is_listening:
                try:
                    conn, addr = self.server_socket.accept()
                    data = conn.recv(1024).decode('utf-8').strip()
                    conn.close()
                    
                    if data:
                        self.urai_paket_data(data)
                except socket.timeout:
                    continue
                except Exception:
                    break
        except Exception as e:
            self.root.after(0, lambda: messagebox.showerror("Gagal Bind", f"Kesalahan TCP:\n{e}"))
            self.root.after(0, lambda: self.btn_tcp.config(text="Perbarui Pengirim TCP"))
            self.is_listening = False

    def urai_paket_data(self, data):
        """
        Fungsi pemisah protokol string.
        Contoh data: DATALUX:100;TEMP:30;HUMI:50
        """
        try:
            bagian = data.split(";")
            if len(bagian) >= 3:
                lux_val = bagian[0].split(":")[1]
                temp_val = bagian[1].split(":")[1]
                humi_val = bagian[2].split(":")[1]
                
                # Jadwalkan fungsi pembaruan antarmuka pada thread utama (Tkinter)
                self.root.after(0, self.perbarui_antarmuka, lux_val, temp_val, humi_val)
        except Exception:
            pass

    def perbarui_antarmuka(self, lux, temp, humi):
        """
        Fungsi perbarui visualisasi.
        Perbarui label teks, warna lingkaran, dan tambahkan data ke array grafik.
        Dipanggil dari dalam event loop Tkinter.
        """
        # Perbarui Teks
        self.var_lux.set(f"{lux} Lux")
        self.var_temp.set(f"{temp} °C")
        self.var_humi.set(f"{humi} %")
        
        # Perbarui Warna Lingkaran
        warna_baru = self.kalkulasi_warna_lux(lux)
        self.canvas_lux.itemconfig(self.lingkaran, fill=warna_baru)
        
        # Perbarui Array Grafik (Maksimal 10 Data)
        self.counter_x += 1
        self.sumbu_x.append(self.counter_x)
        self.data_temp.append(float(temp))
        self.data_humi.append(float(humi))
        
        if len(self.sumbu_x) > 10:
            self.sumbu_x.pop(0)
            self.data_temp.pop(0)
            self.data_humi.pop(0)
            
        # Gambar Ulang Grafik
        self.ax_temp.clear()
        self.ax_humi.clear()
        
        self.ax_temp.set_title("Suhu (°C)")
        self.ax_temp.set_ylim(0, 50)
        self.ax_temp.grid(True)
        self.ax_temp.plot(self.sumbu_x, self.data_temp, marker='o', color='red')
        
        self.ax_humi.set_title("Kelembapan (%)")
        self.ax_humi.set_ylim(0, 100)
        self.ax_humi.grid(True)
        self.ax_humi.plot(self.sumbu_x, self.data_humi, marker='o', color='blue')
        
        self.canvas_plot.draw()

if __name__ == "__main__":
    root_window = tk.Tk()
    aplikasi = AppPenerimaTCP(root_window)
    root_window.mainloop()
