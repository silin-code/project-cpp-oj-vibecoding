CREATE DATABASE IF NOT EXISTS oj_system
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

USE oj_system;

CREATE TABLE IF NOT EXISTS users (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    username    VARCHAR(64) UNIQUE NOT NULL,
    password    VARCHAR(256) NOT NULL,
    role        ENUM('user','admin') DEFAULT 'user',
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS problems (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    title       VARCHAR(256) NOT NULL,
    description TEXT NOT NULL,
    input_desc  TEXT,
    output_desc TEXT,
    difficulty  ENUM('easy','medium','hard') DEFAULT 'easy',
    time_limit  INT DEFAULT 2,
    memory_limit INT DEFAULT 256,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at  DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS testcases (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    problem_id  INT NOT NULL,
    input       TEXT NOT NULL,
    expected    TEXT NOT NULL,
    is_sample   BOOLEAN DEFAULT FALSE,
    sort_order  INT DEFAULT 0,
    FOREIGN KEY (problem_id) REFERENCES problems(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS submissions (
    id          INT PRIMARY KEY AUTO_INCREMENT,
    user_id     INT NOT NULL,
    problem_id  INT NOT NULL,
    code        TEXT NOT NULL,
    status      ENUM('PENDING','JUDGING','AC','WA','CE','RE','TLE','MLE') DEFAULT 'PENDING',
    failed_case INT DEFAULT NULL,
    error_msg   TEXT,
    time_used   INT DEFAULT NULL,
    memory_used INT DEFAULT NULL,
    created_at  DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id),
    FOREIGN KEY (problem_id) REFERENCES problems(id)
);

-- Root admin account (default password: see deployment docs)
INSERT IGNORE INTO users (username, password, role)
VALUES ('root', '$2b$12$rRLy3uV8Hoq1aKm6SWiNAOD4AK.YT1Z7lgFemj0eRmUhXrSW6r8rO', 'admin');
