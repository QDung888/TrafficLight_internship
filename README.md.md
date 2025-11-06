Trang git quan ly code trang web dieu khien, giao dien dieu khien va code vi xu ly esp32 cho mo hinh den giao thong



Thành phần dự án:

1. Phần cứng điều khiển mạch
2. sourcecode của esp32
3. tool tạo lệnh JSON điều khiển mô hình qua serial



Hướng dẫn dùng git



Tạo 1 dự án trên github (tạo 1 project trống trên github sau đó clone về bắt đầu làm)



B1: Tạo 1 Repositories trên github để chứa code dự án



B2: Cài đặt git trên PC và clone Repositories trên github về git clone your\_link\_repo



B3: code



git status: trang thai file

git add . : them file

git commit -m "your\_message" : tao noi dung commit

git push origin main : day code len github



Tạo 1 project local và đẩy lên github (dành cho project đã có sẵn đẩy lên github)



B1: tạo 1 github repo



B2: git init : để tạo .git tại thư mục chứa project trong máy, theo dõi code trong project



B3: viết code



git status

git add .

git commit -m "your\_mesage"

git remote add origin https://....(link đến repo github)

git push origin master



