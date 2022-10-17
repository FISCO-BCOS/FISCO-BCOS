# Fisco-Bcos教程系列

作者:	深职院-麦立健

第一弹：[fisco-bcos搭建视频教程.md by MaiLiJian-CN · Pull Request #65 · FISCO-BCOS/FISCO-BCOS-SIG (github.com)](https://github.com/FISCO-BCOS/FISCO-BCOS-SIG/pull/65)

这一期我们的视频教程是继之前的Fisco-Bcos搭建单群组四节点和部署控制台后来手动部署WeBase子系统【WeBase-Front，WeBase-Node-Manager,WeBase-Web,WeBase-Sign】

第二弹：[WeBase全家桶部署_哔哩哔哩_bilibili](https://www.bilibili.com/video/BV1AB4y1G7t9?vd_source=41e9aeb43a1f59237fe0dfb439c3c148)

------

在此视频中需使用Mysql服务，但并没有在视频中给出安装Mysql服务的详细步骤,因而在此补上。如果您已经安装Mysql或有把握自己能熟悉掌握Mysql下载安装，则可忽略下文，直接观看视频。

第一步

下载数据库

```powershell
sudo apt install mysql-server
sudo apt install mysql-client
#查看是否安装成功
mysql --version
```

第二步 登录

```powershell
sudo mysql -uroot -p123456
#如果登录失败
sudo vim /etc/mysql/my.cnf
#输入以下配置,免密登录
[mysqld]
skip-grant-tables
#保存退出
systemctl restart mysql
sudo mysql -uroot -p
#回车登录
```
进入mysql

```sql

use mysql;
select User,Host,Plugin,authentication_string from user;
```
![1661562801810](https://user-images.githubusercontent.com/102428352/187008560-f67a9db2-21be-42b4-bd52-be095c899fe7.jpg)

```sql
#先无脑刷新一波
flush privileges;
update user set Host='%' where user='root';
flush privileges;
#清空原密码
update user set authentication_string=’’ where user=‘root’;
#修改加密规则和更新密码
ALTER USER 'root'@'%' IDENTIFIED BY '自身密码' PASSWORD EXPIRE NEVER;
ALTER USER 'root'@'%' IDENTIFIED WITH mysql_native_password BY '自身密码';
flush privileges;
exit;
```

```bash
#最后检验一下
sudo vim /etc/mysql/my.cnf
#注释之前的免密配置,保存退出
sudo vim /etc/mysql/mysql.conf.d/mysqld.cnf
#注释掉bind-address,保存退出;
systemctl restart mysql;
mysql -uroot -p
#输入密码,成功登录.
```
![13390be83fa04b3887eda9e3ae2ca719](https://user-images.githubusercontent.com/102428352/187008602-3586e832-3e85-4ca6-b8ea-98081e10ac81.png)
