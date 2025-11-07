# Traffic Light
## 🚀 Giới thiệu
- Dự án này là thiết kế module điều khiển mô hình đèn giao thông. Mô hình có 14 cột đèn, mỗi cột có 3 led đơn xanh, đỏ, vàng. Loại led 12v.
- Yêu cầu sử dụng Arduino IDE, Visual Studio Code, Altium làm hệ thống build dự án.  
- Chip sử dụng esp32-s3 mini, version board esp32 trên Arduino IDE là 2.0.12
- Code được viết bằng **Arduino IDE** và chạy trên board điều khiển tự thiết kế.
[Cập nhật version mới nhất qua](https://github.com/QDung888/TrafficLight_internship/tree/master)

## 🛠️ Cấu hình môi trường
- **Board**: esp32-s3 mini version board esp32 2.0.15
- **Framework**: Arduino IDE
- **Ngôn ngữ**: C/C++

## 📦 Thư viện sử dụng
Project sử dụng các thư viện chính sau:
- **Wifi** –  WiFi SoftAP cho ESP32 version 1.2.7
- **WebServer** – HTTP server nhúng (port 80) version 3.8.1
- **Wire** – I2C dùng cho PCF8575
- **PCF8575** – Dùng thư viện của xreef V2.0.1
- **SPIFFS** – Lưu các file trong thư mục data vào flash. Link video hướng dẫn cài tool: https://youtu.be/9i1nDUoDRcI?si=-pUQmOpcrhJP6nr6  . Link tải: https://github.com/me-no-dev/arduino-esp32fs-plugin
- **ArduinoJson** – Parse/serialize JSON cho HTTP/Serial version 7.4.2
- **MD5Builder** – Tạo MD5


## 📂 Cấu trúc dự án
Project/    
    ├── Image
    ├── Schematic
    ├── Sourcecode/
        └── Source code
            ├── Sourcode.ino     #code firmware 
            ├── data/       # data để lưu vào flash của esp32
                    ├── Index.html  # Giao diện webapp
                    ├── script.js   # Thực thi chắc năng webapp
                    ├── style.css   # phong cách của webapp
                    ├── map.PNG
                    ├── traffic_base.PNG
                    ├── traffic_green.PNG
                    ├── traffic_yellow.PNG
                    ├── traffic_red.PNG       
        └──esp32_pkt_tool.py  # công cụ tạo file JSON

## ⚙️ Cách cài đặt
- Hướng dẫn cài tool SPIFFS: https://youtu.be/9i1nDUoDRcI?si=-pUQmOpcrhJP6nr6  

## ⬆️ Hướng dẫn upload firmware
- Arduino IDE cài đặt các thư viện cần thiết, và tool để up file quá SPIFFS theo video hướng dẫn ở trên. Biên dịch và nạp code
- Chọn board esp32s3-devmodule
- Chọn COM phù hợp sau đó nhấn upload
Lưu ý:
- Phiên bản esp32 2.0.12
- Thư viện PCF8575 của XREEF. Link tải: https://github.com/xreef/PCF8575_library


