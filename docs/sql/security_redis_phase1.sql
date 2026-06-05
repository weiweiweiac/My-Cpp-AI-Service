ALTER TABLE users
    ADD COLUMN password_hash VARCHAR(255) DEFAULT '',
    ADD COLUMN password_salt VARCHAR(255) DEFAULT '';
