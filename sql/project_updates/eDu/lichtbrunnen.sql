DELETE FROM `creature_template_addon` WHERE `entry` IN (31883, 31897, 31896, 31895, 31894, 31893);
INSERT INTO `creature_template_addon` VALUES ('31883', '0', '0', '0', '0', '0', '0', null, '59907 0');
INSERT INTO `creature_template_addon` VALUES ('31897', '0', '0', '0', '0', '0', '0', null, '59907 0');
INSERT INTO `creature_template_addon` VALUES ('31896', '0', '0', '0', '0', '0', '0', null, '59907 0');
INSERT INTO `creature_template_addon` VALUES ('31895', '0', '0', '0', '0', '0', '0', null, '59907 0');
INSERT INTO `creature_template_addon` VALUES ('31894', '0', '0', '0', '0', '0', '0', null, '59907 0');
INSERT INTO `creature_template_addon` VALUES ('31893', '0', '0', '0', '0', '0', '0', null, '59907 0');

DELETE FROM `npc_spellclick_spells` WHERE `npc_entry` IN 31897, 31896, 31895, 31894, 31893, 31883);
INSERT INTO `npc_spellclick_spells` VALUES ('31897', '60123', '0', '0', '0', '2');
INSERT INTO `npc_spellclick_spells` VALUES ('31896', '60123', '0', '0', '0', '2');
INSERT INTO `npc_spellclick_spells` VALUES ('31895', '60123', '0', '0', '0', '2');
INSERT INTO `npc_spellclick_spells` VALUES ('31894', '60123', '0', '0', '0', '2');
INSERT INTO `npc_spellclick_spells` VALUES ('31893', '60123', '0', '0', '0', '2');
INSERT INTO `npc_spellclick_spells` VALUES ('31883', '60123', '0', '0', '0', '2');