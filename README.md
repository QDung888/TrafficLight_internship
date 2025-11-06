TRANG GITHUB QUẢN LÝ CODE ĐIỀU KHIỂN MÔ HÌNH ĐÈN GIAO THÔNG
Thành phần dự án
1. Phần cứng: esp32-s3, PCF8575, ULN2803, PCF8575
2. Sourcecode
3. Tool tạo lệnh JSON điều khiển mô hình qua Serial


Hướng dẫn sử dụng git

Tạo 1 dự án trên github (tạo 1 project trống trên github sau đó clone về bắt đầu làm)

B1: Tạo 1 Repositories trên github để chứa code dự án

B2: Cài đặt git trên PC và clone Repositories trên github về git clone your_link_repo

B3: code

git status: trang thai file
git add . : them file
git commit -m "your_message" : tao noi dung commit
git push origin main : day code len github
Tạo 1 project local và đẩy lên github (dành cho project đã có sẵn đẩy lên github)

B1: tạo 1 github repo

B2: git init : để tạo .git tại thư mục chứa project trong máy, theo dõi code trong project

B3: viết code

git status
git add .
git commit -m "your_mesage"
git remote add origin https://....(link đến repo github)
git push origin master

