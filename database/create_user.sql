-- IMPORTANT: Replace <your_password> below and update config/config.json to match
CREATE USER IF NOT EXISTS 'oj_user'@'localhost' IDENTIFIED BY '<your_password>';
GRANT ALL PRIVILEGES ON oj_system.* TO 'oj_user'@'localhost';
FLUSH PRIVILEGES;
