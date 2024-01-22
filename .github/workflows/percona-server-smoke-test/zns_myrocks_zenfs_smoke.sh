#!/bin/bash

set -e
set -x

DEV=$1
if [ ! -b "/dev/$DEV" ]; then
            echo "/dev/$DEV does not exist."
            exit 1
fi

# Copy debian packages to the results dir so it can be collected later
cd /percona/work
tar czf /percona/work/results/unofficial-experimental-zenfs-percona-server-deb-packages.tgz *.deb
cd -

# Createing the ZenFS device
chown mysql:mysql /dev/$DEV
sudo -H -u mysql zenfs mkfs --zbd=$DEV --aux_path=/tmp/mysql_zenfs_aux_$DEV --finish_threshold=0 --force

# Restarting mysql with RocksDB - ZenFS enabled
service mysql stop

sudo rm -r /var/lib/mysql
sudo mkdir /var/lib/mysql
chown -R mysql:mysql /var/lib/mysql
echo "loose-rocksdb-fs-uri=zenfs://dev:$DEV" >> /etc/mysql/mysql.conf.d/mysqld.cnf
echo "plugin-load-add=rocksdb=ha_rocksdb.so" >> /etc/mysql/mysql.conf.d/mysqld.cnf
echo "MySQL config:"
cat /etc/mysql/mysql.conf.d/mysqld.cnf

mysqld --initialize-insecure --user mysql
service mysql restart
mysql -u root -e "SET GLOBAL default_storage_engine=ROCKSDB;" -S /var/run/mysqld/mysqld.sock
sleep 5

# Smoketest workload
mysql -u root -e "create database sbtest;"
mysql -u root -e "show engines;"
mysql -u root -e "show databases;"
sudo /percona/work/sysbench/src/lua/oltp_write_only.lua --db-driver=mysql --mysql-user=root --time=0 --create_secondary=off --mysql-host=localhost --mysql-db=sbtest --mysql-storage-engine=rocksdb --table-size=100000 --tables=4 --threads=4 --report-interval=5 prepare

# Make sure that rocksdb's files end up on the ZNS device
sudo -H -u mysql zenfs list --path=.rocksdb --zbd=$DEV
sudo -H -u mysql zenfs list --path=.rocksdb --zbd=$DEV | grep IDENTITY
