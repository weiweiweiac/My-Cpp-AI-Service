-- Iteration 3: per-exercise training records for the AI fitness coach system.
-- Multiple rows can be stored for one user and one training date.

CREATE TABLE IF NOT EXISTS training_record (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    user_id BIGINT NOT NULL,
    calendar_id BIGINT DEFAULT NULL,
    record_date DATE NOT NULL,
    exercise_name VARCHAR(100) NOT NULL,
    weight_kg DECIMAL(6,2) DEFAULT NULL,
    reps INT DEFAULT NULL,
    sets INT DEFAULT 1,
    rpe DECIMAL(3,1) DEFAULT NULL,
    rir DECIMAL(3,1) DEFAULT NULL,
    rest_seconds INT DEFAULT NULL,
    duration_minutes INT DEFAULT NULL,
    completed TINYINT(1) NOT NULL DEFAULT 0,
    feeling_note TEXT,
    sort_order INT DEFAULT 0,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    KEY idx_training_record_user_date (user_id, record_date),
    KEY idx_training_record_calendar_id (calendar_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
