-- Iteration 9A: user roles, free AI quota, and AI usage logs.
-- MySQL 5.7 does not support ALTER TABLE ... ADD COLUMN IF NOT EXISTS,
-- so this script uses INFORMATION_SCHEMA + prepared statements to stay idempotent.

SET @users_table := 'users';

SET @sql := (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE `users` ADD COLUMN `role` VARCHAR(20) NOT NULL DEFAULT ''user''',
        'SELECT 1'
    )
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = @users_table
      AND COLUMN_NAME = 'role'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql := (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE `users` ADD COLUMN `ai_quota_total` INT NOT NULL DEFAULT 5',
        'SELECT 1'
    )
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = @users_table
      AND COLUMN_NAME = 'ai_quota_total'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @sql := (
    SELECT IF(
        COUNT(*) = 0,
        'ALTER TABLE `users` ADD COLUMN `ai_quota_used` INT NOT NULL DEFAULT 0',
        'SELECT 1'
    )
    FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE()
      AND TABLE_NAME = @users_table
      AND COLUMN_NAME = 'ai_quota_used'
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

CREATE TABLE IF NOT EXISTS `ai_usage_log` (
    `id` BIGINT PRIMARY KEY AUTO_INCREMENT,
    `user_id` BIGINT NOT NULL,
    `endpoint` VARCHAR(100) NOT NULL,
    `model_type` VARCHAR(50) DEFAULT '',
    `quota_consumed` TINYINT(1) NOT NULL DEFAULT 0,
    `success` TINYINT(1) NOT NULL DEFAULT 1,
    `error_message` TEXT,
    `created_at` TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    KEY `idx_ai_usage_user_created` (`user_id`, `created_at`),
    KEY `idx_ai_usage_endpoint` (`endpoint`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Set an administrator manually when needed:
-- UPDATE users SET role='admin' WHERE username='你的用户名';
