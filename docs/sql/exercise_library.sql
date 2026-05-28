-- Iteration 9B: exercise library foundation.
-- System exercises use user_id = 0. Custom exercises use the real logged-in user_id.

CREATE TABLE IF NOT EXISTS exercise_library (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    user_id BIGINT NOT NULL DEFAULT 0,
    name VARCHAR(100) NOT NULL,
    category VARCHAR(50) DEFAULT '',
    primary_muscle VARCHAR(100) DEFAULT '',
    secondary_muscles VARCHAR(255) DEFAULT '',
    equipment VARCHAR(100) DEFAULT '',
    difficulty VARCHAR(50) DEFAULT '',
    description TEXT,
    tips TEXT,
    is_system TINYINT(1) NOT NULL DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_exercise_user_name (user_id, name),
    KEY idx_exercise_user_id (user_id),
    KEY idx_exercise_name (name),
    KEY idx_exercise_category (category),
    KEY idx_exercise_primary_muscle (primary_muscle),
    KEY idx_exercise_is_system (is_system)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
