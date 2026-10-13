-- Apply manually to the Skyfire world database. Skyfire uses RBAC permission,
-- not AzerothCore's security column. No global creature-template changes.
DELETE FROM `command` WHERE `name` IN ('cfbg','cfbg race','cfbg debug','bf list');
INSERT INTO `command` (`name`,`permission`,`help`) VALUES
('cfbg',496,'Cross-faction battleground commands.'),
('cfbg race',496,'Syntax: .cfbg race <race|random>. Choose the opposite-faction appearance for your next PvP entry.'),
('cfbg debug',304,'Syntax: .cfbg debug [online player]. Show native/current team, appearance and Wintergrasp assignment.'),
('bf list',257,'Syntax: .bf list <battleId>. Show zone, queue, invitation and war counts.');
