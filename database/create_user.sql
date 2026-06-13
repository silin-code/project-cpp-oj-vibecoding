CREATE USER IF NOT EXISTS 'oj_user'@'localhost' IDENTIFIED BY 'oj_pass_2026';
GRANT ALL PRIVILEGES ON oj_system.* TO 'oj_user'@'localhost';
FLUSH PRIVILEGES;
