-- Iteration 2: fitness profile table for the AI fitness coach system.
-- One row stores the current editable fitness profile for one user.

CREATE TABLE IF NOT EXISTS fitness_profile (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    user_id BIGINT NOT NULL,
    gender VARCHAR(20) DEFAULT '',
    age INT DEFAULT NULL,
    height_cm DECIMAL(5,2) DEFAULT NULL,
    weight_kg DECIMAL(5,2) DEFAULT NULL,
    goal VARCHAR(100) DEFAULT '',
    training_level VARCHAR(50) DEFAULT '',
    weekly_days INT DEFAULT NULL,
    equipment VARCHAR(255) DEFAULT '',
    injury_note TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_fitness_profile_user_id (user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
