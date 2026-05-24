-- Iteration 3: training calendar table for the AI fitness coach system.
-- One row stores one user's main plan/status for one calendar date.

CREATE TABLE IF NOT EXISTS fitness_calendar (
    id BIGINT PRIMARY KEY AUTO_INCREMENT,
    user_id BIGINT NOT NULL,
    calendar_date DATE NOT NULL,
    item_type VARCHAR(30) NOT NULL DEFAULT 'plan',
    title VARCHAR(100) DEFAULT '',
    plan_content MEDIUMTEXT,
    status VARCHAR(30) NOT NULL DEFAULT 'planned',
    model_type VARCHAR(50) DEFAULT '',
    profile_snapshot TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_calendar_user_date (user_id, calendar_date),
    KEY idx_calendar_user_date (user_id, calendar_date)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
