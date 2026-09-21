#ifndef PAL_H
#define PAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_PLAYER_ROLES 6
#define MAX_PLAYABLE_PLAYER_ROLES 5
#define MAX_PLAYER_MAGICS 32
#define NUM_MAGIC_ELEMENTAL 5
#define MAX_PLAYER_EQUIPMENTS 6
#define MAX_INVENTORY 256
#define MAX_OBJECTS 600
#define MAX_SCENES 300
#define MAX_POISONS 16
#define MAX_STATUS_SLOTS 16
#define MAX_ENEMIES_IN_TEAM 5

enum {
  STATUS_SPEED = 0,
  STATUS_SLOW,
  STATUS_POISON,
  STATUS_PARALYZED,
  STATUS_CONFUSED,
  STATUS_SLEEP,
  STATUS_WEAK,
  STATUS_DEATH,
  STATUS_BRAVERY,
  STATUS_ALL
};

typedef struct {
  int16_t vanishTime;
  int16_t x, y;
  int16_t layer;
  uint16_t triggerScript;
  uint16_t autoScript;
  int16_t state;
  int16_t triggerMode;
  int16_t spriteNum;
  int16_t spriteFrames;
  int16_t direction;
  int16_t currentFrame;
  int16_t scriptIdleFrame;
  int16_t spritePtrOffset;
  int16_t spriteFramesAuto;
  int16_t scriptIdleFrameAuto;
} event_object_t;

typedef struct {
  uint16_t mapNum;
  uint16_t scriptOnEnter;
  uint16_t scriptOnLeave;
  int16_t eventObjectIndex;
} scene_t;

typedef struct {
  uint16_t data[7];
} object_t;

typedef struct {
  uint16_t item;
  int16_t amount;
  int16_t amountInUse;
} inventory_t;

typedef struct {
  int16_t idleFrames;
  int16_t magicFrames;
  int16_t attackFrames;
  int16_t idleAnimSpeed;
  int16_t actWaitFrames;
  int16_t yPosOffset;
  int16_t attackSound;
  int16_t actionSound;
  int16_t magicSound;
  int16_t deathSound;
  int16_t callSound;
  int16_t health;
  int16_t exp;
  int16_t cash;
  int16_t level;
  int16_t magic;
  int16_t magicRate;
  int16_t attackEquivItem;
  int16_t attackEquivItemRate;
  int16_t stealItem;
  int16_t stealItemCount;
  int16_t attackStrength;
  int16_t magicStrength;
  int16_t defense;
  int16_t dexterity;
  uint16_t fleeRate;
  int16_t poisonResistance;
  uint16_t elemResistance[NUM_MAGIC_ELEMENTAL];
  int16_t physicalResistance;
  int16_t dualMove;
  int16_t collectValue;
} enemy_t;

typedef struct {
  int16_t effect;
  int16_t type;
  int16_t xOffset;
  int16_t yOffset;
  int16_t layerOffset;
  int16_t speed;
  int16_t keepEffect;
  int16_t fireDelay;
  int16_t effectTimes;
  int16_t shake;
  int16_t wave;
  uint16_t unknown;
  int16_t costMP;
  int16_t baseDamage;
  uint16_t elemental;
  int16_t sound;
} magic_t;

typedef struct {
  uint16_t level;
  uint16_t magic;
} levelup_magic_t;

typedef struct {
  levelup_magic_t m[MAX_PLAYABLE_PLAYER_ROLES];
} levelup_magic_all_t;

typedef struct {
  uint16_t x, y;
} pos_t;

typedef struct {
  pos_t pos[5 * 5];
} enemy_pos_t;

#define enemy_pos(team, enemy) enemy_pos_data.pos[(enemy) * 5 + (team)]

typedef struct {
  int16_t role;
  int16_t x, y;
  int16_t frame;
  int16_t imageOffset;
} party_t;

typedef struct {
  int16_t x, y;
  int16_t direction;
} trail_t;

typedef struct {
  float exp;
  int16_t level;
  int16_t count;
} experience_t;

typedef struct {
  experience_t primary[MAX_PLAYER_ROLES];
  experience_t health[MAX_PLAYER_ROLES];
  experience_t magic[MAX_PLAYER_ROLES];
  experience_t attack[MAX_PLAYER_ROLES];
  experience_t magicPower[MAX_PLAYER_ROLES];
  experience_t defense[MAX_PLAYER_ROLES];
  experience_t dexterity[MAX_PLAYER_ROLES];
  experience_t flee[MAX_PLAYER_ROLES];
} all_experience_t;

typedef struct {
  int16_t poisonID;
  uint16_t poisonScript;
} poison_status_t;

typedef struct {
  int16_t spriteNum;
  int16_t x, y;
  int16_t animOffset;
  int16_t origX, origY;
  int16_t direction;
  int16_t origDirection;
  int16_t spriteBase;
  int16_t actionState;
  int16_t objectID;
  int16_t equipID;
} battle_sprite_t;

typedef struct {
  int16_t enemyID;
  int16_t x, y;
  int16_t origX, origY;
  int16_t direction;
  int16_t flag;
  int16_t mapValue;
  int16_t tileData;
  int16_t hp;
  int16_t objectID;
  uint16_t useScript;
  uint16_t battleStartScript;
  uint16_t battleEndScript;
  int16_t reserved;
} enemy_battle_t;

typedef struct {
  uint16_t effect;
  uint16_t frames;
} battle_effect_t;

typedef struct {
  int16_t target;
  int16_t actionType;
  int16_t itemID;
  int16_t invIndex;
  int16_t flag;
} battle_action_t;

typedef struct {
  int16_t x, y;
  int16_t number;
  int16_t colorType;
  int16_t timer;
} damage_number_t;

typedef event_object_t npc_display_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int16_t retval;

extern int32_t tmp_file_size;
extern uint32_t menu_bg_size;

extern int32_t fh_M_MSG, fh_MGO_MKF, fh_F_MKF, fh_ABC_MKF, fh_RNG_MKF;
extern int32_t fh_temp, data_mkf_handle;

extern int16_t music_mode_init, music_mode, use_cd_flag;
extern int16_t has_sfx, has_sfx_raw;
extern int16_t midi_active, midi_playing;
extern int16_t cd_active, cd_track_valid, cd_track_num;
extern int16_t music_track_arg, music_mode_arg;

extern int16_t palette_fade_active, mutex_shaking;
extern intptr_t screen_buffer_ptr;
extern int16_t shake_active, shake_intensity, shake_duration;

extern int16_t dialog_text_x, dialog_text_y;
extern int16_t image_draw_x, image_draw_y, image_lookup_key, image_draw_flag;
extern int16_t dialog_width, dialog_height, dialog_type, dialog_x, dialog_y;
extern int16_t dialog_color, menu_cursor_pos, dialog_y_pos;

extern int16_t rpg_to_load, max_save_number, RPG_save_number;
extern int16_t curr_scene_id_cache, RPG_other_peoples;
extern int16_t RPG_viewport_x, RPG_viewport_y;
extern int16_t RPG_team_number, RPG_curr_scene, scene_to_load;
extern int16_t RPG_color_begin_ptr, RPG_team_direction;
extern int16_t viewport_flags, viewport_flags_2;
extern int16_t x_off, y_off, team_abstract_x, team_abstract_y;
extern int16_t RPG_music_number, RPG_battle_music_number, RPG_battle_scene_number;
extern uint32_t RPG_money;
extern int16_t party_abs_x, party_abs_y, RPG_role_locate_layer;
extern int16_t scene_leave_flag, scene_enter_flag, scene_flags;
extern uint32_t sss_subfile_count;
extern int16_t event_object_count, curr_scene_event_count;
extern int16_t RPG_screen_wave_grade, wave_progression;
extern int16_t npc_dir_frame, curr_npc_idx, npc_curr_frame, npc_direction;
extern int16_t npc_frame_base, npc_sprite_num;
extern int16_t redraw_flag, fade_step_count, redraw_hp_mp_flag;
extern int16_t battle_viewport_x, battle_viewport_y;
extern int16_t RPG_ememy_chase_rate, RPG_change_chaserate_times;
extern int16_t battle_enemy_idx, effect_frame_count;
extern int16_t in_battle_action, battle_param;
extern int16_t battle_dest_x;
extern int16_t blow_away_flag, battle_y_offset;
extern int16_t relative_viewport_x, relative_viewport_y;
extern int16_t viewport_scroll_x, viewport_scroll_y;
extern int16_t viewport_x_bak, viewport_y_bak;
extern int16_t viewport_row_count, viewport_row_stride, viewport_row_limit;
extern int16_t frame_counter, key_pressed;
extern int16_t battle_extra_flag, battle_sub_flag;
extern int16_t RPG_current_calabash_number;

extern int16_t battle_role_idx_2, battle_curr_role_idx, battle_target_cursor;
extern int16_t battle_select_max, battle_select_idx;
extern int16_t battle_action_param;
extern uint32_t battle_action_type;
extern int16_t enemy_max_id, battle_max_hp_sum, enemy_pos_count, team_number;
extern int16_t battle_extra_param, battle_enemy_hp;
extern int16_t magic_select_tmp;
extern int16_t magic_select_idx;
extern int16_t effect_sub_count, effect_particle_count;
extern int16_t flag_battling, auto_battle_flag, attack_done_flag;
extern int16_t coop_magic_tmp, damage_target_hp;
extern int16_t theurgy_effect_count, theurgy_effect_max;
extern int16_t multi_event_state, multi_event_param;
extern int16_t multi_event_param_2, multi_event_param_3;
extern int16_t sprite_frame_count;

extern int16_t flag_trigger, flag_key_updown, flag_parallel_mutex;
extern int16_t mutex_can_change_palette;

extern uint8_t key_state_array[];
extern uint16_t key_pressed_flags[], key_direction_flags[];
extern uint16_t key_repeat_delay[];
extern int16_t fh_M_MSG_global[];
extern uint16_t key_scan_map[], key_action_map[];
extern uint8_t key_scan_codes[];

extern int16_t exit_flag, init_done_flag;
extern intptr_t vb_form_ref;

extern uint8_t screen_buf[], screen_surf[], bg_buf[];
extern uint8_t global_buf_1[];
extern uint16_t global_buf_2[];
extern uint16_t fire_mkf_data[];
extern uint16_t palette_data[];
extern uint16_t map_data_buf[];
extern uint16_t mgo_frame_offsets[], word_glyph_index[];
extern uint8_t rng_anim_data[], menu_bg_data[];
extern uint16_t data_mkf_chunk12[];
extern uint32_t max_subfile_size;
extern uint8_t image_offset_table[];
extern int16_t save_temp_buf[];
extern uint16_t battle_order_array[];
extern uint32_t file_offset_table[];
extern uint8_t sss_script_data[], sss_script_data_2[];
extern uint8_t ball_mkf_data[], data_object_ext[];
extern uint16_t rng_anim_frames[];

extern uint8_t data_battlefield[];
extern uint8_t data_levelup_exp[];
extern uint8_t data_magic[];
extern uint8_t data_enemy_team[];
extern uint8_t data_enemy[];
extern uint8_t events[];
extern levelup_magic_all_t data_levelup_magic[];
extern uint16_t data_store[21][9];
extern scene_t scenes[];
extern object_t objects[];
extern inventory_t inventory[];
extern party_t party[];
extern trail_t trail[];
extern all_experience_t playerExp;
extern uint16_t equipment_effect[14][7][6];
extern uint16_t playerRoles[];
extern enemy_t enemy_runtime_data[];
extern enemy_pos_t enemy_pos_data;
extern uint16_t player_status[][MAX_STATUS_SLOTS];
extern uint16_t enemy_status[][MAX_STATUS_SLOTS];
extern poison_status_t poison_status[][MAX_POISONS];
extern poison_status_t enemy_poison_status[][MAX_POISONS];

extern battle_sprite_t player_battle_sprite[];
extern enemy_battle_t enemy_battle_data[];
extern battle_action_t battle_role_action[];
extern battle_action_t battle_role_data_copy[];
extern npc_display_t npc_display_data[];
extern damage_number_t damage_numbers[];
extern uint16_t effect_x_coords[], effect_y_coords[], effect_frames[];
extern uint16_t player_hit_flags[];
extern uint16_t battle_sprite_data[], battle_sprite_data_ext[];
extern int16_t battle_enemy_data_ext[];
extern uint16_t enemy_battle_data_ext[];
extern uint16_t battle_role_data_ext[][2];
extern int16_t battle_action_queue_ext[];
extern uint16_t battle_sprite_ext[];
extern battle_effect_t battle_effect_data[];
extern magic_t theurgy_data;
extern int16_t battle_action_queue;

extern uint16_t enemy_attack_order[];
extern uint8_t key_mapping_table[][2];
extern char word_dat_data[][10];

#define playerRoles(role, attr) (playerRoles[(attr) * MAX_PLAYER_ROLES + (role)])
#define equipment_effect(role, slot, attr) equipment_effect[(attr) - 17][(slot) - 11][role]
#define player_status(role, status) player_status[role][status]
#define enemy_status(enemyIdx, status) enemy_status[enemyIdx][status]

void entry_stub_show_text(void);
void entry_stub_show_text_and_dialog(void);
void DoEvents_check_exit(void);
void timer_entry_stub(void);
void battle_set_action_code(void);
void swap_values(int16_t *v1, int16_t *v2);
int16_t calc_level_bonus(int16_t enemyIdx, int16_t multiplier);
void load_battle_effect_sprites(void);
int16_t check_key_pressed(void);
int16_t increment_script_ip(int16_t ip);
int16_t read_key(void);
void play_sound(int16_t soundNum, int16_t keepFlag);
void battle_set_action_walk(void);
void copy_subfile_data(int16_t magicIdx);
void wait_frame(int16_t frames);
void load_subfile_to_buf(int16_t subfileNum);
void draw_string(int16_t x, int16_t y, int16_t wordData, int16_t shadow, int16_t color);
void check_battle_action(int16_t *actionParam, int16_t *directionParam);
void push_screen_buffer(void);
void check_trigger_flag(void);
void draw_text_at(int16_t x, int16_t y, int16_t wordIdx, int16_t color);
int16_t count_alive_enemies(void);
int16_t random_enemy_id(void);
void clear_enemy_poison(int16_t enemyIdx);
void load_event_objects(void);
int32_t read_file_and_close(const char *filename, int16_t subfileNum);
void load_fbp_subfile(int16_t subfileNum);
void draw_npc_sprite(void);
int16_t count_item_total(int16_t itemID);
void read_palette(int16_t subfileNum);
void restore_screen(void);
void menu_inventory(void);
int load_map_data(int16_t x, int16_t y, int16_t layer);
void update_trail_data(void);
int16_t check_party_alive(void);
int16_t find_magic_index(int16_t partyIdx, int16_t magicID);
int16_t find_inventory_item(int16_t itemID);
void load_fbp_two_scene(int16_t subfileNum);
void update_shake(void);
void set_auto_battle_targets(void);
int16_t get_player_attribute_total(int16_t roleID, int16_t attrIdx);
void menu_loop(int16_t *cursorPos, int16_t x, int16_t y, int16_t labelIdx, int16_t midBlocks, int16_t rowCount);
void show_dialog_image_and_wait(void);
void get_party_role_id(void);
void check_in_battle(int16_t shakeMode);
void menu_Status(void);
void update_party_position(void);
void update_walk_frame(void);
void read_ball_mkf_index(int16_t subfileNum);
int16_t calc_magic_damage(int16_t baseDamage, int16_t elementIdx);
void read_file_to_buf(int16_t subfileNum, int16_t destIdx);
int16_t random_alive_party_member(void);
void reset_battle_sprite_pos(int16_t roleIdx);
void read_mkf_subfile(int32_t fh, int16_t subfileNum, uint8_t *destBuf);
void load_battle_sprites(void);
void read_rng_subfile(int16_t subfileNum);
int32_t open_file(const char *filename, int16_t writeMode);
void load_sound_data(void);
void load_theurgy_image(int16_t magicIdx);
int16_t count_equipped_items(int16_t itemID);
int16_t calc_battle_damage(int16_t attack, int16_t defense);
void get_subfile_len(int32_t fileHandle, int16_t subfileNum);
void frame_menu(int16_t x, int16_t y, int16_t midBlocks, int16_t highlight);
void load_scene_events(int16_t sceneID);
void draw_battle_status_bar(void);
void fade_out_palette(int16_t speed);
void draw_menu_with_text_and_hp(int16_t wordIdx, int16_t x, int16_t y, int16_t hp, int16_t labelIdx);
void release_resources_exit(void);
void trim_string(char *str);
void make_dialog_frame(int16_t x, int16_t y);
void unequip_item(int16_t itemID, int16_t count);
void fade_palette_to(int16_t speed);
void show_face(int16_t x, int16_t y, int16_t faceID);
void process_menu(void);
void draw_battle_row_sprite(void);
void enemy_cast_anim(int16_t enemyIdx);
void show_fbp_picture(int16_t subfileNum, int16_t rngSubfile);
void show_number_to_tree(int16_t x, int16_t y, int16_t number, int16_t colorType);
void cd_stop(void);
void adjust_battle_sprite_pos(void);
void cross_fade_out(int16_t *speed);
void load_script_data(int16_t subfileNum);
void sell_item_menu(void);
void get_sprite_frame_data(int16_t roleIdx, int16_t frame);
void fade_in(int16_t speed);
void update_damage_numbers(void);
void play_battle_effect(int16_t roleIdx);
void show_enemy_damage(int16_t *theurgyType);
void draw_menu_frame(int16_t x, int16_t y, int16_t lastBlockIdx, int16_t highlight);
void add_damage_number(int16_t x, int16_t y, int16_t number, int16_t colorType);
void apply_enemy_poison_damage(void);
void check_trigger_events(void);
void fade_in_or_out_internal(void);
int16_t check_player_alive(int16_t roleIdx);
void menu_select_party(int16_t x, int16_t y, int16_t spriteNum);
void select_magic(void);
void draw_object_icon(int16_t x, int16_t y, int16_t invIdx);
void show_money(uint32_t amount);
void init_enemy_positions(void);
void display_number(int16_t x, int16_t y, int16_t number, int16_t colorType);
void show_small_number(int16_t x, int16_t y, uint32_t number, int16_t digitCount);
int16_t select_battle_target(void);
void add_inventory_item(int16_t itemID, int16_t amount);
void enemy_hit_reaction(int16_t shakeCount);
void play_rng_effect(int16_t rngSubfile, int16_t frameCount);
void draw_rng_frame(int16_t frameDelay, int16_t frameCount, int16_t yPos);
void query_midi_status(void);
void animate_battle_sprites(int16_t startIdx, int16_t endIdx);
void query_cd_status(void);
void get_scene_map_source(int16_t sceneID);
void player_attack_anim(int16_t roleIdx, int16_t hasEffect);
void draw_effect_sprites(int16_t useOverlay);
void player_attack_execute(int16_t roleIdx, int16_t targetRole);
void cd_stopplay(void);
void midi_close(void);
void render_scene_and_fade(int16_t fadeSpeed);
void init_walk_frames(void);
void stop_all_and_exit(void);
void npc_walk_one_step(int16_t npcIdx, int16_t stepCount);
void render_scene_with_rng(void);
void remove_inventory_item(int16_t itemID, int16_t amount);
void scene_transition(void);
void sort_player_magic(int16_t roleID);
void walk_party_fastest(int16_t startFrame, int16_t endFrame, int16_t speed);
void play_cd(int16_t trackNum, int16_t musicNum, int16_t trackValid);
int16_t compact_inventory(void);
void init_battle_state(void);
int16_t yes_no_menu(int16_t optionFlags, int16_t labelIdx);
int16_t select_enemy_target(void);
void get_sprites_curr_scene(void);
void menu_system(void);
void select_party_member(void);
void play_theurgy_anim(int16_t magicIdx, int16_t startFrame, int16_t minFrames);
void show_party_hp_mp_change(int16_t *theurgyType);
void load_npc_sprites(void);
void stop_app_and_music(void);
void show_equip_detail(int16_t roleID, int16_t equipIdx);
void use_item_menu(void);
void load_team_mgo(void);
void load_enemy_sprites(void);
void scroll_scene_with_fbp(int16_t fbpSubfile, int16_t rngSubfile, int16_t speed);
void play_theurgy_rng_anim(void);
void add_magic_to_player(int16_t roleID, int16_t magicID, int16_t magicType);
void menu_select(int16_t *cursorPos, int16_t x, int16_t y, int16_t midBlocks, int16_t rowCount);
void draw_player_status(int16_t x, int16_t y, int16_t roleID, int16_t highlight);
void draw_menu_table(int16_t x, int16_t y, int16_t wordIdx, int16_t midBlocks, int16_t rowCount, int16_t highlight);
void draw_role_battle_frame(void);
void load_enemy_data(int16_t enemyIdx, int16_t objectID);
void fade_in_pic(int16_t fbpSubfile, int16_t rngSubfile, int16_t speed);
void update_player_battle_status(void);
void cast_theurgy_anim(int16_t itemID);
void show_role_attributes(int16_t x, int16_t y, int16_t roleID, int16_t showEquip);
void redraw_tile(int16_t x, int16_t y, int16_t right, int16_t bottom, int16_t tileX, int16_t tileY);
void update_enemy_battle_anim(void);
void select_direction_menu(uint16_t *direction);
void calc_player_attack_damage(int16_t targetRole, int16_t attackerRole, int16_t isCritical);
void select_battle_action(int16_t menuType);
void increase_player_attr(int16_t roleID, int16_t attrType, int16_t amount);
void flee_from_battle(int16_t roleIdx, int16_t *isFleeing);
void check_save_file(void);
void play_all_kinds_music(int16_t musicNum, int16_t loopFlag);
void read_direction_input(int16_t *dx, int16_t *dy);
void play_opening_anim(void);
void timer_midi_cd_callback(void);
void draw_battle_scene(int16_t frameCount, int16_t speed);
void produce_screen_map(int16_t x, int16_t y, int16_t viewportIdx);
void render_dialog_control(int16_t x, int16_t y);
void process_AutoScript(int16_t npcIdx, uint16_t *scriptEntry);
void SaveRPG_internal(int16_t saveSlot);
void LoadRPG_internal(int16_t saveSlot);
void enemy_attack_enemy(int16_t attackerIdx, int16_t targetIdx);
void add_sprite_to_tree(int16_t x, int16_t y, int16_t depth, int16_t roleIdx, int16_t frameIdx, int16_t useNPC);
void set_team_draw(void);
void calc_display_exp(int16_t exp);
void show_status_icons(void);
void draw_enemy_battle_frame(void);
void buy_item_menu(int16_t storeID);
void level_up_player(int16_t roleID, int16_t levelCount, int16_t restoreHPMP);
void player_attack_player(int16_t attackerIdx, int16_t targetIdx);
void show_item_description(int16_t *roleIdx, int16_t labelIdx, int16_t rowTop, int16_t invIdx);
void process_event_objects(int16_t checkTrigger);
void Load_system_files(void);
void player_attack_all(int16_t roleIdx, int16_t animate);
void update_viewport_scroll(void);
void enemy_attack_role(int16_t enemyIdx, int16_t targetRole, int16_t itemID);
void role_physical_attack(int16_t attackerIdx, int16_t targetIdx, int16_t *targetRole, int16_t isCritical);
void inventory_use_menu(void);
void show_role_status(int16_t roleIdx);
void select_theurgy(int16_t roleID, int16_t *cursorIdx, int16_t filterMask);
void process_Script(int16_t eventObjID, uint16_t *scriptEntry);
void init_key_definitions(void);
void select_item_with_filter(int16_t *cursorIdx, int16_t bgMode, int16_t filterMask);
void enemy_physical_attack(int16_t enemyIdx, int16_t targetIdx, int16_t targetRole, int16_t useMagic);
void calc_display_theurgy(int16_t enemyIdx, int16_t targetRole, int16_t itemID, int16_t power);
void enemy_magical_attack(int16_t enemyIdx, int16_t targetRole, int16_t itemID, int16_t power);
int16_t process_Battle(int16_t battleScene, int16_t surpriseFlag);
void process_scripts(int16_t eventObjID, uint16_t *scriptEntry, int16_t opcode, int16_t *operand1, int16_t *operand2,
                     int16_t *operand3);

void PAL_WaitTime(int16_t frames);
void PAL_Delay(int16_t ticks);
void PAL_CopyMem(void *dest, const void *src, uint32_t count);
void PAL_PushScreen(uint8_t *buf);
void PAL_PopScreen(uint8_t *buf);
void PAL_ClearScreen(void);
void PAL_ClearMenu(void *ptr, uint16_t wordCount);
void PAL_ClearTree(void);
void PAL_ClearClipNA(uint16_t left, uint16_t top, uint16_t rowLimit, uint16_t rowWidth, uint16_t bottom, void *page,
                     void *list);
void PAL_AddToTree(int32_t x, int32_t footY, uint16_t depth, uintptr_t spriteRef);
uint16_t PAL_SpriteHeight(const uint8_t *bitmap);
void PAL_QueueSprite(const uint8_t *bitmap, int16_t x, int16_t groundY, uint16_t depth, uint16_t height);
void PAL_BlitSprite(const uint8_t *bank, uint32_t offset, int16_t y, int16_t x);
void PAL_BlitBitmap(const uint8_t *bitmap, int16_t y, int16_t x);
void PAL_BlitBitmapTo(uint8_t *dst, const uint8_t *bitmap, int16_t y, int16_t x, uint16_t shadow);
void PAL_PutP(int x, int y, const uint8_t *sprite, void *target, uint32_t effect, int mode);
void PAL_Flip(void);
void PAL_CvLong(uint16_t value, uint16_t *out);
void PAL_Ffxy(int16_t *x, int16_t *y, int16_t maxX, int16_t maxY);
void PAL_ExPalate(uint8_t *dst, const uint8_t *src, uint16_t count, uint16_t step);
void PAL_IntPalate(uint8_t *pal);
void PAL_FuPalate(uint16_t index, uint8_t *dst, const uint8_t *srcBase);
void PAL_CvPalate(uint8_t *moving, const uint8_t *target);
void PAL_CorPalate(uint16_t index, uint8_t *pal);
int32_t PAL_Unpak(const void *packed, uint8_t *dst);
uint16_t PAL_PakSize(uint8_t *buf);
void PAL_ExGop(const uint8_t *map, uint16_t half, uint16_t col, uint16_t row, uint16_t *out2, uint16_t *out1);
void PAL_Rhrff(uint16_t *x);
void PAL_ExGm1(uint16_t x, uint16_t y, const uint8_t *map, uint16_t *out);
void PAL_ExGm2(uint16_t count, uint16_t skipIdx, uint16_t x, uint16_t y, const uint8_t *events, uint16_t *out);
void PAL_ExTF(uint16_t *out, int16_t h, int16_t v);
void PAL_ExMyll(uint16_t width);
void PAL_ExRij(uint16_t *flag, uint16_t *x, uint16_t *y);
void PAL_VMap(uint16_t startCol, uint16_t startRow, uint16_t halfFlag, const uint8_t *mapTileData,
              const uint16_t *gopTable, void *target);
void PAL_VWindow(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);
void PAL_ExBB(uint16_t thresholdY, uint16_t half, uint16_t col, uint16_t row, const uint8_t *mapData, uint8_t *bitBuf);
void PAL_ExMap(int16_t vpX, int16_t vpY, const uint8_t *mapData, uint8_t *bitBuf, const uint16_t *gopBase);
void PAL_GetBin(uint16_t *out, uint16_t value, uint8_t bit);
intptr_t PAL_ArrayPtr(void *buf);
void PAL_WriteFile(int32_t handle, const void *buf, uint32_t size);
void PAL_ReadFile(int32_t handle, void *buf, uint32_t size);
void PAL_CloseFile(int32_t handle);
int32_t PAL_GetFileSize(int32_t handle);
void PAL_NTree(int16_t strideFlag, void *listBase);
void PAL_AddPic0(uint8_t *dstPage, const uint8_t *srcPage, uint16_t iterations, uint16_t offset);
void PAL_AddPic(uint8_t *dstPage, const uint8_t *srcPage, uint16_t iterations, uint16_t offset);
void PAL_NipWA(uint16_t startRow, uint16_t rowCount, void *list);
void PAL_NipWB(uint8_t effect, uint16_t rowCount, void *list);
void PAL_NipWSeg(uint8_t *buf);
void PAL_PutIpNA(int16_t x, int16_t topY, int16_t strideFlag, int16_t rowBound, const void *sprite, intptr_t listBase);
void PAL_Rblk(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom, uint8_t color);
void PAL_RripA(uint16_t rows, uint8_t grade, void *list);
void PAL_RripAFreeze(void);
void PAL_RngPut(const uint16_t *frame);
void PAL_PopScreen6(const void *srcBase, uint16_t offset);
void PAL_PopScreen6a(const void *srcBase, uint16_t offset, uint16_t count);
void PAL_PopScreenB(uint16_t *buf, uint16_t effect);
void PAL_SetTimer(void);
void PAL_KillTimer(void);
uint16_t PAL_InitCD(void);
// NOTE: golden 0x1cf2 经 MCI 播 AVI；remake 自持解码（avi.c），stop!=0 不可跳。
void PAL_PlayAvi(void *hwnd, int16_t play, int16_t stop);
// NOTE: golden SetMode 切 DDraw 显示模式；remake 首调自举 kitty 终端后端（失败 → headless 继续）。
int32_t PAL_SetMode(uint32_t mode);
void PAL_StopApp(int16_t code);
int32_t PAL_InitDSound(uint32_t hwnd);
// NOTE: golden 0x2114 建 DirectInput 对象；remake 输入直读 kitty 键盘电平表，恒返 0。
int32_t PAL_InitInput_Win(uint32_t hinst, uint32_t hwnd);
void PAL_ShutdownDSound(void);
void PAL_ShutDownInput(void);
void PAL_ResetMode(void);
// NOTE: golden 取 VB Timer()/Randomize 种子值；remake 改用 kitty 单调毫秒时钟。
uint32_t PAL_GetTicks(void);
void PAL_Shutdown(int16_t code);
void PAL_LoadDSound(uint16_t soundNum, uint8_t keepFlag);
void PAL_PlayDSound(uint16_t soundNum, uint8_t keepFlag);
void PAL_FlushDSound(void);
void PAL_ReadKey(uint8_t *keyState);
int8_t PAL_CheckKey(uint8_t *keyState);
uint16_t PAL_GetActiveWindow(void);
void PAL_DrawString(const char *text, int16_t x, int16_t y, int16_t shadowFlag, uint8_t color, void *target);
double VB_rtcRandomNext(void);
int VB_rtcAnsiValueBstr(const char *str);
void VB_Mid(char *dst, const char *src, int start, int len);
int VB_rtcMsgBox(const char *title, int flags, const char *msg);
void VB_Left(char *dst, const char *src, int len);
int VB_Len(const char *str);
int VB_Abs(int v);
int VB_Int(double v);
double VB_CSng(int v);
int32_t PAL_Decompress(uint8_t *src, uint8_t *dst, int32_t dstSize);

int32_t pal_lcreat(const char *filename, int attr);
int32_t pal_lopen(const char *filename, int flags);
int32_t pal_hread(int32_t handle, void *buf, int32_t count);
void pal_lclose(int32_t handle);
int32_t pal_llseek(int32_t handle, int32_t offset, int32_t whence);
int32_t pal_mciSendStringA(const char *cmd, char *retbuf, int32_t retlen);
void pal_rtcDoEvents(void);
void pal_playMidi(uint16_t musicNum, uint16_t loopFlag);
void pal_playCdTrack(uint16_t trackNum, uint16_t musicNum, uint16_t trackValid);
int32_t pal_vbReadWordDat(uint8_t *dst);
void pal_vbOpenWordDat(void);
void pal_vbCloseWordDat(void);

#ifdef __cplusplus
}
#endif

#endif
