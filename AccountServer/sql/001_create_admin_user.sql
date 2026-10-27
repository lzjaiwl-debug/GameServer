-- 超级管理员表（wuxia_account）
-- 用途：
-- 1) 管理后台账号基础信息
-- 2) 存储登录码验签用 device_secret（与 U 盘 key 配套）

USE `wuxia_account`;

CREATE TABLE IF NOT EXISTS `admin_user` (
  `admin_id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '主键ID',
  `username` VARCHAR(64) NOT NULL COMMENT '显示用户名（保留原始大小写）',
  `username_lc` VARCHAR(64) NOT NULL COMMENT '用户名小写（用于唯一检索）',
  `nickname` VARCHAR(64) NOT NULL DEFAULT '' COMMENT '昵称',
  `password_hash` VARCHAR(512) NOT NULL DEFAULT '' COMMENT '密码PHC串（可选，建议存储）',
  `password_algo` VARCHAR(32) NOT NULL DEFAULT '' COMMENT '密码算法名（argon2id/pbkdf2-sha256）',
  `device_secret` VARCHAR(128) NOT NULL DEFAULT '' COMMENT '设备密钥（登录码HMAC密钥，建议加密保存）',
  `status` TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '状态：1正常 2禁用',
  `role_mask` BIGINT UNSIGNED NOT NULL DEFAULT 1 COMMENT '权限位掩码',
  `last_login_ip` VARCHAR(45) NOT NULL DEFAULT '' COMMENT '最后登录IP',
  `created_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '创建时间',
  `updated_at` DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3) COMMENT '更新时间',
  PRIMARY KEY (`admin_id`),
  UNIQUE KEY `uk_admin_user_username_lc` (`username_lc`),
  KEY `idx_admin_user_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci COMMENT='后台管理员账号表';

-- 可选：初始化一个超管（请自行替换 password_hash 与 device_secret）
-- INSERT INTO `admin_user` (`username`, `username_lc`, `nickname`, `password_hash`, `password_algo`, `device_secret`, `status`, `role_mask`)
-- VALUES ('admin', 'admin', 'SuperAdmin', '<PHC_HASH>', 'argon2id', '<DEVICE_SECRET_BASE64>', 1, 18446744073709551615);
