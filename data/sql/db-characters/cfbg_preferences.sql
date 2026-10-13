-- Apply manually to the Skyfire characters database. No character race/team is changed.
CREATE TABLE IF NOT EXISTS `mod_cfbg_preferences` (
  `guid` INT UNSIGNED NOT NULL,
  `race` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=random; preference only',
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
