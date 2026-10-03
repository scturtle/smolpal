
#include "pal.h"
#include "kitty.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// NOTE: 开机/开场/结局 AVI 已落地（avi.c 自持解码 MS Video 1 + PCM 音轨，
// pal_ext.c PAL_PlayAvi 桥接）；置环境变量 PAL_SKIP_AVI=1 跳过开机/开场/新游戏
// AVI 用于调试（解析方式同 PAL_HEADLESS）。
static int pal_skip_avi(void) {
  const char *env = getenv("PAL_SKIP_AVI");
  return env != NULL && atoi(env) != 0;
}

static int16_t vb_round_banker_i16(float v) {
  int32_t fl = (int32_t)floorf(v);
  int32_t ce = (int32_t)ceilf(v);
  float dfl = v - (float)fl;
  float dce = (float)ce - v;
  if (dfl < dce)
    return (int16_t)fl;
  if (dce < dfl)
    return (int16_t)ce;
  return (int16_t)((fl % 2 == 0) ? fl : ce);
}

static uint16_t vb_round_banker_u16(float v) { return (uint16_t)vb_round_banker_i16(v); }

static uint32_t vb_round_banker_u32(float v) {
  int64_t fl = (int64_t)floorf(v);
  int64_t ce = (int64_t)ceilf(v);
  float dfl = v - (float)fl;
  float dce = (float)ce - v;
  if (dfl < dce)
    return (uint32_t)fl;
  if (dce < dfl)
    return (uint32_t)ce;
  return (uint32_t)((fl % 2 == 0) ? fl : ce);
}

// NOTE: golden 的 R8 运算链在 x87 64 位扩展精度（CW 0x133F）上求值，
// CI2R8/CI4R8/FnIntR8 以 banker 舍入取整；此处用 double 近似扩展精度，
// 需要 >53 位尾数的中间值理论上可差 1（残差已在 remake.md 登记）。
static int16_t vb_round_banker_i16_d(double v) {
  double fl = floor(v), ce = ceil(v);
  double dfl = v - fl, dce = ce - v;
  if (dfl < dce)
    return (int16_t)fl;
  if (dce < dfl)
    return (int16_t)ce;
  return (int16_t)(((int64_t)fl % 2 == 0) ? fl : ce);
}

static uint32_t vb_round_banker_u32_d(double v) {
  double fl = floor(v), ce = ceil(v);
  double dfl = v - fl, dce = ce - v;
  if (dfl < dce)
    return (uint32_t)fl;
  if (dce < dfl)
    return (uint32_t)ce;
  return (uint32_t)(((int64_t)fl % 2 == 0) ? fl : ce);
}

int32_t tmp_file_size;
uint32_t menu_bg_size;

int32_t fh_M_MSG;
int32_t fh_MGO_MKF;
int32_t fh_F_MKF;
int32_t fh_ABC_MKF;
int32_t fh_RNG_MKF;
int32_t fh_temp;
int32_t data_mkf_handle;

int16_t music_mode_init;
int16_t music_mode;
int16_t use_cd_flag;
int16_t has_sfx;
int16_t has_sfx_raw;
int16_t midi_active;
int16_t midi_playing;
int16_t cd_active;
int16_t cd_track_valid;
int16_t cd_track_num;
int16_t music_track_arg;
int16_t music_mode_arg;

int16_t palette_fade_active;
int16_t mutex_shaking;
// NOTE: golden 为 32 位伪指针值；remake 用 intptr_t 适配 64 位主机。
intptr_t screen_buffer_ptr;
int16_t shake_active;
int16_t shake_intensity;
int16_t shake_duration;

int16_t dialog_text_x, dialog_text_y;
int16_t image_draw_x, image_draw_y;
int16_t image_lookup_key, image_draw_flag;
int16_t dialog_width, dialog_height;
int16_t dialog_type, dialog_x, dialog_y;
int16_t dialog_color;
int16_t menu_cursor_pos;
int16_t dialog_y_pos;

int16_t rpg_to_load;
int16_t max_save_number;
int16_t RPG_save_number;
int16_t curr_scene_id_cache;
int16_t RPG_other_peoples;
int16_t RPG_viewport_x, RPG_viewport_y;
int16_t RPG_team_number;
int16_t RPG_curr_scene;
int16_t scene_to_load;
int16_t RPG_color_begin_ptr;
int16_t RPG_team_direction;
int16_t viewport_flags, viewport_flags_2;
int16_t x_off, y_off;
int16_t team_abstract_x, team_abstract_y;
int16_t RPG_music_number;
int16_t RPG_battle_music_number;
int16_t RPG_battle_scene_number;
uint32_t RPG_money;
int16_t party_abs_x, party_abs_y;
int16_t RPG_role_locate_layer;
int16_t scene_leave_flag, scene_enter_flag;
int16_t scene_flags;
uint32_t sss_subfile_count;
int16_t event_object_count;
int16_t curr_scene_event_count;
int16_t RPG_screen_wave_grade;
int16_t wave_progression;
int16_t npc_dir_frame;
int16_t curr_npc_idx;
int16_t npc_curr_frame;
int16_t npc_direction;
int16_t npc_frame_base;
int16_t npc_sprite_num;
int16_t redraw_flag, redraw_hp_mp_flag;
int16_t fade_step_count;
int16_t battle_viewport_x, battle_viewport_y;
int16_t RPG_ememy_chase_rate;
int16_t RPG_change_chaserate_times;
int16_t battle_enemy_idx;
int16_t effect_frame_count;
int16_t in_battle_action;
int16_t battle_param;
int16_t battle_dest_x;
int16_t blow_away_flag;
int16_t battle_y_offset;
int16_t relative_viewport_x, relative_viewport_y;
int16_t viewport_scroll_x, viewport_scroll_y;
int16_t viewport_x_bak, viewport_y_bak;
int16_t viewport_row_count, viewport_row_stride;
int16_t viewport_row_limit;
int16_t frame_counter;
int16_t key_pressed;
int16_t battle_extra_flag;
int16_t battle_sub_flag;
int16_t RPG_current_calabash_number;

int16_t battle_role_idx_2;
int16_t battle_curr_role_idx;
int16_t battle_target_cursor;
int16_t battle_select_max;
int16_t battle_select_idx;
int16_t battle_action_param;
uint32_t battle_action_type;
int16_t enemy_max_id;
int16_t battle_max_hp_sum;
int16_t enemy_pos_count;
int16_t team_number;
int16_t battle_extra_param;
int16_t battle_enemy_hp;
int16_t magic_select_tmp;
int16_t magic_select_idx;
int16_t effect_sub_count;
int16_t effect_particle_count;
int16_t flag_battling;
int16_t auto_battle_flag;
int16_t attack_done_flag;
int16_t coop_magic_tmp;
int16_t damage_target_hp;
int16_t theurgy_effect_count;
int16_t theurgy_effect_max;
int16_t multi_event_state;
int16_t multi_event_param;
int16_t multi_event_param_2;
int16_t multi_event_param_3;
int16_t sprite_frame_count;

int16_t flag_trigger;
int16_t flag_key_updown;
int16_t flag_parallel_mutex;
int16_t mutex_can_change_palette;
uint8_t key_state_array[256];
uint16_t key_pressed_flags[5];
uint16_t key_direction_flags[5];
uint16_t key_repeat_delay[5];
int16_t fh_M_MSG_global[5];
uint16_t key_scan_map[5];
uint16_t key_action_map[5];
uint8_t key_scan_codes[8];

int16_t exit_flag;
int16_t init_done_flag;
intptr_t vb_form_ref;

uint8_t screen_buf[65536];
uint8_t screen_surf[64000];
uint8_t bg_buf[65536];
uint8_t global_buf_1[128001];
uint16_t global_buf_2[32000];
uint16_t fire_mkf_data[32768];
uint16_t palette_data[1536];
uint16_t map_data_buf[65536 / 2];
uint16_t mgo_frame_offsets[65536];
// NOTE: golden word_glyph_index 声明 29200B，实际 MGO 累计解压写入超出
// （VB 指针写无界检查）；remake 按真实数据扩容。
uint16_t word_glyph_index[225000];
uint8_t rng_anim_data[1155234 + 8];
uint32_t max_subfile_size;
uint8_t image_offset_table[256];
uint8_t menu_bg_data[26624];
uint16_t data_mkf_chunk12[150];
int16_t save_temp_buf[100];
uint16_t battle_order_array[256];
uint32_t file_offset_table[32];
uint8_t sss_script_data[65536];
uint8_t sss_script_data_2[389120];
uint8_t ball_mkf_data[147456];
uint8_t data_object_ext[18432];
uint16_t rng_anim_frames[6];

uint8_t data_battlefield[1024];
uint8_t data_levelup_exp[1024];
uint8_t data_magic[4096];
uint8_t data_enemy_team[4096];
uint8_t data_enemy[12288];
uint8_t events[171009];
_Static_assert(sizeof(enemy_t) == 70, "data_enemy record = 70B (disasm UDT(70 B))");
_Static_assert(sizeof(event_object_t) == 32, "events record = 32B (disasm UDT(32 B))");
_Static_assert(sizeof(magic_t) == 32, "data_magic record = 32B (idx*32)");
levelup_magic_all_t data_levelup_magic[20];
uint16_t data_store[21][9];
scene_t scenes[MAX_SCENES + 1];
_Static_assert(sizeof(scenes) >= 2400 + sizeof(scene_t), "scenes must hold 300 records + padding scene[0]");
_Static_assert(sizeof(scene_t) == 8, "save layout: 300 records x 8 bytes = 2400");
object_t objects[MAX_OBJECTS];
inventory_t inventory[MAX_INVENTORY];
party_t party[MAX_PLAYABLE_PLAYER_ROLES];
trail_t trail[MAX_PLAYABLE_PLAYER_ROLES];
all_experience_t playerExp;
uint16_t equipment_effect[14][7][6];
uint16_t playerRoles[75 * MAX_PLAYER_ROLES];
enemy_t enemy_runtime_data[MAX_ENEMIES_IN_TEAM];
enemy_pos_t enemy_pos_data;
uint16_t player_status[MAX_PLAYABLE_PLAYER_ROLES][MAX_STATUS_SLOTS];
uint16_t enemy_status[MAX_ENEMIES_IN_TEAM][MAX_STATUS_SLOTS];
poison_status_t poison_status[MAX_PLAYABLE_PLAYER_ROLES][MAX_POISONS];
poison_status_t enemy_poison_status[MAX_ENEMIES_IN_TEAM][MAX_POISONS];

battle_sprite_t player_battle_sprite[10];
enemy_battle_t enemy_battle_data[MAX_ENEMIES_IN_TEAM];
battle_action_t battle_role_action[8];
npc_display_t npc_display_data[161];
damage_number_t damage_numbers[12];
uint16_t effect_x_coords[6];
uint16_t effect_y_coords[6];
uint16_t effect_frames[6];
uint16_t player_hit_flags[MAX_ENEMIES_IN_TEAM];
uint16_t battle_sprite_data[16];
uint16_t battle_sprite_data_ext[16];
int16_t battle_enemy_data_ext[MAX_ENEMIES_IN_TEAM];
uint16_t enemy_battle_data_ext[MAX_ENEMIES_IN_TEAM];
uint16_t battle_role_data_ext[MAX_ENEMIES_IN_TEAM][2];
battle_action_t battle_role_data_copy[8];
int16_t battle_action_queue_ext[10];
uint16_t battle_sprite_ext[10];
battle_effect_t battle_effect_data[10];
magic_t theurgy_data;
int16_t battle_action_queue;

uint16_t enemy_attack_order[5];
uint8_t key_mapping_table[20][2];
char word_dat_data[600][10];

void entry_stub_show_text(void) { menu_Status(); }

void entry_stub_show_text_and_dialog(void) {
  menu_Status();
  draw_battle_status_bar();
}

void DoEvents_check_exit(void) {
  do {
    pal_rtcDoEvents();
    // NOTE: golden DoEvents 让出时窗体经 WM_PAINT 重绘；remake 无消息泵，在此
    // 补一次上屏；让出改在各自旋轮询点做，避免拉长 wait_frame 计时。
    PAL_Flip();
  } while (exit_flag == 1);
}

void timer_entry_stub(void) {
  if (exit_flag == 1) {
    stop_app_and_music();
  }
}

void battle_set_action_code(void) { battle_role_action[battle_curr_role_idx].actionType = 5; }

void swap_values(int16_t *v1, int16_t *v2) {
  int16_t tmp = *v1;
  *v1 = *v2;
  *v2 = tmp;
}

int16_t calc_level_bonus(int16_t enemyIdx, int16_t multiplier) {
  return (int16_t)((enemy_runtime_data[enemyIdx].level + 6) * multiplier);
}

void load_battle_effect_sprites(void) { PAL_CopyMem(fire_mkf_data, data_object_ext, data_mkf_handle); }

int16_t check_key_pressed(void) {
  DoEvents_check_exit();
  return PAL_CheckKey(key_state_array);
}

int16_t increment_script_ip(int16_t ip) {
  if (ip == 32767)
    return -32768;
  else
    return ip + 1;
}

int16_t read_key(void) {
  key_pressed = 0;
  while (key_pressed == 0) {
    // NOTE: golden rtcDoEvents 每圈交还时间片；remake pump 非阻塞，增 1ms 让出
    // 防灼 CPU（实测标题菜单 ~89%），键采样 1kHz 延迟无感。
    kitty_delay_ms(1);
    key_pressed = check_key_pressed();
  }
  return key_pressed;
}

void play_sound(int16_t soundNum, int16_t keepFlag) {
  if (has_sfx_raw != 0) {
    if (has_sfx && (soundNum > 0)) {
      PAL_PlayDSound(soundNum, keepFlag);
    }
  }
}

void battle_set_action_walk(void) {
  int32_t i;
  for (i = battle_curr_role_idx; i <= enemy_pos_count; i++) {
    battle_role_action[i].actionType = 8;
  }
}

void copy_subfile_data(int16_t magicIdx) {
  memcpy(&theurgy_data.effect, data_magic + (size_t)(int16_t)(magicIdx * 32), 32);
}

void wait_frame(int16_t frames) {
  int32_t i;
  int16_t result;
  for (i = 1; i <= frames; i++) {
    result = check_key_pressed();
    if (result)
      return;
    PAL_WaitTime(1);
  }
}

void load_subfile_to_buf(int16_t subfileNum) {
  get_subfile_len(fh_MGO_MKF, subfileNum);
  pal_hread(fh_MGO_MKF, global_buf_2, tmp_file_size);
}

void draw_string(int16_t x, int16_t y, int16_t wordData, int16_t shadow, int16_t color) {
  char buf[sizeof(word_dat_data[0]) + 1];
  if (wordData >= 565)
    return;
  memcpy(buf, word_dat_data[wordData], sizeof(word_dat_data[0]));
  buf[sizeof(word_dat_data[0])] = 0;
  PAL_DrawString(buf, (int16_t)x, (int16_t)y, (int16_t)shadow, (uint8_t)color, (void *)screen_buffer_ptr);
}

void check_battle_action(int16_t *actionParam, int16_t *directionParam) {
  if (battle_role_action[battle_curr_role_idx].actionType == 8) {
    battle_curr_role_idx = enemy_pos_count;
  }
  battle_curr_role_idx = battle_curr_role_idx + 1;
  *directionParam = 0;
  *actionParam = 0;
}

void push_screen_buffer(void) {
  if (screen_buffer_ptr == 0) {
    PAL_PushScreen(screen_buf);
  } else {
    PAL_CopyMem(screen_buf, (void *)(intptr_t)screen_buffer_ptr, 64000);
  }
}

void check_trigger_flag(void) {
  if (flag_key_updown) {
    wait_frame(160);
    if (flag_parallel_mutex) {
      restore_screen();
    }
  } else if (flag_trigger) {
    show_dialog_image_and_wait();
  }
  flag_trigger = 0;
  flag_key_updown = 0;
}

void draw_text_at(int16_t x, int16_t y, int16_t wordIdx, int16_t color) { draw_string(x, y, wordIdx, 0, color); }

int16_t count_alive_enemies(void) {
  int32_t j = 0, i;
  for (i = 0; i <= team_number; i++) {
    if (enemy_battle_data[i].hp > 0)
      j++;
  }
  return (int16_t)j;
}

int16_t random_enemy_id(void) {
  int32_t rng = 0;
  while (enemy_battle_data[rng].hp <= 0) {
    rng = (uint16_t)VB_Int(VB_rtcRandomNext() * VB_CSng(enemy_max_id));
  }
  return (int16_t)rng;
}

void clear_enemy_poison(int16_t enemyIdx) {
  int32_t i;
  for (i = 0; i <= 15; i++) {
    enemy_poison_status[enemyIdx][i].poisonID = 0;
    enemy_status(enemyIdx, i) = 0;
  }
}

void load_event_objects(void) {
  memcpy(events + scenes[RPG_curr_scene].eventObjectIndex * 32, &npc_display_data[1].vanishTime,
         (size_t)curr_scene_event_count * 32);
}

int32_t read_file_and_close(const char *filename, int16_t subfileNum) {
  int32_t h = open_file_required(filename);
  get_subfile_len(h, subfileNum);
  pal_hread(h, global_buf_2, tmp_file_size);
  pal_lclose(h);
  return tmp_file_size;
}

void load_fbp_subfile(int16_t subfileNum) {
  read_file_and_close("FBP.MKF", subfileNum);
  PAL_Unpak(global_buf_2, global_buf_1);
}

void draw_npc_sprite(void) {
  int16_t frameOffset = mgo_frame_offsets[curr_npc_idx];
  int32_t x = npc_curr_frame - (mgo_frame_offsets[frameOffset] / 2);
  PAL_QueueSprite((const uint8_t *)(mgo_frame_offsets + frameOffset), (int16_t)x, npc_direction, 0,
                  PAL_SpriteHeight((const uint8_t *)(mgo_frame_offsets + frameOffset)));
}

int16_t count_item_total(int16_t itemID) {
  int16_t result;
  int32_t j;
  result = find_inventory_item(itemID);
  if (result >= 0) {
    j = inventory[result].amount;
  } else {
    j = 0;
  }
  j = j + count_equipped_items(itemID);
  return (int16_t)j;
}

void read_palette(int16_t subfileNum) {
  read_file_and_close("PAT.MKF", subfileNum);
  PAL_CopyMem(palette_data, global_buf_2, 1536);
}

void restore_screen(void) {
  if (screen_buffer_ptr == 0) {
    PAL_PopScreen(screen_buf);
  } else {
    PAL_CopyMem((void *)(intptr_t)screen_buffer_ptr, screen_buf, 64000);
  }
  mutex_can_change_palette = 0;
  flag_parallel_mutex = 0;
}

void menu_inventory(void) {
  int32_t tmp2;
  tmp2 = menu_loop(&(int16_t){0}, 30, 60, 22, 2, 2);
  switch (tmp2) {
    case 0:
      inventory_use_menu();
      break;
    case 1:
      select_magic();
      break;
  }
}

int16_t load_map_data(int16_t x, int16_t y, int16_t layer) {
  int16_t tmp2 = 0;
  PAL_ExGm1(x, y, (const uint8_t *)map_data_buf, (uint16_t *)&tmp2);
  if (tmp2)
    PAL_ExGm2(curr_scene_event_count, layer, x, y, (const uint8_t *)&npc_display_data[1], (uint16_t *)&tmp2);
  return tmp2;
}

void update_trail_data(void) {
  PAL_Rhrff((uint16_t *)&trail[0].x);
  trail[0].direction = RPG_team_direction;
  trail[0].x = x_off;
  trail[0].y = y_off;
}

int16_t check_party_alive(void) {
  int16_t aliveCount = 0, i;
  for (i = 0; i <= enemy_pos_count; i++) {
    if (playerRoles(party[i].role, 9) > 0)
      aliveCount++;
  }
  return aliveCount;
}

int16_t find_magic_index(int16_t partyIdx, int16_t magicID) {
  int16_t foundIdx = 0, i;
  for (i = 32; i <= 63; i++) {
    if (playerRoles(party[partyIdx].role, i) == magicID)
      foundIdx = i;
  }
  return foundIdx;
}

int16_t find_inventory_item(int16_t itemID) {
  int16_t foundIdx = -1;
  int32_t i;
  for (i = 0; i <= 255; i++) {
    if (inventory[i].amount > 0 && inventory[i].item == itemID)
      foundIdx = i;
  }
  return foundIdx;
}

void load_fbp_two_scene(int16_t subfileNum) {
  load_fbp_subfile(subfileNum);
  read_file_and_close("FBP.MKF", subfileNum + 1);
  PAL_Unpak(global_buf_2, global_buf_1 + 64000);
}

void update_shake(void) {
  if (shake_active)
    return;
  if (shake_intensity > 0) {
    shake_intensity--;
    if (shake_intensity < 16) {
      shake_duration = (shake_duration * 15) / 16;
    }
    int32_t shakeOffset = (shake_intensity & 1) * shake_duration;
    (void)shakeOffset;
  }
}

int16_t set_auto_battle_targets(void) {
  int32_t result, i;
  result = random_enemy_id();
  for (i = battle_curr_role_idx; i <= enemy_pos_count; i++) {
    battle_role_action[i].actionType = 6;
    battle_role_action[i].target = result;
  }
  auto_battle_flag = 1;
  battle_role_idx_2 = 0;
  return (int16_t)result;
}

int16_t get_player_attribute_total(int16_t roleID, int16_t attrIdx) {
  int32_t total = playerRoles(roleID, attrIdx);
  int32_t i;
  for (i = 11; i <= 17; i++) {
    total += equipment_effect(roleID, i, attrIdx);
  }
  return total;
}

int16_t menu_loop(int16_t *cursorPos, int16_t x, int16_t y, int16_t labelIdx, int16_t midBlocks, int16_t rowCount) {
  int32_t i;
  for (i = 0; i < rowCount; i++) {
    battle_order_array[i] = labelIdx + i;
    battle_order_array[i + 100] = (uint16_t)-1;
  }
  return menu_select(cursorPos, x, y, midBlocks, rowCount);
}

void show_dialog_image_and_wait(void) {
  image_draw_flag = 0;
  if (dialog_type != 0) {
    int32_t frame = ((const uint16_t *)data_mkf_chunk12)[((const uint16_t *)data_mkf_chunk12)[image_lookup_key]];
    PAL_PutP(image_draw_x, image_draw_y, (const uint8_t *)((const uint16_t *)data_mkf_chunk12 + frame),
             (void *)screen_buffer_ptr, 0, 0);
  }
  read_key();
  flag_trigger = 0;
}

void get_party_role_id(void) {
  int32_t i;
  for (i = 0; i <= RPG_team_number + RPG_other_peoples; i++) {
    add_sprite_to_tree(party[i].x, party[i].y, RPG_role_locate_layer, i, party[i].frame, 0);
  }
}

void check_in_battle(int16_t shakeMode) {
  mutex_shaking = mutex_shaking ^ 1;
  PAL_ClearTree();
  if (flag_battling) {
    draw_battle_scene(1, 0);
    return;
  } else {
    PAL_ClearMenu(global_buf_2, 8192);
    get_sprites_curr_scene();
    get_party_role_id();
    render_scene_with_rng();
    render_scene_and_fade(shakeMode);
    return;
  }
}

void menu_Status(void) {
  int16_t roleIndex = 0;
  while (roleIndex >= 0 && roleIndex <= RPG_team_number) {
    show_role_status((uint16_t)roleIndex);
    if (key_pressed == 3 || key_pressed == 5) {
      roleIndex = roleIndex - 1;
    } else {
      roleIndex = roleIndex + 1;
    }
    if (key_pressed == 1) {
      roleIndex = -1;
    }
  }
}

void update_party_position(void) {
  party_abs_x = team_abstract_x + RPG_viewport_x;
  party_abs_y = team_abstract_y + RPG_viewport_y;
  if (party_abs_x != x_off || party_abs_y != y_off) {
    update_walk_frame();
  } else {
    viewport_flags_2 = 0;
    viewport_flags = 0;
    scene_flags = (scene_flags & 2) ^ 2;
  }
  set_team_draw();
}

void update_walk_frame(void) {
  scene_flags = (scene_flags + 1) & 3;
  if ((scene_flags & 1) == 0) {
    viewport_flags = 0;
    viewport_flags_2 = viewport_flags;
  } else {
    viewport_flags = (scene_flags + 1) / 2;
    viewport_flags_2 = 3 - viewport_flags;
  }
  PAL_Rhrff((uint16_t *)&trail[0].x);
}

void read_ball_mkf_index(int16_t subfileNum) {
  PAL_CopyMem(file_offset_table, &ball_mkf_data[subfileNum * 4], 8);
  PAL_CopyMem(global_buf_2, &ball_mkf_data[file_offset_table[0]], file_offset_table[1] - file_offset_table[0]);
}

int16_t calc_magic_damage(int16_t baseDamage, int16_t elementIdx) {
  memcpy(save_temp_buf, data_battlefield + RPG_battle_scene_number * 12, 12);
  int16_t i = baseDamage;
  if (elementIdx > 0 && elementIdx <= 5)
    i = vb_round_banker_i16((float)(int16_t)(baseDamage * (10 + save_temp_buf[elementIdx])) / 10.0f);
  return i;
}

void read_file_to_buf(int16_t subfileNum, int16_t destIdx) {
  get_subfile_len(fh_F_MKF, subfileNum);
  pal_hread(fh_F_MKF, global_buf_2, tmp_file_size);
  PAL_Decompress((uint8_t *)global_buf_2, (uint8_t *)&mgo_frame_offsets[destIdx],
                 (int32_t)(sizeof(mgo_frame_offsets) - destIdx * 2));
  npc_frame_base = PAL_PakSize((uint8_t *)global_buf_2);
}

int16_t random_alive_party_member(void) {
  int32_t rng;
  rng = (uint16_t)VB_Int(VB_rtcRandomNext() * VB_CSng(battle_extra_param));
  while (playerRoles(party[rng].role, 9) <= 0) {
    rng = (uint16_t)VB_Int(VB_rtcRandomNext() * VB_CSng(battle_extra_param));
  }
  return (int16_t)rng;
}

void reset_battle_sprite_pos(int16_t roleIdx) {
  player_battle_sprite[roleIdx].x = player_battle_sprite[roleIdx].origX;
  player_battle_sprite[roleIdx].y = player_battle_sprite[roleIdx].origY;
  player_battle_sprite[roleIdx].direction = player_battle_sprite[roleIdx].origDirection;
  player_battle_sprite[roleIdx].animOffset = 0;
}

void read_mkf_subfile(int32_t fh, int16_t subfileNum, uint8_t *destBuf) {
  uint32_t off = file_offset_table[subfileNum];
  uint32_t next = file_offset_table[subfileNum + 1];
  tmp_file_size = (int32_t)(next - off);
  pal_llseek(fh, (int32_t)off, 0);
  pal_hread(fh, destBuf, tmp_file_size);
}

void load_battle_sprites(void) {
  int32_t i;
  enemy_pos_count = RPG_team_number;
  battle_extra_param = enemy_pos_count + 1;
  int16_t frameOffset = 0;
  for (i = 0; i <= enemy_pos_count; i++) {
    player_battle_sprite[i].spriteBase = frameOffset;
    read_file_to_buf(player_battle_sprite[i].spriteNum, frameOffset);
    frameOffset = frameOffset + npc_frame_base;
  }
}

void read_rng_subfile(int16_t subfileNum) {
  get_subfile_len(fh_RNG_MKF, subfileNum);
  if (tmp_file_size > max_subfile_size) {
    max_subfile_size = tmp_file_size;
  }
  pal_hread(fh_RNG_MKF, rng_anim_data, tmp_file_size);
}

int32_t open_file(const char *filename, int16_t writeMode) {
  int32_t h;
  if (writeMode)
    h = pal_lcreat(filename, 0);
  else
    h = pal_lopen(filename, 0);
  return h;
}

// NOTE: golden 对 DLL 句柄失败静默降级（零数据垃圾状态）；remake 偏离为报错退出（remake.md §3）。
int32_t open_file_required(const char *filename) {
  int32_t h = open_file(filename, 0);
  if (h <= 0) {
    fprintf(stderr, "[remake] 无法打开资源文件 %s（检查 $PAL98_DATA 或 CWD 数据目录）\n", filename);
    exit(1);
  }
  return h;
}

void load_sound_data(void) {
  PAL_LoadDSound(27, 1);
  PAL_LoadDSound(28, 1);
  PAL_LoadDSound(29, 1);
  PAL_LoadDSound(30, 1);
  PAL_LoadDSound(45, 1);
  PAL_LoadDSound(212, 1);
  PAL_LoadDSound(47, 1);
}

void load_theurgy_image(int16_t magicIdx) {
  int16_t tmp;
  memcpy(&tmp, data_magic + magicIdx * 32, 2);
  if (tmp < 55) {
    read_file_and_close("FIRE.MKF", tmp);
    PAL_Unpak(global_buf_2, (uint8_t *)fire_mkf_data);
  }
}

int16_t count_equipped_items(int16_t itemID) {
  int32_t count = 0, i, j, k;
  for (i = 0; i <= RPG_team_number; i++) {
    k = party[i].role;
    for (j = 11; j <= 16; j++) {
      if (playerRoles(k, j) == itemID)
        count++;
    }
  }
  return count;
}

int16_t calc_battle_damage(int16_t attack, int16_t defense) {
  int32_t i = 0;
  if (attack > 0) {
    int32_t j = (int32_t)attack - (int32_t)defense;
    int32_t k = vb_round_banker_i16_d((double)defense / 5.0 * 3.0);
    if ((int32_t)attack > k) {
      i = (int32_t)attack - k;
      if (j > 0)
        i += j;
    }
    i /= 2;
    if (i < 0)
      i = 0;
  }
  return (int16_t)i;
}

void get_subfile_len(int32_t fileHandle, int16_t subfileNum) {
  uint32_t off, next;
  pal_llseek(fileHandle, (int32_t)(int16_t)(subfileNum * 4), 0);
  pal_hread(fileHandle, (uint8_t *)file_offset_table, 8);
  off = file_offset_table[0];
  next = file_offset_table[1];
  pal_llseek(fileHandle, (int32_t)off, 0);
  tmp_file_size = (int32_t)(next - off);
}

void frame_menu(int16_t x, int16_t y, int16_t midBlocks, int16_t highlight) {
  int32_t i;
  PAL_CopyMem(global_buf_2, menu_bg_data, menu_bg_size);
  save_temp_buf[0] = 44;
  for (i = 1; i <= midBlocks; i++) {
    save_temp_buf[i] = 45;
  }
  save_temp_buf[i] = 46;
  draw_menu_frame(x, y, midBlocks + 1, highlight);
}

void load_scene_events(int16_t sceneID) {
  curr_scene_event_count = scenes[sceneID + 1].eventObjectIndex - scenes[sceneID].eventObjectIndex;
  if (curr_scene_event_count > 160)
    curr_scene_event_count = 160;
  memcpy(&npc_display_data[1].vanishTime, events + scenes[sceneID].eventObjectIndex * 32,
         (size_t)curr_scene_event_count * 32);
}

void draw_battle_status_bar(void) {
  int32_t i;
  dialog_y_pos = 200;
  draw_battle_scene(1, -4);
  dialog_y_pos = 140;
  for (i = 0; i <= 3; i++) {
    PAL_PutP(key_scan_map[i], key_action_map[i], (const uint8_t *)(word_glyph_index + word_glyph_index[40 + i]),
             (void *)screen_buffer_ptr, 4, 2);
  }
  show_status_icons();
}

void fade_out_palette(int16_t speed) {
  if (palette_fade_active) {
    palette_fade_active = 0;
    if (speed > 0) {
      int32_t i;
      for (i = 6; i <= 63; i++) {
        PAL_ExPalate((uint8_t *)&palette_data[768], (const uint8_t *)&palette_data[RPG_color_begin_ptr], 768, i);
        PAL_IntPalate((uint8_t *)&palette_data[768]);
        PAL_WaitTime(speed);
      }
    }
    PAL_IntPalate((uint8_t *)&palette_data[RPG_color_begin_ptr]);
  }
}

void draw_menu_with_text_and_hp(int16_t wordIdx, int16_t x, int16_t y, int32_t hp, int16_t labelIdx) {
  frame_menu(x, y, 5, labelIdx);
  draw_string(x + 10, y + 10, wordIdx, 3, 0);
  show_small_number(x + 48, y + 15, hp, 6);
}

void release_resources_exit(void) {
  cd_stopplay();
  midi_close();
  PAL_KillTimer();
  pal_lclose(fh_M_MSG);
  pal_lclose(fh_MGO_MKF);
  pal_lclose(fh_F_MKF);
  pal_lclose(fh_ABC_MKF);
  pal_lclose(fh_RNG_MKF);
  pal_lclose(fh_temp);
  fh_M_MSG = fh_MGO_MKF = fh_F_MKF = fh_ABC_MKF = fh_RNG_MKF = 0;
  fh_temp = 0;
  memset(rng_anim_data, 0, sizeof(rng_anim_data));
  PAL_ShutdownDSound();
  PAL_ShutDownInput();
  PAL_ResetMode();
  PAL_Shutdown(0);
  exit_flag = 1;
}

void trim_string(char *str) {
  int strLen = VB_Len(str);
  while (strLen > 0) {
    unsigned char ch = (unsigned char)str[strLen - 1];
    if (ch != 0 && ch != 32)
      break;
    strLen--;
  }
  str[strLen] = 0;
}

void make_dialog_frame(int16_t x, int16_t y) {
  int32_t frame;
  PAL_CopyMem(global_buf_2, menu_bg_data, menu_bg_size);
  frame = ((const uint16_t *)global_buf_2)[69];
  PAL_PutP(x + 3, y + 2, (const uint8_t *)(global_buf_2 + frame), (void *)screen_buffer_ptr, 0, 3);
  PAL_PutP(x, y, (const uint8_t *)(global_buf_2 + frame), (void *)screen_buffer_ptr, 0, 0);
}

void unequip_item(int16_t itemID, int16_t count) {
  int32_t i, j, k, l;
  for (i = 1; i <= count; i++) {
    for (j = 0; j <= RPG_team_number; j++) {
      l = party[j].role;
      for (k = 11; k <= 16; k++) {
        if (playerRoles(l, k) == itemID) {
          playerRoles(l, k) = 0;
          goto L_0040595E;
        }
      }
    }
  L_0040595E:;
  }
}

void fade_palette_to(int16_t speed) {
  PAL_FuPalate(speed, (uint8_t *)&palette_data[1152], (const uint8_t *)&palette_data[RPG_color_begin_ptr]);
  PAL_CopyMem(&palette_data[768], &palette_data[RPG_color_begin_ptr], 768);
  int32_t i;
  for (i = 1; i <= 25; i++) {
    PAL_CvPalate((uint8_t *)&palette_data[768], (const uint8_t *)&palette_data[1152]);
    PAL_IntPalate((uint8_t *)&palette_data[768]);
    PAL_WaitTime(8);
  }
}

void show_face(int16_t x, int16_t y, int16_t faceID) {
  int16_t faceIdx;
  int32_t px, py;
  int16_t faceW, faceH;

  faceIdx = 0;
  read_file_and_close("RGM.MKF", faceID);
  faceIdx = global_buf_2[0];
  px = (int16_t)x - (int16_t)(global_buf_2[faceIdx] / 2);
  py = (int16_t)y - (int16_t)(global_buf_2[faceIdx + 1] / 2);
  faceW = (int16_t)global_buf_2[faceIdx];
  faceH = (int16_t)global_buf_2[faceIdx + 1];
  (void)faceW;
  (void)faceH;
  PAL_PutP(px, py, (const uint8_t *)(global_buf_2 + faceIdx), (void *)screen_buffer_ptr, 0, 0);
}

void process_menu(void) {
  int32_t tmp2;
  draw_menu_with_text_and_hp(21, 0, 0, RPG_money, -1);
  tmp2 = menu_loop(&(int16_t){0}, 3, 37, 3, 3, 4);
  switch (tmp2) {
  case 0:
    entry_stub_show_text();
    break;
  case 1:
    use_item_menu();
    break;
  case 2:
    menu_inventory();
    break;
  case 3:
    menu_system();
    break;
  }
  draw_battle_row_sprite();
}

void draw_battle_row_sprite(void) {
  int16_t i, j, k;
  PAL_ExMyll(viewport_row_count);
  PAL_VWindow(0, 0, 319, 199);
  relative_viewport_x = 0;
  relative_viewport_y = 0;
  viewport_x_bak = RPG_viewport_x;
  viewport_y_bak = RPG_viewport_y;
  j = RPG_viewport_x;
  k = RPG_viewport_y;
  PAL_ExRij((uint16_t *)&i, (uint16_t *)&j, (uint16_t *)&k);
  PAL_VMap(j, k, i, (const uint8_t *)map_data_buf, mgo_frame_offsets, global_buf_1);
}

void enemy_cast_anim(int16_t enemyIdx) {
  int32_t lastFrame = (enemy_runtime_data[enemyIdx].idleFrames + enemy_runtime_data[enemyIdx].magicFrames) - 1;
  if (enemy_runtime_data[enemyIdx].magicFrames > 0) {
    int32_t i;
    for (i = enemy_runtime_data[enemyIdx].idleFrames; i <= lastFrame; i++) {
      enemy_battle_data[enemyIdx].direction = i;
      draw_battle_scene(enemy_runtime_data[enemyIdx].actWaitFrames, 0);
    }
    return;
  } else {
    draw_battle_scene(1, 0);
    return;
  }
}

void show_fbp_picture(int16_t subfileNum, int16_t rngSubfile) {
  PAL_CopyMem(bg_buf, global_buf_1, 64000);
  if (subfileNum >= 0) {
    load_fbp_subfile(subfileNum);
  } else {
    PAL_ClearMenu(global_buf_1, 32000);
  }
  if (rngSubfile) {
    draw_rng_frame(rngSubfile, 95, 10672);
  } else {
    PAL_PopScreen(global_buf_1);
  }
  PAL_CopyMem(global_buf_1, bg_buf, 64000);
}

void show_number_to_tree(int16_t x, int16_t y, int16_t number, int16_t colorType) {
  int32_t colorBase = 0, screenX, remaining, digit;
  if (number < 0)
    return;
  if (colorType == 0)
    colorBase = 19;
  if (colorType == 1)
    colorBase = 29;
  if (colorType == 2)
    colorBase = 56;
  screenX = x;
  remaining = number;
  while (remaining > 0) {
    digit = remaining % 10;
    remaining = remaining / 10;
    const uint8_t *bitmap = (const uint8_t *)(word_glyph_index + word_glyph_index[digit + colorBase]);
    PAL_QueueSprite(bitmap, (int16_t)screenX, (int16_t)(y + 999), 999, PAL_SpriteHeight(bitmap));
    screenX = screenX - 6;
  }
}

void cd_stop(void) {
  if (music_mode == 2 && cd_active != 0) {
    cd_track_valid = 0;
    cd_active = 0;
    cd_track_num = 0;
    pal_mciSendStringA("stop cdtrack", NULL, 0);
  }
}

void adjust_battle_sprite_pos(void) {
  int32_t i, j;
  for (i = 1; i <= 9; i++) {
    for (j = 0; j <= enemy_pos_count; j++) {
      if (check_player_alive(j) > 1) {
        player_battle_sprite[j].x = (player_battle_sprite[j].x + battle_sprite_data[j]) / 2;
        player_battle_sprite[j].y = (player_battle_sprite[j].y + battle_sprite_data_ext[j]) / 2;
      }
    }
    draw_battle_scene(1, 0);
  }
}

void cross_fade_out(int16_t *speed) {
  int16_t start = 0, end = 64, i;
  palette_fade_active = 0;
  if (*speed == 0)
    *speed = 1;
  if (*speed < 0) {
    int16_t tmp = start;
    start = end;
    end = tmp;
  }
  for (i = start; (*speed > 0) ? i <= end : i >= end; i += *speed) {
    PAL_ExPalate((uint8_t *)&palette_data[768], (const uint8_t *)&palette_data[RPG_color_begin_ptr], 768, (uint16_t)i);
    PAL_IntPalate((uint8_t *)&palette_data[768]);
    process_event_objects(0);
    check_in_battle(0);
  }
  palette_fade_active = (*speed < 0);
}

void load_script_data(int16_t subfileNum) {
  int32_t fileSize;
  uint32_t start;
  uint32_t end;

  memcpy(file_offset_table, &sss_script_data[subfileNum * 4], 8);
  start = file_offset_table[0];
  end = file_offset_table[1];
  fileSize = (int32_t)(end - start);
  image_offset_table[0] = (uint8_t)fileSize;
  pal_llseek(fh_M_MSG, (int32_t)start, 0);
  pal_hread(fh_M_MSG, &image_offset_table[1], fileSize);
  image_offset_table[fileSize + 1] = 0;
}

void sell_item_menu(void) {
  int16_t selected;
  int32_t answer;
  int16_t cursorPos = 0;
  for (;;) {
    draw_menu_with_text_and_hp(21, 100, 150, RPG_money, 0);
    selected = select_item_with_filter(&cursorPos, 1, 32);
    if (selected < 0)
      break;
    push_screen_buffer();
    answer = (uint16_t)yes_no_menu(0, 19);
    if (answer == 1) {
      remove_inventory_item((uint16_t)selected, 1);
      RPG_money += objects[selected].data[1] / 2;
    }
    restore_screen();
  }
  draw_battle_row_sprite();
}

void get_sprite_frame_data(int16_t roleIdx, int16_t frame) {
  int32_t baseIdx, y;
  int32_t frameOffset =
      mgo_frame_offsets[player_battle_sprite[roleIdx].spriteBase + player_battle_sprite[roleIdx].direction] +
      player_battle_sprite[roleIdx].spriteBase;
  baseIdx = player_battle_sprite[roleIdx].x - (mgo_frame_offsets[frameOffset] / 2);
  y = player_battle_sprite[roleIdx].y - mgo_frame_offsets[frameOffset + 1];
  PAL_PutP(baseIdx, y, (const uint8_t *)(mgo_frame_offsets + frameOffset), (void *)screen_buffer_ptr, frame, 1);
}

void fade_in(int16_t speed) {
  int16_t i;
  if (palette_fade_active == 0) {
    palette_fade_active = 1;
    if (speed > 0) {
      for (i = 63; i >= 5; i--) {
        PAL_ExPalate((uint8_t *)&palette_data[768], (const uint8_t *)&palette_data[RPG_color_begin_ptr], 768,
                     (uint16_t)i);
        PAL_IntPalate((uint8_t *)&palette_data[768]);
        PAL_WaitTime(speed);
      }
    }
    PAL_ExPalate((uint8_t *)&palette_data[768], (const uint8_t *)&palette_data[RPG_color_begin_ptr], 768, 0);
    PAL_IntPalate((uint8_t *)&palette_data[768]);
  }
}

void update_damage_numbers(void) {
  int32_t i;
  for (i = 0; i <= 11; i++) {
    if (damage_numbers[i].timer > 0) {
      show_number_to_tree(damage_numbers[i].x, damage_numbers[i].y, damage_numbers[i].number,
                          damage_numbers[i].colorType);
      damage_numbers[i].timer--;
      damage_numbers[i].y--;
    }
  }
}

void play_battle_effect(int16_t roleIdx) {
  int32_t i;
  load_battle_effect_sprites();
  effect_x_coords[1] = player_battle_sprite[roleIdx].x;
  effect_y_coords[1] = player_battle_sprite[roleIdx].y;
  effect_frames[1] = 100;
  effect_particle_count = 1;
  effect_frame_count = 14 + battle_effect_data[player_battle_sprite[roleIdx].spriteNum].effect * 10;
  for (i = 1; i <= 10; i++) {
    effect_frame_count++;
    draw_battle_scene(1, -1);
  }
  effect_particle_count = 0;
}

void show_enemy_damage(int16_t *theurgyType) {
  int32_t i;
  int16_t j, k;
  for (i = 0; i <= team_number; i++) {
    if (battle_enemy_data_ext[i] > 0) {
      j = enemy_battle_data[i].hp - battle_enemy_data_ext[i];
      if (j != 0) {
        k = (j > 0) ? 0 : 1;
        add_damage_number(enemy_battle_data[i].x, enemy_battle_data[i].y - 110, VB_Abs(j), k);
        *theurgyType = -1;
        player_hit_flags[i] = (uint16_t)-1;
      }
    }
  }
}

void draw_menu_frame(int16_t x, int16_t y, int16_t lastBlockIdx, int16_t highlight) {
  int16_t i;
  int16_t cursorX = (int16_t)x;
  for (i = 0; i <= (int16_t)lastBlockIdx; i++) {
    int32_t k = global_buf_2[save_temp_buf[i]];
    if (highlight)
      PAL_PutP((int16_t)(cursorX + 6), (int16_t)(y + 6), (const uint8_t *)(global_buf_2 + k), (void *)screen_buffer_ptr,
               0, 3);
    PAL_PutP((int16_t)cursorX, y, (const uint8_t *)(global_buf_2 + k), (void *)screen_buffer_ptr, 0, 0);
    cursorX += global_buf_2[k];
  }
}

void add_damage_number(int16_t x, int16_t y, int16_t number, int16_t colorType) {
  int32_t i;
  if ((int16_t)number <= 0)
    return;
  for (i = 0; i <= 11; i++) {
    if (damage_numbers[i].timer <= 0) {
      damage_numbers[i].timer = 10;
      damage_numbers[i].x = x;
      damage_numbers[i].y = y;
      damage_numbers[i].number = number;
      damage_numbers[i].colorType = colorType;
      if (damage_numbers[i].y < 15)
        damage_numbers[i].y = 15;
      return;
    }
  }
}

void apply_enemy_poison_damage(void) {
  int32_t i;
  for (i = 0; i <= team_number; i++) {
    if (battle_order_array[i]) {
      enemy_battle_data[i].hp -= battle_order_array[20 + i];
      if (enemy_battle_data[i].hp > 0) {
        enemy_battle_data[i].x = enemy_battle_data[i].origX;
        enemy_battle_data[i].y = enemy_battle_data[i].origY;
      } else {
        battle_enemy_hp = enemy_runtime_data[i].deathSound;
      }
    }
  }
}

void check_trigger_events(void) {
  int16_t objIdx = produce_screen_map(party_abs_x, party_abs_y, RPG_team_direction);
  if (objIdx >= 0) {
    if (npc_display_data[objIdx].currentFrame < (npc_display_data[objIdx].spriteFrames * 4)) {
      int32_t i;
      npc_display_data[objIdx].currentFrame = 0;
      npc_display_data[objIdx].direction = (RPG_team_direction + 2) & 3;
      for (i = 0; i <= RPG_team_number; i++) {
        party[i].frame = RPG_team_direction * 3;
      }
      check_in_battle(0);
    }
    process_Script((int16_t)objIdx, &npc_display_data[objIdx].triggerScript);
  }
}

void fade_in_or_out_internal(void) {}

int16_t check_player_alive(int16_t roleIdx) {
  int16_t alive = -1;
  alive &= (player_status(roleIdx, 0) <= 0);
  alive &= (player_status(roleIdx, 1) <= 0);
  alive &= (player_status(roleIdx, 2) <= 0);
  alive &= (playerRoles(party[roleIdx].role, 9) > 0);
  if (alive)
    alive = player_battle_sprite[roleIdx].direction == 1 ? 1 : 2;
  return alive;
}

int16_t menu_select_party(int16_t x, int16_t y, int16_t spriteNum) {
  int16_t i = 0;
  int16_t j = 0;
  if (RPG_team_number > 0) {
    int32_t k;
    if (j > (int16_t)RPG_team_number)
      j = RPG_team_number;
    for (k = 0; k <= RPG_team_number; k++) {
      battle_order_array[k] = playerRoles(party[k].role, 3);
      PAL_GetBin(&battle_order_array[100 + k], spriteNum, party[k].role);
    }
    i = menu_select(&j, x, y, 3, RPG_team_number + 1);
  }
  return i;
}

void select_magic(void) {
  int16_t magicCount;
  int32_t j;
  int16_t itemIdx = 0;
  do {
    int16_t menuResult = select_item_with_filter(&magic_select_tmp, 0, 1);
    if (menuResult < 0)
      return;
    PAL_GetBin((uint16_t *)&magicCount, objects[menuResult].data[6], 4);
    if (magicCount != 0) {
      draw_battle_row_sprite();
      j = 0;
      process_Script(j, &objects[menuResult].data[2]);
      if (redraw_hp_mp_flag) {
        PAL_GetBin((uint16_t *)&magicCount, objects[menuResult].data[6], 3);
        if (magicCount)
          remove_inventory_item(menuResult, 1);
      }
      return;
    }
    show_item_description(&itemIdx, 110, 2, magic_select_tmp);
  } while (itemIdx < 0);
}

void draw_object_icon(int16_t x, int16_t y, int16_t invIdx) {
  int32_t frame;
  int16_t ballIdx = objects[invIdx].data[0];
  if (ballIdx) {
    frame = ((const uint16_t *)global_buf_2)[70];
    PAL_PutP(x + 5, y + 4, (const uint8_t *)(global_buf_2 + frame), (void *)screen_buffer_ptr, 0, 3);
    PAL_PutP(x, y, (const uint8_t *)(global_buf_2 + frame), (void *)screen_buffer_ptr, 0, 0);
    read_ball_mkf_index(ballIdx);
    PAL_PutP(x + 8, y + 7, (const uint8_t *)(global_buf_2 + global_buf_2[0]), (void *)screen_buffer_ptr, 0, 0);
  }
}

void show_money(uint32_t amount) {
  frame_menu(60, 100, 10, -1);
  draw_string(70, 110, 9, 3, 0);
  show_small_number(148, 114, amount, 6);
  draw_string(194, 110, 10, 3, 0);
  RPG_money = RPG_money + amount;
}

void init_enemy_positions(void) {
  int32_t i;
  for (i = 0; i <= team_number; i++) {
    enemy_battle_data[i].x = enemy_pos(team_number, i).x;
    enemy_battle_data[i].y = enemy_pos(team_number, i).y + enemy_runtime_data[i].yPosOffset;
    enemy_battle_data[i].origX = enemy_battle_data[i].x;
    enemy_battle_data[i].origY = enemy_battle_data[i].y;
    enemy_battle_data[i].direction = 0;
    enemy_battle_data[i].flag = 0;
  }
}

void display_number(int16_t x, int16_t y, int16_t number, int16_t colorType) {
  int16_t colorBase = 0, screenX, remaining, digit;
  PAL_CopyMem(global_buf_2, menu_bg_data, menu_bg_size);
  if (number < 0)
    return;
  if (colorType == 0)
    colorBase = 19;
  if (colorType == 1)
    colorBase = 29;
  if (colorType == 2)
    colorBase = 56;
  screenX = x;
  remaining = number;
  do {
    digit = remaining % 10;
    remaining = remaining / 10;
    PAL_PutP(screenX, y, (const uint8_t *)(global_buf_2 + ((const uint16_t *)global_buf_2)[digit + colorBase]),
             (void *)screen_buffer_ptr, 0, 0);
    screenX -= 6;
  } while (remaining > 0);
}

void show_small_number(int16_t x, int16_t y, uint32_t number, int16_t digitCount) {
  int16_t screenX, digit;
  int16_t i;
  uint32_t num;
  PAL_CopyMem(global_buf_2, menu_bg_data, menu_bg_size);
  if ((int32_t)number < 0)
    return;
  screenX = (x + (digitCount * 6)) - 6;
  num = number;
  for (i = 1; i <= digitCount; i++) {
    digit = num % 10;
    num = (uint32_t)VB_Int((double)num / 10.0);
    PAL_PutP(screenX, y, (const uint8_t *)(global_buf_2 + ((const uint16_t *)global_buf_2)[digit + 19]),
             (void *)screen_buffer_ptr, 0, 0);
    screenX -= 6;
    if (num <= 0)
      return;
  }
}

int16_t select_battle_target(void) {
  int16_t i = 0, result;
  if (enemy_pos_count > 0) {
    battle_target_cursor = 0;
    i = -2;
    while (i == -2) {
      // NOTE: 同 read_key：remake 增补让出防灼 CPU（golden 靠 DoEvents 交还）。
      kitty_delay_ms(1);
      result = check_key_pressed();
      if (result == 5)
        battle_target_cursor--;
      if (result == 6)
        battle_target_cursor++;
      if (battle_target_cursor < 0)
        battle_target_cursor = enemy_pos_count;
      if (battle_target_cursor > enemy_pos_count)
        battle_target_cursor = 0;
      if (result == 1)
        i = -1;
      if (result == 2)
        i = battle_target_cursor;
      draw_battle_scene(1, 0);
    }
    battle_target_cursor = -1;
  }
  return i;
}

void add_inventory_item(int16_t itemID, int16_t amount) {
  int32_t i;
  if ((int16_t)itemID <= 0)
    return;
  for (i = 0; i <= 255; i++) {
    if (inventory[i].amount > 0 && inventory[i].item == itemID) {
      inventory[i].amount += amount;
      return;
    }
  }
  for (i = 0; i <= 255; i++) {
    if (inventory[i].amount <= 0) {
      inventory[i].amount = amount;
      inventory[i].item = itemID;
      inventory[i].amountInUse = 0;
      return;
    }
  }
}

void enemy_hit_reaction(int16_t shakeCount) {
  int16_t amp = 8;
  int32_t i, j, k;
  for (i = 1; i <= shakeCount; i++) {
    for (j = 0; j <= 1; j++) {
      for (k = 0; k <= team_number; k++) {
        if (battle_order_array[k]) {
          enemy_battle_data[k].x -= amp;
          enemy_battle_data[k].y -= (amp / 2);
          if (i == 1)
            player_hit_flags[k] = (uint16_t)-1;
        }
      }
      draw_battle_scene(1, 0);
      amp = -amp;
    }
    amp = amp / 2;
  }
}

void play_rng_effect(int16_t rngSubfile, int16_t frameCount) {
  int16_t effectSpeed = frameCount;
  if (effectSpeed == 0)
    effectSpeed = 88;
  int16_t drawOffset;
  if (flag_battling) {
    draw_enemy_battle_frame();
    drawOffset = 9060;
  } else {
    PAL_NipWSeg((uint8_t *)(global_buf_1 + 64000));
    draw_battle_row_sprite();
    PAL_RripAFreeze();
    check_in_battle(0);
    PAL_NipWSeg(screen_buffer_ptr ? (uint8_t *)(intptr_t)screen_buffer_ptr : NULL);
    PAL_CopyMem(global_buf_1, global_buf_1 + 64000, 64000);
    drawOffset = 10668;
  }
  draw_rng_frame(rngSubfile, effectSpeed, drawOffset);
  if (flag_battling) {
    PAL_CopyMem(global_buf_1, bg_buf, 64000);
    return;
  } else {
    draw_battle_row_sprite();
    return;
  }
}

void draw_rng_frame(int16_t frameDelay, int16_t frameCount, int16_t yPos) {
  int32_t i;
  PAL_PushScreen(screen_buf);
  for (i = 0; i <= frameCount; i++) {
    int32_t frame = rng_anim_frames[i % 6];
    if (i < 6)
      PAL_AddPic0((uint8_t *)screen_buf, (uint8_t *)global_buf_1, yPos, frame);
    else
      PAL_AddPic((uint8_t *)screen_buf, (uint8_t *)global_buf_1, yPos, frame);
    PAL_PopScreen6a(screen_buf, frame, yPos);
    PAL_WaitTime(frameDelay);
    update_shake();
  }
  PAL_PopScreen(global_buf_1);
}

int16_t query_midi_status(void) {
  char mciResult[64] = {0};
  pal_mciSendStringA("status midi mode", mciResult, sizeof(mciResult));
  if (strncmp(mciResult, "playing", 7) == 0)
    return 1;
  else
    return 0;
}

int16_t query_cd_status(void) {
  char mciResult[64] = {0};
  pal_mciSendStringA("status cdtrack mode", mciResult, sizeof(mciResult));
  if (strncmp(mciResult, "playing", 7) == 0)
    return 1;
  else
    return 0;
}

void animate_battle_sprites(int16_t startIdx, int16_t endIdx) {
  int32_t fadeStep, spriteIdx, frameBase, drawX;
  int32_t drawY;
  for (fadeStep = 1; fadeStep <= 15; fadeStep++) {
    PAL_WaitTime(2);
    for (spriteIdx = startIdx; spriteIdx <= endIdx; spriteIdx++) {
      frameBase =
          mgo_frame_offsets[player_battle_sprite[spriteIdx].spriteBase + player_battle_sprite[spriteIdx].direction] +
          player_battle_sprite[spriteIdx].spriteBase;
      drawX = player_battle_sprite[spriteIdx].x - (mgo_frame_offsets[frameBase] / 2);
      drawY = player_battle_sprite[spriteIdx].y - mgo_frame_offsets[frameBase + 1];
      PAL_PutP(drawX, drawY, (const uint8_t *)(mgo_frame_offsets + frameBase), (void *)screen_buffer_ptr, fadeStep, 1);
    }
  }
}

void get_scene_map_source(int16_t sceneID) {
  if (scenes[sceneID].mapNum != curr_scene_id_cache) {
    read_file_and_close("MAP.MKF", scenes[sceneID].mapNum);
    PAL_Decompress((uint8_t *)global_buf_2, (uint8_t *)map_data_buf, (int32_t)sizeof(map_data_buf));
    int32_t gopFile = open_file_required("GOP.MKF");
    get_subfile_len(gopFile, scenes[sceneID].mapNum);
    pal_hread(gopFile, mgo_frame_offsets, tmp_file_size);
    pal_lclose(gopFile);
  }
  curr_scene_id_cache = scenes[sceneID].mapNum;
}

void player_attack_anim(int16_t roleIdx, int16_t hasEffect) {
  int32_t i;
  player_battle_sprite[roleIdx].direction = 0;
  int16_t lungeStep = 4;
  for (i = 1; i <= 4; i++) {
    player_battle_sprite[roleIdx].x -= lungeStep;
    player_battle_sprite[roleIdx].y -= (lungeStep / 2);
    draw_battle_scene(1, 0);
    lungeStep--;
  }
  draw_battle_scene(2, 0);
  player_battle_sprite[roleIdx].direction = 5;
  if (hasEffect) {
    play_sound(playerRoles(party[roleIdx].role, 72), 1);
    play_battle_effect(roleIdx);
  }
  draw_battle_scene(1, 0);
}

void draw_effect_sprites(int16_t useOverlay) {
  const uint16_t *ffd = (const uint16_t *)fire_mkf_data;
  int16_t frameIdx = ffd[effect_frame_count];
  int32_t i;
  int32_t drawX, drawY;
  for (i = 1; i <= effect_particle_count; i++) {
    drawX = effect_x_coords[i] - (ffd[frameIdx] / 2);
    if (useOverlay) {
      drawY = (effect_y_coords[i] - ffd[frameIdx + 1]) - battle_y_offset;
      PAL_PutP(drawX, drawY, (const uint8_t *)(ffd + frameIdx), (void *)global_buf_1, 0, 0);
    } else {
      const uint8_t *bitmap = (const uint8_t *)(ffd + frameIdx);
      PAL_QueueSprite(bitmap, (int16_t)drawX, (int16_t)((effect_y_coords[i] + effect_frames[i]) - battle_y_offset),
                      effect_frames[i], PAL_SpriteHeight(bitmap));
    }
  }
}

void player_attack_execute(int16_t roleIdx, int16_t targetRole) {
  int32_t i;
  double rng;
  PAL_ClearMenu((uint8_t *)battle_order_array, 25);
  int16_t attackTimes;
  if (player_status(roleIdx, 8) > 0)
    attackTimes = 1;
  else
    attackTimes = 0;
  for (i = 0; i <= (uint16_t)attackTimes; i++) {
    player_attack_all((int16_t)roleIdx, attackTimes);
  }
  reset_battle_sprite_pos(roleIdx);
  draw_battle_scene(1, 0);
  apply_enemy_poison_damage();
  rng = VB_rtcRandomNext();
  playerExp.health[targetRole].count = vb_round_banker_i16_d((double)playerExp.health[targetRole].count + rng * 2.0);
  playerExp.attack[targetRole].count++;
}

void cd_stopplay(void) {
  if (music_mode == 2) {
    pal_mciSendStringA("stop cdtrack", NULL, 0);
    pal_mciSendStringA("close cdtrack", NULL, 0);
  }
}

void midi_close(void) {
  if (music_mode != 0 && midi_active != 0) {
    midi_playing = 0;
    midi_active = 0;
    pal_mciSendStringA("stop midi", NULL, 0);
    pal_mciSendStringA("close midi", NULL, 0);
  }
}

void render_scene_and_fade(int16_t fadeSpeed) {
  intptr_t bufPtr = PAL_ArrayPtr(global_buf_1);
  PAL_ClearClipNA(relative_viewport_x, relative_viewport_y, viewport_row_limit, viewport_row_stride, viewport_row_count,
                  (void *)bufPtr, global_buf_2);
  RPG_screen_wave_grade += wave_progression;
  if (RPG_screen_wave_grade > 0 && RPG_screen_wave_grade < 256) {
    PAL_RripA(viewport_row_limit, RPG_screen_wave_grade, global_buf_2);
  } else {
    RPG_screen_wave_grade = 0;
    wave_progression = 0;
  }
  PAL_NTree(relative_viewport_x, global_buf_2);
  if (sprite_frame_count) {
    PAL_NipWB((uint8_t)sprite_frame_count, viewport_row_count, global_buf_2);
  } else {
    PAL_NipWA(frame_counter, viewport_row_count, global_buf_2);
  }
  PAL_WaitTime(10);
  fade_out_palette(fadeSpeed);
  update_shake();
}

void init_walk_frames(void) {
  int32_t i;
  int16_t walkFrames = playerRoles(party[0].role, 64);
  if (walkFrames == 0)
    walkFrames = 3;
  party[0].frame = RPG_team_direction * walkFrames;
  for (i = 1; i <= RPG_team_number; i++) {
    walkFrames = playerRoles(party[i].role, 64);
    if (walkFrames == 0)
      walkFrames = 3;
    party[i].frame = trail[2].direction * walkFrames;
  }
  for (i = 1; i <= RPG_other_peoples; i++) {
    party[RPG_team_number + i].frame = trail[2 + i].direction * 3;
  }
  scene_flags = (scene_flags & 2) ^ 2;
}

void stop_all_and_exit(void) {
  if (init_done_flag == 1) {
    if (midi_active == 1)
      midi_close();
    if (cd_active == 1)
      cd_stopplay();
    PAL_KillTimer();
  }
  exit_flag = 1;
}

void npc_walk_one_step(int16_t npcIdx, int16_t stepCount) {
  int32_t direction = npc_display_data[npcIdx].direction;
  npc_display_data[npcIdx].x += (int16_t)key_pressed_flags[direction] * (stepCount + stepCount);
  npc_display_data[npcIdx].y += (int16_t)key_direction_flags[direction] * stepCount;
  int16_t walkFrames = npc_display_data[npcIdx].spriteFrames;
  if (walkFrames > 0) {
    if (walkFrames == 3)
      walkFrames = 4;
    npc_display_data[npcIdx].currentFrame = (npc_display_data[npcIdx].currentFrame + 1) % walkFrames;
    return;
  } else if (npc_display_data[npcIdx].spriteFramesAuto > 0) {
    npc_display_data[npcIdx].currentFrame =
        (npc_display_data[npcIdx].currentFrame + 1) % npc_display_data[npcIdx].spriteFramesAuto;
    return;
  }
}

void render_scene_with_rng(void) {
  switch (multi_event_state) {
  case 0:
    PAL_ExMap(RPG_viewport_x, RPG_viewport_y, (const uint8_t *)map_data_buf, (uint8_t *)global_buf_2, mgo_frame_offsets);
    return;
  case 2:
    if ((int16_t)multi_event_param <= 0)
      multi_event_param = 1;
    if ((multi_event_param_2 % multi_event_param) == 0) {
      int16_t ratio = multi_event_param_2 / multi_event_param;
      if (ratio < 96) {
        int16_t frame = rng_anim_frames[ratio % 6];
        if (ratio < 6)
          PAL_AddPic0((uint8_t *)global_buf_1, (uint8_t *)mgo_frame_offsets, 10668, frame);
        else
          PAL_AddPic((uint8_t *)global_buf_1, (uint8_t *)mgo_frame_offsets, 10668, frame);
      } else {
        multi_event_state = 1;
      }
    }
    multi_event_param_2++;
    return;
  }
}

void remove_inventory_item(int16_t itemID, int16_t amount) {
  int32_t found = amount;
  int32_t i;
  for (i = 0; i <= 255; i++) {
    if (inventory[i].item == itemID && inventory[i].amount > 0 && found > 0) {
      inventory[i].amountInUse -= found;
      if (inventory[i].amountInUse < 0)
        inventory[i].amountInUse = 0;
      if (inventory[i].amount >= found) {
        inventory[i].amount -= found;
        found = 0;
      } else {
        found -= inventory[i].amount;
        inventory[i].amount = 0;
        inventory[i].item = 0;
      }
    }
  }
  unequip_item(itemID, found);
}

void scene_transition(void) {
  for (;;) {
    flag_battling = 0;
    scene_enter_flag = 0;
    scene_leave_flag = 0;
    if (redraw_flag & 32) {
      LoadRPG_internal(rpg_to_load);
    } else if (RPG_curr_scene != scene_to_load) {
      RPG_screen_wave_grade = 0;
      wave_progression = 0;
      if (RPG_curr_scene > 0)
        load_event_objects();
    }
    multi_event_state = 0;
    team_abstract_x = 160;
    team_abstract_y = 112;
    RPG_curr_scene = scene_to_load;
    if (redraw_flag & 4)
      load_scene_events(RPG_curr_scene);
    get_scene_map_source(RPG_curr_scene);
    load_npc_sprites();
    draw_battle_row_sprite();
    if (redraw_flag & 1)
      load_team_mgo();
    if ((redraw_flag & 8) == 0)
      break;
    redraw_flag = redraw_flag & 2;
    process_Script(0, &scenes[RPG_curr_scene].scriptOnEnter);
    if (RPG_curr_scene == scene_to_load)
      break;
  }
  if (redraw_flag & 2)
    play_all_kinds_music(RPG_music_number, 1);
  redraw_flag = 0;
  init_battle_state();
}

void sort_player_magic(int16_t roleID) {
  int32_t i, j;
  PAL_ClearMenu((uint8_t *)battle_order_array, 150);
  int16_t magicCount = -1;
  for (i = 32; i <= 63; i++) {
    if (playerRoles(roleID, i) > 0) {
      magicCount++;
      battle_order_array[magicCount] = playerRoles(roleID, i);
    }
  }
  for (i = 0; i < magicCount; i++) {
    for (j = i + 1; j <= magicCount; j++) {
      if (battle_order_array[i] > battle_order_array[j]) {
        swap_values((int16_t *)&battle_order_array[i], (int16_t *)&battle_order_array[j]);
      }
    }
  }
  for (i = 0; i <= 31; i++) {
    playerRoles(roleID, i + 32) = battle_order_array[i];
  }
}

void walk_party_fastest(int16_t startFrame, int16_t endFrame, int16_t speed) {
  int32_t i;
  float floatVal = speed ? 100.0f / (float)speed : 0.0f;
  PAL_CopyMem(bg_buf, global_buf_1, 64000);
  for (i = startFrame; i <= endFrame; i++) {
    uint32_t off, next;
    memcpy(file_offset_table, &rng_anim_data[i * 4], 8);
    off = file_offset_table[0];
    next = file_offset_table[1];
    tmp_file_size = (int32_t)(next - off);
    if (tmp_file_size <= 0)
      break;
    PAL_Unpak(rng_anim_data + off, global_buf_1);
    PAL_RngPut((const uint16_t *)global_buf_1);
    PAL_WaitTime(vb_round_banker_u16(floatVal));
    fade_out_palette(1);
    update_shake();
  }
  PAL_CopyMem(global_buf_1, bg_buf, 64000);
}

void play_cd(int16_t trackNum, int16_t musicNum, int16_t trackValid) {
  if (music_mode == 2) {
    midi_close();
    cd_stop();
    if (use_cd_flag) {
      if (trackNum > 0) {
        // NOTE: golden 经 MCI 播 CD-DA；remake 无光驱，经 pal_playCdTrack 回退 RIX
        // musicNum（SDLPAL 0x00A3 约定，与 golden 非 CD 分支同参）。
        pal_playCdTrack(trackNum, musicNum, trackValid);
        cd_track_valid = trackValid;
        cd_active = 1;
        cd_track_num = trackNum;
      }
    }
    return;
  } else {
    play_all_kinds_music(musicNum, trackValid);
    return;
  }
}

int16_t compact_inventory(void) {
  int16_t foundIdx = -1;
  int32_t i;
  inventory_t tmp;
  PAL_ClearMenu((uint8_t *)battle_order_array, 256);
  for (i = 0; i <= 255; i++) {
    if (inventory[i].amount > 99)
      inventory[i].amount = 99;
    if (inventory[i].amount > 0) {
      foundIdx++;
      battle_order_array[foundIdx] = i;
    }
  }
  for (i = 0; (int16_t)i <= foundIdx; i++) {
    tmp = inventory[i];
    inventory[i] = inventory[battle_order_array[i]];
    inventory[battle_order_array[i]] = tmp;
  }
  return foundIdx;
}

void init_battle_state(void) {
  int32_t i, j, k;
  for (i = 0; i <= 255; i++)
    inventory[i].amountInUse = 0;
  PAL_ClearMenu((uint8_t *)equipment_effect, 490);
  for (i = 0; i <= RPG_team_number; i++) {
    k = party[i].role;
    playerRoles(k, 4) = 0;
    player_battle_sprite[i].spriteNum = playerRoles(k, 1);
    player_battle_sprite[i].equipID = playerRoles(k, 65);
    player_status(i, 8) = 0;
    for (j = 11; j <= 16; j++) {
      if (playerRoles(k, j) > 0) {
        process_Script(i, &objects[playerRoles(k, j)].data[3]);
      }
    }
  }
}

int16_t yes_no_menu(int16_t optionFlags, int16_t labelIdx) {
  int32_t menuPosX = 120, menuPosY = 100;
  int16_t selection = -1;
  int16_t isYes = optionFlags & 1;
  int16_t inputResult = -2;
  int32_t i, keyResult, tmpX;
  while (inputResult == -2) {
    tmpX = menuPosX;
    for (i = 0; i <= 1; i++) {
      frame_menu(tmpX, menuPosY, 2, selection);
      if (i == isYes) {
        draw_text_at(tmpX + 15, menuPosY + 9, labelIdx + i, 250);
      } else {
        draw_string(tmpX + 16, menuPosY + 10, labelIdx + i, 3, 0);
      }
      tmpX += 80;
    }
    selection = 0;
    keyResult = read_key();
    if (keyResult == 1)
      inputResult = -1;
    if (keyResult == 2)
      inputResult = isYes;
    if (keyResult == 5 || keyResult == 6)
      isYes = 1 - isYes;
  }
  return inputResult;
}

int16_t select_enemy_target(void) {
  int16_t i;
  int32_t j, k;
  if (count_alive_enemies() <= 1) {
    i = 0;
    for (j = 0; j <= team_number; j++) {
      if (enemy_battle_data[j].hp > 0)
        i = j;
    }
  } else {
    multi_event_param_3 = 0;
    i = -2;
    k = 1;
    while (i == -2) {
      // NOTE: 同 read_key：remake 增补让出防灼 CPU（golden 靠 DoEvents 交还）。
      kitty_delay_ms(1);
      key_pressed = check_key_pressed();
      if (key_pressed == 5) {
        multi_event_param_3--;
        k = -1;
      }
      if (key_pressed == 6) {
        multi_event_param_3++;
        k = 1;
      }
      if (multi_event_param_3 < 0)
        multi_event_param_3 = team_number;
      if (multi_event_param_3 > team_number)
        multi_event_param_3 = 0;
      while (enemy_battle_data[multi_event_param_3].hp <= 0) {
        multi_event_param_3 =
            (uint16_t)(((int32_t)multi_event_param_3 + k + (int32_t)enemy_max_id) % (int32_t)enemy_max_id);
      }
      if (key_pressed == 1)
        i = -1;
      if (key_pressed == 2)
        i = multi_event_param_3;
      draw_battle_scene(1, 0);
    }
    multi_event_param_3 = -1;
  }
  return i;
}

void get_sprites_curr_scene(void) {
  int32_t i, layerVal, spriteNum, frameCount;
  int16_t npcX, npcY;
  for (i = 1; i <= curr_scene_event_count; i++) {
    if (npc_display_data[i].state > 0) {
      npcX = npc_display_data[i].x - RPG_viewport_x;
      npcY = npc_display_data[i].y - RPG_viewport_y;
      layerVal = npc_display_data[i].layer * 8;
      if (npcX >= -64 && npcY >= 0 && npcX <= 384 && npcY <= 328) {
        spriteNum = npc_display_data[i].currentFrame;
        if (npc_display_data[i].spriteFrames == 3) {
          if (spriteNum == 2)
            spriteNum = 0;
          if (spriteNum == 3)
            spriteNum = 2;
          frameCount = npc_display_data[i].direction * 3;
        } else {
          frameCount = npc_display_data[i].direction * npc_display_data[i].spriteFrames;
        }
        if (npc_display_data[i].spriteNum > 0) {
          add_sprite_to_tree(npcX, npcY, layerVal, i, frameCount + spriteNum, 1);
        }
      }
    }
  }
}

void menu_system(void) {
  int32_t tmp2, result, result2;
  tmp2 = menu_loop(&(int16_t){0}, 40, 60, 11, 4, 5);
  switch (tmp2) {
  case 0:
    result = check_save_file();
    if (result >= 0)
      SaveRPG_internal(result + 1);
    return;
  case 1:
    result = check_save_file();
    if (result >= 0)
      LoadRPG_internal(result + 1);
    return;
  case 2:
    result = use_cd_flag;
    result2 = yes_no_menu(result, 17);
    if (result2 >= 0) {
      use_cd_flag = result2;
      if (use_cd_flag == 0) {
        cd_stop();
        midi_close();
      } else {
        play_all_kinds_music(music_track_arg, music_mode_arg);
      }
    }
    return;
  case 3:
    result = has_sfx;
    result2 = yes_no_menu(result, 17);
    if (result2 >= 0)
      has_sfx = result2;
    return;
  case 4:
    result = 0;
    result2 = yes_no_menu(result, 19);
    if (result2 == 1)
      release_resources_exit();
    return;
  }
}

int16_t select_party_member(void) {
  int16_t selection = -2, selectedIndex = 0;
  int16_t keyResult;
  push_screen_buffer();
  PAL_CopyMem(global_buf_2, menu_bg_data, menu_bg_size);
  while (selection == -2) {
    int32_t selX;
    if (selectedIndex > RPG_team_number)
      selectedIndex = RPG_team_number;
    if (selectedIndex < 0)
      selectedIndex = 0;
    selX = (uint16_t)(75 + selectedIndex * 80);
    PAL_CopyMem(global_buf_1, screen_buf, 64000);
    PAL_PutP((int16_t)selX, 158, (const uint8_t *)(global_buf_2 + global_buf_2[67]), (void *)global_buf_1, 0, 0);
    PAL_PopScreen(global_buf_1);
    keyResult = read_key();
    if (keyResult == 5)
      selectedIndex--;
    if (keyResult == 6)
      selectedIndex++;
    if (keyResult == 1) {
      selection = -1;
      selectedIndex = -1;
    }
    if (keyResult == 2)
      selection = selectedIndex;
  }
  restore_screen();
  return selectedIndex;
}

void play_theurgy_anim(int16_t magicIdx, int16_t startFrame, int16_t minFrames) {
  int32_t i;
  int16_t j;
  copy_subfile_data(magicIdx);
  if (minFrames < 9)
    play_sound(theurgy_data.sound, 0);
  theurgy_effect_max = theurgy_data.wave;
  for (i = 0; i <= theurgy_data.effectTimes; i++) {
    effect_frame_count = startFrame;
    while (fire_mkf_data[effect_frame_count] > 0) {
      draw_battle_scene(1, theurgy_data.speed);
      effect_frame_count++;
    }
  }
  effect_frame_count--;
  battle_y_offset = 16;
  j = 10;
  for (i = 1; i <= theurgy_data.shake; i++) {
    draw_battle_scene(1, 0);
    j = 10 - j;
    battle_y_offset = (uint16_t)VB_Int(VB_rtcRandomNext() * 6.0) + j;
  }
  battle_y_offset = 0;
  theurgy_effect_max = 0;
  if (theurgy_data.keepEffect && theurgy_effect_count < 9) {
    draw_effect_sprites(-1);
    PAL_CopyMem(bg_buf, global_buf_1, 64000);
  }
}

void show_party_hp_mp_change(int16_t *theurgyType) {
  int32_t i, roleID;
  int16_t hpDiff, j;
  for (i = 0; i <= enemy_pos_count; i++) {
    roleID = party[i].role;
    hpDiff = playerRoles(roleID, 9) - battle_role_data_ext[i][0];
    if (hpDiff != 0) {
      j = (hpDiff > 0) ? 0 : 1;
      add_damage_number(player_battle_sprite[i].x, player_battle_sprite[i].y - 70, VB_Abs(hpDiff), j);
      *theurgyType = -1;
    }
    hpDiff = playerRoles(roleID, 10) - battle_role_data_ext[i][1];
    if (hpDiff > 0) {
      add_damage_number(player_battle_sprite[i].x, player_battle_sprite[i].y - 62, hpDiff, 2);
      *theurgyType = -1;
    }
    if ((int16_t)playerRoles(roleID, 9) < 0)
      playerRoles(roleID, 9) = 0;
    if ((int16_t)playerRoles(roleID, 10) < 0)
      playerRoles(roleID, 10) = 0;
  }
}

void load_npc_sprites(void) {
  int32_t dup = 0, i, j, spriteNum, found, tmp, overlap;
  for (i = 1; i <= curr_scene_event_count; i++) {
    spriteNum = npc_display_data[i].spriteNum;
    if (spriteNum > 0) {
      found = (uint16_t)-1;
      for (j = 1; j <= i - 1; j++) {
        if (spriteNum == npc_display_data[j].spriteNum)
          found = j;
      }
      if (found == (uint16_t)-1) {
        load_subfile_to_buf(spriteNum);
        tmp = PAL_PakSize((uint8_t *)global_buf_2);
        if (dup + tmp > 0) {
          PAL_Decompress((uint8_t *)global_buf_2, (uint8_t *)&fire_mkf_data[dup], 64000);
          npc_display_data[i].spritePtrOffset = dup;
          dup += tmp;
        } else {
          npc_display_data[i].spriteNum = 0;
        }
      } else {
        npc_display_data[i].spritePtrOffset = npc_display_data[found].spritePtrOffset;
      }
    }
    overlap = 0;
    while (fire_mkf_data[npc_display_data[i].spritePtrOffset + overlap] > 0)
      overlap++;
    npc_display_data[i].spriteFramesAuto = overlap;
    if (overlap == 0)
      npc_display_data[i].spriteNum = 0;
  }
  battle_extra_flag = dup;
}

void stop_app_and_music(void) {
  PAL_StopApp(0);
  if (midi_active == 1)
    pal_mciSendStringA("play midi", NULL, 0);
  if (cd_active == 1) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "play cdtrack from%u to%u", cd_track_num, cd_track_num + 1);
    pal_mciSendStringA(cmd, NULL, 0);
  }
  exit_flag = 0;
}

void show_equip_detail(int16_t roleID, int16_t equipIdx) {
  char name[128], equip[128], tail[128], combined[384];
  frame_menu(78, 0, 10, -1);
  memcpy(name, word_dat_data[playerRoles(roleID, 3)], sizeof(word_dat_data[0]));
  memcpy(equip, word_dat_data[48], sizeof(word_dat_data[0]));
  memcpy(tail, word_dat_data[32], sizeof(word_dat_data[0]));
  name[sizeof(word_dat_data[0])] = 0;
  equip[sizeof(word_dat_data[0])] = 0;
  tail[sizeof(word_dat_data[0])] = 0;
  trim_string(name);
  trim_string(equip);
  trim_string(tail);
  snprintf(combined, sizeof(combined), "%s%s%s", name, equip, tail);
  PAL_DrawString((const char *)combined, 102, 8, 3, 0, (void *)screen_buffer_ptr);
  draw_menu_table(72, 34, 9, 10, 8, -1);
  show_role_attributes(88, 47, roleID, -1);
  level_up_player(roleID, equipIdx, -1);
  show_role_attributes(169, 47, roleID, 0);
  wait_frame(200);
}

void use_item_menu(void) {
  int16_t i, j, selection;
  int16_t itemID, itemObjID;
  int16_t cursorPos = 0;
  int16_t cursorIdx = 0;
  int16_t keyResult;
  int32_t tmp = 0;

  show_status_icons();
  i = menu_select_party(36, 64, -1);
  if (i < 0)
    return;
  for (;;) {
  L_0040AF7C:
    j = party[i].role;
    itemID = select_theurgy(j, &cursorIdx, 1);
    if (itemID < 0)
      return;
    itemObjID = objects[itemID].data[0];
    PAL_GetBin((uint16_t *)&cursorPos, objects[itemID].data[6], 4);
    if (cursorPos == 0) {
      selection = select_party_member();
      if (selection < 0)
        goto L_0040AF7C;
    } else {
      selection = j;
    }
    for (keyResult = 0; keyResult <= RPG_team_number; keyResult++)
      tmp += playerRoles(keyResult, 9);
    process_Script(selection, &objects[itemID].data[2]);
    copy_subfile_data(itemObjID);
    if (redraw_hp_mp_flag)
      playerRoles(j, 10) -= theurgy_data.costMP;
    if (palette_fade_active != 0)
      return;
    for (keyResult = 0; keyResult <= RPG_team_number; keyResult++)
      tmp -= playerRoles(keyResult, 9);
    if (tmp == 0)
      return;
    show_status_icons();
  }
}

void load_team_mgo(void) {
  int32_t i = 0, j, k, roleID, l, found;
  for (j = 0; j <= RPG_team_number; j++) {
    roleID = playerRoles(party[j].role, 2);
    l = (uint16_t)-1;
    for (k = 0; k <= j - 1; k++) {
      if (roleID == playerRoles(party[k].role, 2))
        l = k;
    }
    if (l == (uint16_t)-1) {
      load_subfile_to_buf(roleID);
      found = PAL_PakSize((uint8_t *)global_buf_2);
      PAL_Decompress((uint8_t *)global_buf_2, (uint8_t *)&word_glyph_index[i], 64000);
      party[j].imageOffset = i;
      i += found;
    } else {
      party[j].imageOffset = party[l].imageOffset;
    }
  }
  for (k = 1; k <= RPG_other_peoples; k++) {
    j = RPG_team_number + k;
    load_subfile_to_buf(party[j].role);
    found = PAL_PakSize((uint8_t *)global_buf_2);
    PAL_Decompress((uint8_t *)global_buf_2, (uint8_t *)&word_glyph_index[i], 64000);
    party[j].imageOffset = i;
    i += found;
  }
}

void load_enemy_sprites(void) {
  int32_t i = 0, j, k, l, spriteIdx, found;
  for (j = 0; j <= team_number; j++) {
    if (enemy_battle_data[j].hp > 0 && enemy_battle_data[j].objectID > 0) {
      l = enemy_battle_data[j].enemyID;
      spriteIdx = (uint16_t)-1;
      for (k = 0; k <= j - 1; k++) {
        if (enemy_battle_data[k].hp > 0 && l == enemy_battle_data[k].enemyID)
          spriteIdx = k;
      }
      if (spriteIdx == (uint16_t)-1) {
        get_subfile_len(fh_ABC_MKF, l);
        pal_hread(fh_ABC_MKF, global_buf_2, tmp_file_size);
        found = PAL_PakSize((uint8_t *)global_buf_2);
        PAL_Decompress((uint8_t *)global_buf_2, (uint8_t *)&map_data_buf[i], (int32_t)(sizeof(map_data_buf) - i * 2));
        enemy_battle_data[j].tileData = map_data_buf[map_data_buf[i] + i + 1];
        enemy_battle_data[j].mapValue = i;
        i += found;
      } else {
        enemy_battle_data[j].tileData = enemy_battle_data[spriteIdx].tileData;
        enemy_battle_data[j].mapValue = enemy_battle_data[spriteIdx].mapValue;
      }
    }
  }
  init_enemy_positions();
}

void scroll_scene_with_fbp(int16_t fbpSubfile, int16_t rngSubfile, int16_t speed) {
  int32_t j, frameIdx = 0, fadeStep = 0;
  int16_t frameCount = 200;
  intptr_t bufPtr = 0, offset = 0;
  float stepDelay = speed ? 100.0f / (float)speed : 0.0f;

  load_fbp_two_scene(fbpSubfile);
  if (rngSubfile > 0) {
    load_subfile_to_buf(rngSubfile);
    PAL_Unpak(global_buf_2, (uint8_t *)fire_mkf_data);
  }
  for (j = 1; j <= 200; j++) {
    frameCount--;
    if (frameCount >= 0) {
      bufPtr = PAL_ArrayPtr(global_buf_1);
      offset = bufPtr + frameCount * 320;
    }
    PAL_ClearClipNA(0, 0, viewport_row_limit, viewport_row_stride, viewport_row_limit, (void *)offset, global_buf_2);
    if (rngSubfile != 0) {
      PAL_PutIpNA(0, 0, 0, (int16_t)viewport_row_limit, &fire_mkf_data[fire_mkf_data[frameIdx]],
                  (intptr_t)global_buf_2);
      frameIdx++;
      if (fire_mkf_data[frameIdx] == 0)
        frameIdx = 0;
    }
    if (palette_fade_active && fadeStep <= 64) {
      fadeStep++;
      PAL_ExPalate((uint8_t *)&palette_data[768], (const uint8_t *)palette_data, 768, fadeStep);
      PAL_IntPalate((uint8_t *)&palette_data[768]);
    }
    PAL_NipWA(0, viewport_row_limit, global_buf_2);
    PAL_WaitTime(vb_round_banker_u16(stepDelay));
  }
  palette_fade_active = 0;
}

void play_theurgy_rng_anim(void) {
  int32_t i;
  PAL_CopyMem(global_buf_1, bg_buf, 64000);
  if (npc_dir_frame && theurgy_data.effectTimes != 0) {
    PAL_PopScreenB((uint16_t *)global_buf_1, theurgy_data.effectTimes);
  }
  draw_enemy_battle_frame();
  PAL_PushScreen((uint8_t *)fire_mkf_data);
  for (i = 0; i <= 46; i++) {
    int16_t frame = rng_anim_frames[i % 6];
    if (i < 6) {
      PAL_AddPic0((uint8_t *)fire_mkf_data, (uint8_t *)global_buf_1, 10668, frame);
    } else {
      PAL_AddPic((uint8_t *)fire_mkf_data, (uint8_t *)global_buf_1, 10668, frame);
      PAL_AddPic((uint8_t *)fire_mkf_data, (uint8_t *)global_buf_1, 10668, frame);
      PAL_PopScreen6(fire_mkf_data, frame);
    }
    PAL_WaitTime(2);
  }
  PAL_CopyMem(global_buf_1, bg_buf, 64000);
  if (npc_dir_frame && theurgy_data.effectTimes != 0) {
    PAL_PopScreenB((uint16_t *)global_buf_1, theurgy_data.effectTimes);
  }
}

void add_magic_to_player(int16_t roleID, int16_t magicID, int16_t magicType) {
  int16_t i;
  int16_t foundIdx = 0;
  int16_t magicNotFound = -1;
  for (i = 63; i >= 32; i--) {
    if (playerRoles(roleID, (uint16_t)i) == magicID)
      magicNotFound = 0;
    if (playerRoles(roleID, (uint16_t)i) == 0)
      foundIdx = (uint16_t)i;
  }
  if (magicNotFound && foundIdx > 0) {
    playerRoles(roleID, foundIdx) = magicID;
    if (magicType) {
      frame_menu(60, 100, 11, -1);
      draw_string(70, 110, playerRoles(roleID, 3), 3, 0);
      draw_string(118, 110, 33, 3, 0);
      draw_string(158, 110, magicID, 3, 25);
      wait_frame(180);
    }
  }
}

int16_t menu_select(int16_t *cursorPos, int16_t x, int16_t y, int16_t midBlocks, int16_t rowCount) {
  int16_t maxIdx = (int16_t)rowCount - 1;
  int16_t selection = -2;
  int16_t i, j;
  draw_menu_table(x, y, 0, midBlocks, rowCount, -1);
  while (selection == -2) {
    int16_t rowY = (int16_t)y + 12;
    int16_t textX = (int16_t)x + 13;
    if (*cursorPos < 0)
      *cursorPos = maxIdx;
    if (*cursorPos > maxIdx)
      *cursorPos = 0;
    for (i = 0; i <= maxIdx; i++) {
      if (battle_order_array[i + 100])
        j = (i == *cursorPos) ? 250 : 78;
      else
        j = (i == *cursorPos) ? 28 : 24;
      draw_text_at((uint16_t)textX, (uint16_t)rowY, battle_order_array[i], (uint16_t)j);
      rowY += 18;
    }
    j = read_key();
    if (j == 1)
      selection = -1;
    if (j == 2 && battle_order_array[*cursorPos + 100]) {
      selection = *cursorPos;
      draw_text_at(x + 13, y + 12 + selection * 18, battle_order_array[selection], 43);
    }
    if (j == 3)
      (*cursorPos)--;
    if (j == 4)
      (*cursorPos)++;
  }
  return selection;
}

void draw_player_status(int16_t x, int16_t y, int16_t roleID, int16_t highlight) {
  int16_t glyphOffset = global_buf_2[48 + roleID];
  if (highlight == 0)
    PAL_PutP(x - 3, y, (const uint8_t *)(global_buf_2 + glyphOffset), (void *)screen_buffer_ptr, 0, 0);
  else
    PAL_PutP(x - 3, y, (const uint8_t *)(global_buf_2 + glyphOffset), (void *)screen_buffer_ptr, highlight, 2);
  PAL_PutP(x + 47, y + 11, (const uint8_t *)(global_buf_2 + global_buf_2[39]), (void *)screen_buffer_ptr, 0, 0);
  PAL_PutP(x + 47, y + 27, (const uint8_t *)(global_buf_2 + global_buf_2[39]), (void *)screen_buffer_ptr, 0, 0);
  display_number(x + 64, y + 13, playerRoles(roleID, 7), 0);
  display_number(x + 64, y + 29, playerRoles(roleID, 8), 2);
  display_number(x + 42, y + 9, playerRoles(roleID, 9), 0);
  display_number(x + 42, y + 25, playerRoles(roleID, 10), 2);
}

void draw_menu_table(int16_t x, int16_t y, int16_t wordIdx, int16_t midBlocks, int16_t rowCount, int16_t highlight) {
  int16_t i, j, k, l;
  int16_t rowY, blockIdx;
  if (midBlocks < 0)
    return;
  PAL_CopyMem(global_buf_2, menu_bg_data, menu_bg_size);
  i = (int16_t)(rowCount - 2);
  j = (int16_t)(midBlocks - 2);
  rowY = y;
  save_temp_buf[0] = wordIdx;
  blockIdx = 1;
  for (k = 0; k <= j; k++) {
    save_temp_buf[blockIdx] = wordIdx + 1;
    blockIdx++;
  }
  save_temp_buf[blockIdx] = wordIdx + 2;
  draw_menu_frame(x, rowY, blockIdx, highlight);
  rowY += 20;
  for (l = 0; l <= i; l++) {
    save_temp_buf[0] = wordIdx + 3;
    blockIdx = 1;
    for (k = 0; k <= j; k++) {
      save_temp_buf[blockIdx] = wordIdx + 4;
      blockIdx++;
    }
    save_temp_buf[blockIdx] = wordIdx + 5;
    draw_menu_frame(x, rowY, blockIdx, highlight);
    rowY += 18;
  }
  save_temp_buf[0] = wordIdx + 6;
  blockIdx = 1;
  for (k = 0; k <= j; k++) {
    save_temp_buf[blockIdx] = wordIdx + 7;
    blockIdx++;
  }
  save_temp_buf[blockIdx] = wordIdx + 8;
  draw_menu_frame(x, rowY, blockIdx, highlight);
}

void draw_role_battle_frame(void) {
  int16_t i, frameIdx, drawX, jitter;
  for (i = enemy_pos_count; i >= 0; i--) {
    frameIdx = mgo_frame_offsets[player_battle_sprite[i].spriteBase + player_battle_sprite[i].direction] +
               player_battle_sprite[i].spriteBase;
    drawX = player_battle_sprite[i].x - (mgo_frame_offsets[frameIdx] / 2);
    if (player_battle_sprite[i].direction == 0 && player_status(i, 0)) {
      jitter = (int16_t)vb_round_banker_u16(VB_rtcRandomNext());
    } else {
      jitter = 0;
    }
    if (battle_select_max == 0) {
      const uint8_t *bitmap = (const uint8_t *)(mgo_frame_offsets + frameIdx);
      PAL_QueueSprite(bitmap, (int16_t)drawX,
                      (int16_t)((player_battle_sprite[i].y - jitter) + player_battle_sprite[i].animOffset),
                      player_battle_sprite[i].animOffset, PAL_SpriteHeight(bitmap));
    }
    if (battle_role_idx_2) {
      if (i == battle_target_cursor) {
        const uint8_t *bitmap = (const uint8_t *)(word_glyph_index + word_glyph_index[66 + (effect_sub_count & 1)]);
        PAL_QueueSprite(bitmap, (int16_t)(player_battle_sprite[i].x - 8), (int16_t)player_battle_sprite[i].y, 61,
                        PAL_SpriteHeight(bitmap));
      }
      if (i == battle_curr_role_idx) {
        const uint8_t *bitmap = (const uint8_t *)(word_glyph_index + word_glyph_index[68 + (effect_sub_count & 1)]);
        PAL_QueueSprite(bitmap, (int16_t)(player_battle_sprite[i].x - 8), (int16_t)player_battle_sprite[i].y, 68,
                        PAL_SpriteHeight(bitmap));
      }
    }
  }
}

void load_enemy_data(int16_t enemyIdx, int16_t objectID) {
  enemy_battle_data[enemyIdx].objectID = objectID;
  if (objectID > 0) {
    enemy_battle_data[enemyIdx].enemyID = objects[objectID].data[0];
    memcpy(&enemy_runtime_data[enemyIdx], data_enemy + objects[objectID].data[0] * 70, sizeof(enemy_t));
    enemy_battle_data[enemyIdx].hp = enemy_runtime_data[enemyIdx].health;
    enemy_battle_data[enemyIdx].useScript = objects[objectID].data[2];
    enemy_battle_data[enemyIdx].battleStartScript = objects[objectID].data[3];
    enemy_battle_data[enemyIdx].battleEndScript = objects[objectID].data[4];
    if (enemy_runtime_data[enemyIdx].exp > (32767 - battle_max_hp_sum)) {
      battle_max_hp_sum = 32767;
    } else {
      battle_max_hp_sum += enemy_runtime_data[enemyIdx].exp;
    }
    battle_action_type += enemy_runtime_data[enemyIdx].cash;
    PAL_LoadDSound(enemy_runtime_data[enemyIdx].attackSound, 0);
    PAL_LoadDSound(enemy_runtime_data[enemyIdx].actionSound, 0);
    PAL_LoadDSound(enemy_runtime_data[enemyIdx].magicSound, 0);
    PAL_LoadDSound(enemy_runtime_data[enemyIdx].deathSound, 0);
    PAL_LoadDSound(enemy_runtime_data[enemyIdx].callSound, 0);
  }
}

void fade_in_pic(int16_t fbpSubfile, int16_t rngSubfile, int16_t speed) {
  int16_t i, frameCount = 0, frameIdx = 0;
  float floatVal = speed ? 100.0f / (float)speed : 0.0f;

  read_file_and_close("FBP.MKF", fbpSubfile);
  PAL_Decompress((uint8_t *)global_buf_2, (uint8_t *)mgo_frame_offsets, 64000);
  if (rngSubfile > 0) {
    load_subfile_to_buf(rngSubfile);
    PAL_Decompress((uint8_t *)global_buf_2, (uint8_t *)fire_mkf_data, 64000);
  }
  multi_event_state = 2;
  multi_event_param = 1;
  multi_event_param_2 = 0;
  for (i = 1; i <= 100; i++) {
    intptr_t bufPtr;
    render_scene_with_rng();
    bufPtr = PAL_ArrayPtr(global_buf_1);
    PAL_ClearClipNA(0, 0, viewport_row_limit, viewport_row_stride, viewport_row_limit, (void *)bufPtr, global_buf_2);
    if (rngSubfile != 0) {
      PAL_PutIpNA(0, 0, 0, (int16_t)viewport_row_limit, fire_mkf_data + fire_mkf_data[frameIdx],
                  (intptr_t)global_buf_2);
      frameIdx++;
      if (fire_mkf_data[frameIdx] == 0)
        frameIdx = 0;
    }
    if (palette_fade_active && frameCount <= 64) {
      frameCount++;
      PAL_ExPalate((uint8_t *)&palette_data[768], (const uint8_t *)palette_data, 768, frameCount);
      PAL_IntPalate((uint8_t *)&palette_data[768]);
    }
    PAL_NipWA(0, viewport_row_limit, global_buf_2);
    PAL_WaitTime(vb_round_banker_u16(floatVal));
  }
  palette_fade_active = 0;
}

void update_player_battle_status(void) {
  int16_t i, roleID;
  for (i = 0; i <= enemy_pos_count; i++) {
    roleID = party[i].role;
    player_battle_sprite[i].direction = player_battle_sprite[i].actionState;
    battle_role_action[i].flag = 0;
    if ((int16_t)playerRoles(roleID, 9) < 100 &&
        (int16_t)playerRoles(roleID, 9) <= (int16_t)playerRoles(roleID, 7) / 5) {
      player_battle_sprite[i].direction = 1;
    }
    if (player_status(i, 2))
      player_battle_sprite[i].direction = 1;
    if (player_status(i, 4)) {
      player_battle_sprite[i].direction = 0;
    } else if ((int16_t)playerRoles(roleID, 9) <= 0) {
      player_battle_sprite[i].direction = 2;
      if (player_battle_sprite[i].origDirection != 2)
        battle_role_action[i].flag = -1;
    }
    player_battle_sprite[i].origDirection = player_battle_sprite[i].direction;
    if (player_battle_sprite[i].direction <= 1) {
      player_battle_sprite[i].x = player_battle_sprite[i].origX;
      player_battle_sprite[i].y = player_battle_sprite[i].origY;
    }
  }
}

void cast_theurgy_anim(int16_t itemID) {
  int32_t i, j, k;
  shake_active = -1;
  int16_t subfileNum = objects[itemID].data[0];
  copy_subfile_data(subfileNum);
  play_sound(theurgy_data.sound, 0);
  draw_text_at(200, 50, itemID, 14);
  attack_done_flag = 0;
  animate_battle_sprites(0, enemy_pos_count);
  read_file_to_buf(theurgy_data.layerOffset + 10, 0);
  PAL_WaitTime(5);
  curr_npc_idx = 0;
  npc_curr_frame = 240 + theurgy_data.xOffset;
  npc_direction = 165 + theurgy_data.yOffset;
  play_theurgy_rng_anim();
  k = curr_npc_idx;
  for (i = 1; i <= 2; i++) {
    curr_npc_idx = k;
    for (j = 1; j <= theurgy_data.speed; j++) {
      draw_battle_scene(2, 0);
      curr_npc_idx++;
    }
  }
  for (j = 1; j <= theurgy_data.keepEffect; j++) {
    draw_battle_scene(4, 0);
    curr_npc_idx++;
  }
  load_theurgy_image(theurgy_data.effect);
  PAL_WaitTime(20);
  for (j = 1; j <= theurgy_data.fireDelay - 1; j++) {
    draw_battle_scene(1, 0);
    curr_npc_idx++;
  }
  shake_active = 0;
}

void show_role_attributes(int16_t x, int16_t y, int16_t roleID, int16_t showEquip) {
  int32_t i;
  int16_t result;
  int16_t yCoord;
  display_number(x + 58, y + 4, playerRoles(roleID, 6), 0);
  yCoord = y;
  for (i = 48; i <= 55; i++) {
    if (showEquip)
      draw_text_at(x, yCoord, i, 187);
    else
      PAL_PutP(x + 16, yCoord + 6, (const uint8_t *)(global_buf_2 + global_buf_2[47]), (void *)screen_buffer_ptr, 0, 0);
    yCoord += 18;
  }
  yCoord = y + 20;
  for (i = 7; i <= 8; i++) {
    display_number(x + 58, yCoord, playerRoles(roleID, i + 2), 0);
    PAL_PutP(x + 62, yCoord + 3, (const uint8_t *)(global_buf_2 + global_buf_2[39]), (void *)screen_buffer_ptr, 0, 0);
    display_number(x + 78, yCoord + 6, playerRoles(roleID, i), 1);
    yCoord += 18;
  }
  yCoord = y + 58;
  for (i = 17; i <= 21; i++) {
    result = get_player_attribute_total(roleID, i);
    display_number(x + 58, yCoord, result, 0);
    yCoord += 18;
  }
}

void redraw_tile(int16_t x, int16_t y, int16_t right, int16_t bottom, int16_t tileX, int16_t tileY) {
  int16_t tileCol, endTileCol, tileRow, endTileRow;
  int16_t xPixelOffset, yPixelOffset;
  int32_t i, j, k;
  int16_t screenY, screenX;
  uint16_t tileData2, tileData, tileFrame, tileFrame2;
  intptr_t bufPtr;

  PAL_VWindow(x, y, right, bottom);
  tileCol = tileX / 32;
  xPixelOffset = (int16_t)(-(int16_t)(tileX % 32) - 16);
  endTileCol = (((tileX + right) - x) + 16) / 32;
  tileRow = tileY / 16;
  yPixelOffset = (int16_t)(-(int16_t)(tileY % 16) - 8);
  endTileRow = (((tileY + bottom) - y) + 8) / 16;
  screenY = (int16_t)(y + yPixelOffset);
  for (i = tileRow; i <= endTileRow; i++) {
    for (j = 0; j <= 1; j++) {
      screenX = (int16_t)((x + xPixelOffset) + (j * 16));
      for (k = tileCol; k <= endTileCol; k++) {
        PAL_ExGop((const uint8_t *)map_data_buf, j, k, i, &tileData, &tileData2);
        PAL_CvLong(mgo_frame_offsets[tileData], &tileFrame);
        bufPtr = PAL_ArrayPtr(global_buf_1);
        PAL_PutP(screenX, screenY, (const uint8_t *)(mgo_frame_offsets + tileFrame), (void *)bufPtr, 0, 0);
        if (tileData2 > 0) {
          PAL_CvLong(mgo_frame_offsets[tileData2 - 1], &tileFrame2);
          bufPtr = PAL_ArrayPtr(global_buf_1);
          PAL_PutP(screenX, screenY, (const uint8_t *)(mgo_frame_offsets + tileFrame2), (void *)bufPtr, 0, 0);
        }
        screenX += 32;
      }
      screenY += 8;
    }
  }
}

void update_enemy_battle_anim(void) {
  int16_t i;
  for (i = team_number; i >= 0; i--) {
    int16_t idleSpeed = enemy_runtime_data[i].idleAnimSpeed;
    int16_t spriteIdx, width;
    // NOTE: height 为 remake 增补参数（golden AddToTree 由 DLL 内部解析精灵头）。
    uint16_t height;
    int16_t x, tmp = 0;

    if (!((enemy_battle_data[i].hp > 0) || (battle_enemy_data_ext[i] > 0)) || idleSpeed == 0)
      continue;
    if (attack_done_flag && idleSpeed < 99 && (enemy_status(i, 1) + enemy_status(i, 2)) == 0 &&
        effect_sub_count % idleSpeed == 0) {
      // NOTE: 安全偏差：golden `Mod idleFrames` 无零守卫（idleFrames=0 时 VB
      // 除零崩溃），remake 跳过帧推进（已登记 remake.md）。
      if (enemy_runtime_data[i].idleFrames > 0)
        enemy_battle_data[i].direction = (enemy_battle_data[i].direction + 1) % enemy_runtime_data[i].idleFrames;
    }
    spriteIdx =
        map_data_buf[enemy_battle_data[i].mapValue + enemy_battle_data[i].direction] + enemy_battle_data[i].mapValue;
    width = map_data_buf[spriteIdx];
    height = map_data_buf[spriteIdx + 1];
    x = enemy_battle_data[i].x - (width / 2);
    if (battle_role_idx_2 && (effect_sub_count & 1) && i == multi_event_param_3)
      player_hit_flags[i] = (uint16_t)-1;
    if (enemy_status(i, 0))
      tmp = (int16_t)vb_round_banker_i16_d(VB_rtcRandomNext() * 3.0);
    PAL_QueueSprite((const uint8_t *)(map_data_buf + spriteIdx), (int16_t)(x + tmp + battle_dest_x),
                    (int16_t)(enemy_battle_data[i].y - battle_y_offset + (battle_dest_x / 2)), 0, height);
  }
}

int16_t select_direction_menu(uint16_t *direction) {
  int16_t result = -2, keyResult;
  int32_t i;
  while (result == -2) {
    // NOTE: 同 read_key：remake 增补让出防灼 CPU（golden 靠 DoEvents 交还）。
    kitty_delay_ms(1);
    keyResult = check_key_pressed();
    if (keyResult == 3)
      *direction = 0;
    if (keyResult == 5 && battle_order_array[1] == 0)
      *direction = 1;
    if (keyResult == 6 && battle_order_array[2] == 0)
      *direction = 2;
    if (keyResult == 4)
      *direction = 3;
    if (keyResult == 2)
      result = (int16_t)*direction;
    if (keyResult == 1)
      result = -1;
    if (keyResult >= 9)
      result = -keyResult;
    if (*direction > 0 && *direction <= 3) {
      if (battle_order_array[*direction])
        *direction = 0;
    }
    for (i = 0; i <= 3; i++) {
      if (battle_order_array[i]) {
        PAL_PutP(key_scan_map[i], key_action_map[i], (const uint8_t *)(word_glyph_index + word_glyph_index[40 + i]),
                 (void *)screen_buffer_ptr, 20, 2);
      } else if (i == *direction) {
        PAL_PutP(key_scan_map[i], key_action_map[i], (const uint8_t *)(word_glyph_index + word_glyph_index[40 + i]),
                 (void *)screen_buffer_ptr, 0, 0);
      } else {
        PAL_PutP(key_scan_map[i], key_action_map[i], (const uint8_t *)(word_glyph_index + word_glyph_index[40 + i]),
                 (void *)screen_buffer_ptr, 4, 2);
      }
    }
    draw_battle_scene(1, 0);
  }
  return result;
}

void calc_player_attack_damage(int16_t targetRole, int16_t attackerRole, int16_t isCritical) {
  int16_t baseDamage, i;
  int16_t atkPower, defPower, k, j = 0;
  double rng;

  if (playerRoles(attackerRole, 4)) {
    player_attack_execute(targetRole, attackerRole);
    return;
  }
  baseDamage = (player_status[targetRole][8] > 0) ? 1 : 0;
  for (i = 0; i <= baseDamage; i++) {
    atkPower = get_player_attribute_total(attackerRole, 17);
    defPower = calc_level_bonus(isCritical, 4);
    k = (calc_battle_damage(atkPower, enemy_runtime_data[isCritical].defense + defPower) * 2) /
        enemy_runtime_data[isCritical].physicalResistance;
    role_physical_attack(targetRole, isCritical, &k, baseDamage);
    j += k;
  }
  reset_battle_sprite_pos(targetRole);
  draw_battle_scene(1, 0);
  enemy_battle_data[isCritical].hp -= j;
  if (enemy_battle_data[isCritical].hp > 0) {
    enemy_battle_data[isCritical].x = enemy_battle_data[isCritical].origX;
    enemy_battle_data[isCritical].y = enemy_battle_data[isCritical].origY;
  } else {
    battle_enemy_hp = enemy_runtime_data[isCritical].deathSound;
  }
  rng = VB_rtcRandomNext();
  playerExp.health[attackerRole].count =
      vb_round_banker_i16_d((double)playerExp.health[attackerRole].count + rng * 2.0);
  playerExp.attack[attackerRole].count++;
}

int16_t select_battle_action(int16_t menuType) {
  int16_t result, tmp, tmp2, tmp3, tmp4, tmp5;
  int16_t actionResult = 1;
  int16_t cursorStart;

  for (;;) {
    if (menuType == 0)
      cursorStart = 1;
    else
      cursorStart = 4;
    result = select_item_with_filter(&magic_select_tmp, -1, cursorStart);
    draw_battle_status_bar();
    if (result < 0) {
      actionResult = -2;
    L_0040E088:
      return actionResult;
    }
    tmp = 0;
    tmp5 = menuType;
    switch (tmp5) {
    case 0:
      PAL_GetBin((uint16_t *)&tmp2, objects[result].data[6], 4);
      if (tmp2 == 0) {
        tmp3 = select_battle_target();
        if (tmp3 < 0) {
          tmp = -2;
        } else {
          battle_role_action[battle_curr_role_idx].actionType = 3;
          battle_role_action[battle_curr_role_idx].target = tmp3;
        }
      } else {
        battle_role_action[battle_curr_role_idx].actionType = 3;
        battle_role_action[battle_curr_role_idx].target = -1;
      }
      break;
    case 1:
      PAL_GetBin((uint16_t *)&tmp2, objects[result].data[6], 4);
      if (tmp2 == 0) {
        tmp4 = select_enemy_target();
        if (tmp4 < 0) {
          tmp = -2;
        } else {
          battle_role_action[battle_curr_role_idx].actionType = 4;
          battle_role_action[battle_curr_role_idx].target = tmp4;
        }
      } else {
        battle_role_action[battle_curr_role_idx].actionType = 4;
        battle_role_action[battle_curr_role_idx].target = battle_curr_role_idx;
      }
      break;
    }
    if (tmp != -2) {
      battle_role_action[battle_curr_role_idx].itemID = result;
      battle_role_action[battle_curr_role_idx].invIndex = magic_select_tmp;
      inventory[magic_select_tmp].amountInUse++;
      goto L_0040E088;
    }
  }
}


void increase_player_attr(int16_t roleID, int16_t attrType, int16_t amount) {
  int16_t attrName;
  if (attrType <= 2) {
    playerRoles(roleID, 8 + attrType) += amount;
    attrName = attrType + 6;
  } else {
    attrName = attrType + 14;
  }
  playerRoles(roleID, attrName) += amount;
  if (playerRoles(roleID, attrName) > 999)
    playerRoles(roleID, attrName) = 999;
  frame_menu(52, 60, 11, -1);
  char message[100];
  char name[sizeof(word_dat_data[0]) + 1];
  char attr[sizeof(word_dat_data[0]) + 1];
  char suffix[sizeof(word_dat_data[0]) + 1];
  memcpy(name, word_dat_data[playerRoles(roleID, 3)], sizeof(word_dat_data[0]));
  memcpy(attr, word_dat_data[48 + attrType], sizeof(word_dat_data[0]));
  memcpy(suffix, word_dat_data[32], sizeof(word_dat_data[0]));
  name[sizeof(word_dat_data[0])] = 0;
  attr[sizeof(word_dat_data[0])] = 0;
  suffix[sizeof(word_dat_data[0])] = 0;
  trim_string(name);
  trim_string(attr);
  trim_string(suffix);
  snprintf(message, sizeof(message), "%s%s%s", name, attr, suffix);
  PAL_DrawString((const char *)message, 62, 70, 3, 0, (void *)screen_buffer_ptr);
  display_number(218, 74, amount, 0);
  wait_frame(180);
}

void flee_from_battle(int16_t roleIdx, int16_t *isFleeing) {
  int16_t roleID = party[roleIdx].role;
  int16_t i, j, fleeChance;
  int32_t k, l;

  if (*isFleeing == 0) {
    i = roleIdx;
    j = roleIdx;
    fleeChance = 3;
  } else {
    play_sound(45, 1);
    i = 0;
    j = enemy_pos_count;
    fleeChance = 9;
  }

  for (k = 0; k <= fleeChance; k++) {
    draw_battle_scene(1, 0);
    for (l = i; l <= j; l++) {
      if (playerRoles(party[l].role, 9) > 0) {
        player_battle_sprite[l].direction = 0;
        player_battle_sprite[l].x += 6 + l + l;
        player_battle_sprite[l].y += 4 - l;
      }
    }
  }

  if (*isFleeing == 0) {
    player_battle_sprite[roleIdx].direction = 1;
    draw_battle_scene(1, 0);
    draw_text_at(128, 90, 31, 14);
    PAL_Delay(22);
    player_battle_sprite[roleIdx].direction = player_battle_sprite[roleIdx].origDirection;
    draw_battle_scene(1, 0);
    player_battle_sprite[roleIdx].x = player_battle_sprite[roleIdx].origX;
    player_battle_sprite[roleIdx].y = player_battle_sprite[roleIdx].origY;
    playerExp.flee[roleID].count += 2;
  } else {
    battle_select_max = -1;
  }
  draw_battle_scene(1, -4);
  if (*isFleeing)
    *isFleeing = 2;
  battle_action_param = *isFleeing;
}

int16_t check_save_file(void) {
  int16_t i;
  int16_t highlight, selectedSlot, cursorSlot;
  int16_t fileResult, menuY;
  char filename[16];
  int32_t handle;

  for (i = 1; i <= 5; i++) {
    snprintf(filename, sizeof(filename), "%d.RPG", i);
    handle = open_file(filename, 0);
    if (handle > 0) {
      PAL_ReadFile(handle, &battle_order_array[i - 1], 2);
      if (battle_order_array[i - 1] > max_save_number)
        max_save_number = battle_order_array[i - 1];
    } else {
      battle_order_array[i - 1] = 0;
    }
    PAL_CloseFile(handle);
  }

  highlight = -1;
  selectedSlot = -2;
  cursorSlot = 0;
  while (selectedSlot == -2) {
    if (cursorSlot < 0)
      cursorSlot = 0;
    if (cursorSlot > 4)
      cursorSlot = 4;
    menuY = 4;
    for (i = 0; i <= 4; i++) {
      frame_menu(180, menuY, 6, highlight);
      if (i == cursorSlot)
        draw_text_at(197, menuY + 9, 43 + i, 250);
      else
        draw_string(198, menuY + 10, 43 + i, 3, 0);
      display_number(270, menuY + 15, battle_order_array[i], 0);
      menuY += 38;
    }
    highlight = 0;
    fileResult = read_key();
    if (fileResult == 1)
      selectedSlot = -1;
    if (fileResult == 3)
      cursorSlot--;
    if (fileResult == 4)
      cursorSlot++;
    if (fileResult == 2)
      selectedSlot = cursorSlot;
  }
  return selectedSlot;
}

void play_all_kinds_music(int16_t musicNum, int16_t loopFlag) {
  if (music_mode == 0)
    return;
  midi_close();
  cd_stop();
  if (use_cd_flag && musicNum > 0 && musicNum != 29) {
    pal_playMidi(musicNum, loopFlag);
    midi_active = 1;
    midi_playing = loopFlag & 1;
    music_mode_arg = midi_playing;
    music_track_arg = musicNum;
  }
}

void read_direction_input(int16_t *dx, int16_t *dy) {
  int16_t dirX1 = 0, dirY1 = 0, dirX2 = 0, dirY2 = 0;
  int16_t dirChanged = 0;
  int16_t dirX = 0;
  // NOTE: 偏离 golden：多方向键同按（左+右、上+左等）时取「仍按住键中
  // 最后按下者」持续行走；golden 平手/双组并存 = 停走（原版同样停）。
  static uint16_t press_seq[8]; /* 各槽位最近按下的序号 */
  static uint16_t press_ctr;
  int16_t wdx = 0, wdy = 0; /* 按住键中最后按下者的方向 */
  int best = -1;
  int16_t tmp, keyResult;

  key_pressed = check_key_pressed();
  if (dx == NULL || dy == NULL)
    return;

  for (tmp = 0; tmp <= 7; tmp++) {
    int16_t st = (int16_t)key_state_array[key_scan_codes[tmp]];
    if (st == 2)
      press_seq[tmp] = ++press_ctr;
    if (st >= 2 && (int)press_seq[tmp] > best) {
      best = (int)press_seq[tmp];
      wdx = 0;
      wdy = 0;
      if (tmp == 2 || tmp == 6)
        wdx = -1;
      else if (tmp == 3 || tmp == 7)
        wdx = 1;
      else if (tmp == 0 || tmp == 4)
        wdy = -1;
      else
        wdy = 1;
    }
  }

  for (tmp = 2; tmp <= 3; tmp++) {
    keyResult = 4 - tmp;
    if ((key_state_array[key_scan_codes[0]] == tmp) || (key_state_array[key_scan_codes[4]] == tmp))
      dirX1 = keyResult;
    if ((key_state_array[key_scan_codes[1]] == tmp) || (key_state_array[key_scan_codes[5]] == tmp))
      dirY1 = keyResult;
    if ((key_state_array[key_scan_codes[2]] == tmp) || (key_state_array[key_scan_codes[6]] == tmp))
      dirX2 = keyResult;
    if ((key_state_array[key_scan_codes[3]] == tmp) || (key_state_array[key_scan_codes[7]] == tmp))
      dirY2 = keyResult;
  }

  if ((dirX1 + dirY1) == 0) {
    dirChanged = 0;
    if (dirX2 > dirY2)
      dirX = -1;
    if (dirX2 < dirY2)
      dirX = 1;
    if (dirX2 != 0 && dirX2 == dirY2)
      dirX = wdx;
  }
  if ((dirX2 + dirY2) == 0) {
    dirX = 0;
    if (dirX1 > dirY1)
      dirChanged = -1;
    if (dirX1 < dirY1)
      dirChanged = 1;
    if (dirX1 != 0 && dirX1 == dirY1)
      dirChanged = wdy;
  }
  if ((dirX1 + dirY1) != 0 && (dirX2 + dirY2) != 0) { /* 跨轴同按：最后按下者 */
    dirX = wdx;
    dirChanged = wdy;
  }
  if (dirX1 == 2) {
    dirChanged = -1;
    dirX = 0;
  }
  if (dirY1 == 2) {
    dirChanged = 1;
    dirX = 0;
  }
  if (dirX2 == 2) {
    dirX = -1;
    dirChanged = 0;
  }
  if (dirY2 == 2) {
    dirX = 1;
    dirChanged = 0;
  }
  if ((dirY2 == 0) && (dirX == 1))
    dirX = 0;
  if ((dirX2 == 0) && (dirX == -1))
    dirX = 0;
  if ((dirY1 == 0) && (dirChanged == 1))
    dirChanged = 0;
  if ((dirX1 == 0) && (dirChanged == -1))
    dirChanged = 0;
  *dx = dirX;
  *dy = dirChanged;
}

void play_opening_anim(void) {
  int16_t k = 0, x = 220, i = 200, color = 0;
  int16_t y = -400;
  int32_t j;
  intptr_t bufPtr;
  intptr_t offset;

  load_fbp_two_scene(61);
  load_subfile_to_buf(571);
  PAL_Unpak(global_buf_2, (uint8_t *)fire_mkf_data);
  load_subfile_to_buf(572);
  PAL_Unpak(global_buf_2, (uint8_t *)mgo_frame_offsets);
  for (j = 0; j <= 399; j++) {
    i -= (j & 1);
    if (i >= 0) {
      bufPtr = PAL_ArrayPtr(global_buf_1);
      offset = bufPtr + (i * 320);
    }
    PAL_ClearClipNA(0, 0, viewport_row_limit, viewport_row_stride, viewport_row_limit, (void *)offset, global_buf_2);
    PAL_RripA(viewport_row_limit, 2, global_buf_2);
    PAL_PutIpNA(0, (int16_t)y, 0, (int16_t)viewport_row_limit, &fire_mkf_data[fire_mkf_data[0]],
                (intptr_t)global_buf_2);
    PAL_PutIpNA(0, (int16_t)(y + 200), 0, (int16_t)viewport_row_limit, &fire_mkf_data[fire_mkf_data[1] & 32767],
                (intptr_t)global_buf_2);
    if (y < 10)
      y++;
    k = (k + 1) & 3;
    x -= (k & 1);
    if (x < 80)
      x = 80;
    PAL_PutIpNA(230, (int16_t)x, 0, (int16_t)viewport_row_limit,
                (const uint8_t *)mgo_frame_offsets + 2 * mgo_frame_offsets[k], (intptr_t)global_buf_2);
    if (color <= 64) {
      color++;
      PAL_ExPalate((uint8_t *)&palette_data[768], (const uint8_t *)palette_data, 720, color);
      PAL_IntPalate((uint8_t *)&palette_data[768]);
    }
    PAL_NipWA(0, viewport_row_limit, global_buf_2);
    PAL_WaitTime(5);
  }
  palette_fade_active = 0;
}

// NOTE: golden 由 GetActiveWindow 焦点比较驱动后台分支（失焦 = 关红点收尾）；
// remake 恒前台（见 pal_ext.c PAL_GetActiveWindow），窗口销毁由 SIGHUP 泵内承接。
void timer_midi_cd_callback(void) {
  if (PAL_GetActiveWindow() != 0) {
    if (exit_flag == 1)
      stop_app_and_music();
    if (cd_active == 1 && cd_track_valid != 0) {
      if (query_cd_status() == 0) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "play cdtrack from%u to%u", cd_track_num, cd_track_num + 1);
        pal_mciSendStringA(cmd, NULL, 0);
      }
    }
    if (midi_active == 1 && midi_playing == 1) {
      if (query_midi_status() == 0) {
        pal_mciSendStringA("seek midi to start", NULL, 0);
        pal_mciSendStringA("play midi", NULL, 0);
      }
    }
    return;
  } else if (exit_flag == 0) {
    stop_all_and_exit();
    return;
  } else if (exit_flag == 1) {
    // NOTE: remake 增补(51ca403 兜底),golden 无此分支——p-code 0040F522
    // 处仅 exit_flag=0→stop_all_and_exit,窗口失活且 exit_flag=1 时什么都不做。
    // VB 由宿主 End 语句承担终止;remake 需显式清理。仅在 stop_all_and_exit
    // 已置 exit_flag=1 的关机阶段且窗口失活时触发,正常游玩不可达。
    release_resources_exit();
  }
}

void draw_battle_scene(int16_t frameCount, int16_t speed) {
  int16_t i;
  double rng = VB_rtcRandomNext();
  battle_dest_x = vb_round_banker_i16_d((double)battle_dest_x + (double)blow_away_flag * rng);

  for (i = 1; i <= frameCount; i++) {
    int32_t j;
    int16_t partyX = theurgy_effect_count + theurgy_effect_max;
    intptr_t bufPtr = PAL_ArrayPtr(global_buf_1);

    PAL_ClearClipNA(0, battle_y_offset, dialog_y_pos, viewport_row_stride, dialog_y_pos, (void *)bufPtr, global_buf_2);
    PAL_ExMyll(dialog_y_pos);
    if (partyX > 0)
      PAL_RripA(dialog_y_pos, (uint8_t)partyX, global_buf_2);
    update_enemy_battle_anim();
    if (npc_dir_frame)
      draw_npc_sprite();
    else
      draw_role_battle_frame();
    draw_effect_sprites(0);
    update_damage_numbers();
    PAL_NTree(0, global_buf_2);
    PAL_NipWA(frame_counter, dialog_y_pos, global_buf_2);

    effect_sub_count++;
    for (j = 0; j <= team_number; j++) {
      if (player_hit_flags[j]) {
        int16_t spriteIdx = map_data_buf[enemy_battle_data[j].mapValue + enemy_battle_data[j].direction] +
                            enemy_battle_data[j].mapValue;
        int16_t x = enemy_battle_data[j].x - (int16_t)(map_data_buf[spriteIdx] / 2) + battle_dest_x;
        int16_t y = enemy_battle_data[j].y - (int16_t)map_data_buf[spriteIdx + 1] + (battle_dest_x / 2);
        PAL_PutP(x, y, (const uint8_t *)(map_data_buf + spriteIdx), (void *)screen_buffer_ptr, 6, 1);
      }
    }
    for (j = 0; j <= enemy_pos_count; j++) {
      if (enemy_battle_data_ext[j])
        get_sprite_frame_data(j, 6);
    }
    PAL_WaitTime(5 + speed);
    update_shake();
  }
  PAL_ClearMenu((uint8_t *)player_hit_flags, 5);
  PAL_ClearMenu((uint8_t *)enemy_battle_data_ext, 3);
}

int16_t produce_screen_map(int16_t x, int16_t y, int16_t viewportIdx) {
  int16_t viewportX = x, viewportY = y;
  int16_t i, j, baseIdx, dataIdx, dataIdx2;
  int16_t npcIdx = -1;
  int16_t mapX, mapY, tileData;

  battle_sprite_data[0] = viewportX;
  battle_sprite_data_ext[0] = viewportY;
  PAL_ExRij((uint16_t *)&save_temp_buf[0], &battle_sprite_data[0], &battle_sprite_data_ext[0]);
  for (i = 0; i <= 4; i++) {
    baseIdx = (i * 3) + 1;
    dataIdx = baseIdx + 1;
    dataIdx2 = baseIdx + 2;
    battle_sprite_data[dataIdx] = viewportX;
    battle_sprite_data_ext[dataIdx] = viewportY + (key_repeat_delay[viewportIdx] * 2);
    PAL_ExRij((uint16_t *)&save_temp_buf[dataIdx], &battle_sprite_data[dataIdx], &battle_sprite_data_ext[dataIdx]);
    battle_sprite_data[dataIdx2] = viewportX + (fh_M_MSG_global[viewportIdx] * 2);
    battle_sprite_data_ext[dataIdx2] = viewportY;
    PAL_ExRij((uint16_t *)&save_temp_buf[dataIdx2], &battle_sprite_data[dataIdx2], &battle_sprite_data_ext[dataIdx2]);
    viewportX += fh_M_MSG_global[viewportIdx];
    viewportY += key_repeat_delay[viewportIdx];
    battle_sprite_data[baseIdx] = viewportX;
    battle_sprite_data_ext[baseIdx] = viewportY;
    PAL_ExRij((uint16_t *)&save_temp_buf[baseIdx], &battle_sprite_data[baseIdx], &battle_sprite_data_ext[baseIdx]);
  }
  for (j = 0; j <= 12; j++) {
    for (baseIdx = 1; baseIdx <= curr_scene_event_count; baseIdx++) {
      if (npc_display_data[baseIdx].state > 0) {
        if (((npc_display_data[baseIdx].triggerMode * 6) - 4) > j && npc_display_data[baseIdx].triggerMode <= 3) {
          mapX = npc_display_data[baseIdx].x;
          mapY = npc_display_data[baseIdx].y;
          PAL_ExRij((uint16_t *)&tileData, (uint16_t *)&mapX, (uint16_t *)&mapY);
          if (tileData == save_temp_buf[j] && mapX == battle_sprite_data[j] && mapY == battle_sprite_data_ext[j]) {
            npcIdx = baseIdx;
            goto L_0040FBF8;
          }
        }
      }
    }
  }
L_0040FBF8:
  return npcIdx;
}

void render_dialog_control(int16_t x, int16_t y) {
  int32_t j, k;
  uint8_t dialogBuf[3];
  int32_t i_16;
  int16_t tmp, tmp2;

  image_draw_x = x;
  image_draw_y = y;
  image_lookup_key = 0;
  for (j = 1; j <= image_offset_table[0]; j++) {
    k = image_offset_table[j];
    switch (k) {
    case 34:
      swap_values(&dialog_x, &dialog_y);
      continue;
    case 36:
      music_mode_init = image_offset_table[j + 1] - 48;
      music_mode_init = music_mode_init * 10;
      music_mode_init = music_mode_init + (image_offset_table[j + 2] - 48);
      music_mode_init = vb_round_banker_u16((float)music_mode_init * 10.0f / 7.0f);
      j += 2;
      continue;
    case 126:
      tmp = image_offset_table[j + 1] - 48;
      tmp = tmp * 10;
      tmp = tmp + (image_offset_table[j + 2] - 48);
      tmp = (int16_t)vb_round_banker_i16((float)tmp * 10.0f / 7.0f);
      flag_trigger = 0;
      image_draw_flag = 0;
      PAL_Delay(tmp);
      j += 2;
      continue;
    case 41:
      image_lookup_key = 1;
      continue;
    case 40:
      image_lookup_key = 2;
      continue;
    default:
      if (image_offset_table[j] > 128) {
        dialogBuf[0] = image_offset_table[j];
        dialogBuf[1] = image_offset_table[j + 1];
        dialogBuf[2] = 0;
        j += 1;
        i_16 = 16;
      } else {
        dialogBuf[0] = image_offset_table[j];
        dialogBuf[1] = 0;
        i_16 = 8;
      }
      PAL_DrawString((const char *)dialogBuf, (int16_t)image_draw_x, (int16_t)image_draw_y, 0, (uint8_t)dialog_x,
                     (void *)screen_buffer_ptr);
      if (image_draw_flag == 0)
        PAL_WaitTime(music_mode_init);
      image_draw_x += i_16;
      tmp2 = check_key_pressed();
      if (tmp2 == 2)
        image_draw_flag = -1;
      continue;
    }
  }
}

void process_AutoScript(int16_t npcIdx, uint16_t *scriptEntry) {
  int16_t op;
  uint16_t tmp;
  int16_t opcode, operand1, operand2, operand3;
  double rng;

L_0040FF88:
  for (;;) {
    tmp = 0;
    PAL_CvLong(*scriptEntry, &tmp);
    PAL_CopyMem(save_temp_buf, &sss_script_data_2[tmp * 8], 8);
    opcode = save_temp_buf[0];
    operand1 = save_temp_buf[1];
    operand2 = save_temp_buf[2];
    operand3 = save_temp_buf[3];
    op = opcode;

    if (op == 0)
      return;

    if (op == 2) {
      if (operand2 == 0) {
        *scriptEntry = operand1;
      } else {
        npc_display_data[npcIdx].scriptIdleFrameAuto++;
        if (npc_display_data[npcIdx].scriptIdleFrameAuto < operand2) {
          *scriptEntry = operand1;
        } else {
          npc_display_data[npcIdx].scriptIdleFrameAuto = 0;
          goto L_004102CE;
        }
        return;
      }
      return;
    }

    switch (op) {
    case 3:
      if (operand2 == 0) {
        *scriptEntry = operand1;
        goto L_0040FF88;
      } else {
        npc_display_data[npcIdx].scriptIdleFrameAuto++;
        if (npc_display_data[npcIdx].scriptIdleFrameAuto >= operand2) {
          npc_display_data[npcIdx].scriptIdleFrameAuto = 0;
          goto L_004102CE;
        }
        *scriptEntry = operand1;
        goto L_0040FF88;
      }
    case 4:
      if (operand2 <= 0) {
        process_Script(npcIdx, (uint16_t *)&operand1);
        goto L_004102CE;
      } else {
        tmp = operand2 - scenes[RPG_curr_scene].eventObjectIndex;
        if (!((tmp > 0) && (tmp <= curr_scene_event_count))) {
          goto L_004102CE;
        } else {
          process_Script((int16_t)tmp, (uint16_t *)&operand1);
          goto L_004102CE;
        }
      }
    case 6:
      rng = VB_rtcRandomNext();
      if ((float)operand1 >= (float)(rng * 100.0))
        goto L_004102CE;
      if (operand2 == 0)
        return;
      break;
    case 9:
      npc_display_data[npcIdx].scriptIdleFrameAuto++;
      if (npc_display_data[npcIdx].scriptIdleFrameAuto >= operand1) {
        npc_display_data[npcIdx].scriptIdleFrameAuto = 0;
        goto L_004102CE;
      }
      return;
    default:
      if (op > 10)
        process_scripts(npcIdx, scriptEntry, opcode, &operand1, &operand2, &operand3);
    L_004102CE:
      *scriptEntry = increment_script_ip(*scriptEntry);
      return;
    }
    *scriptEntry = operand2;
  }
}

void SaveRPG_internal(int16_t saveSlot) {
  char filename[16];
  int32_t handle;

  snprintf(filename, sizeof(filename), "%d.RPG", saveSlot);
  rpg_to_load = saveSlot;
  handle = open_file(filename, -1);
  if (handle <= 0)
    return;

  max_save_number++;
  save_temp_buf[0] = max_save_number;
  save_temp_buf[1] = RPG_viewport_x;
  save_temp_buf[2] = RPG_viewport_y;
  save_temp_buf[3] = RPG_team_number;
  save_temp_buf[4] = RPG_curr_scene;
  save_temp_buf[5] = RPG_color_begin_ptr;
  save_temp_buf[6] = RPG_team_direction;
  save_temp_buf[7] = RPG_music_number;
  save_temp_buf[8] = RPG_battle_music_number;
  save_temp_buf[9] = RPG_battle_scene_number;
  save_temp_buf[10] = RPG_screen_wave_grade;
  save_temp_buf[11] = 0;
  save_temp_buf[12] = RPG_current_calabash_number;
  save_temp_buf[13] = RPG_role_locate_layer;
  save_temp_buf[14] = RPG_ememy_chase_rate;
  save_temp_buf[15] = RPG_change_chaserate_times;
  save_temp_buf[16] = RPG_other_peoples;
  PAL_WriteFile(handle, save_temp_buf, 40);
  PAL_WriteFile(handle, &RPG_money, 4);
  PAL_WriteFile(handle, &party[0].role, 50);
  PAL_WriteFile(handle, &trail[0].x, 30);
  PAL_WriteFile(handle, &playerExp, 384);
  PAL_WriteFile(handle, &playerRoles, 900);
  uint8_t vbPoison[320];
  int pr, ps;
  for (pr = 0; pr < MAX_PLAYABLE_PLAYER_ROLES; pr++)
    for (ps = 0; ps < MAX_POISONS; ps++)
      memcpy(vbPoison + (pr + 5 * ps) * 4, &poison_status[pr][ps], 4);
  PAL_WriteFile(handle, vbPoison, 320);
  PAL_WriteFile(handle, &inventory[0].item, 1536);
  PAL_WriteFile(handle, &scenes[1].mapNum, 2400);
  PAL_WriteFile(handle, &objects[0].data[0], 8400);
  load_event_objects();
  PAL_WriteFile(handle, events, event_object_count * 32);
  PAL_CloseFile(handle);
}

void LoadRPG_internal(int16_t saveSlot) {
  char filename[16];
  int32_t handle;

  snprintf(filename, sizeof(filename), "%d.RPG", saveSlot);
  rpg_to_load = saveSlot;
  handle = open_file(filename, 0);
  // NOTE: pal_lopen 失败返回 -1；原 ==0 判空漏接负句柄，缺失存档会以零缓存继续装档
  if (handle <= 0)
    return;

  save_temp_buf[0] = 0;
  PAL_ReadFile(handle, save_temp_buf, 40);
  RPG_save_number = save_temp_buf[0];
  if (RPG_save_number > 0) {
    RPG_viewport_x = save_temp_buf[1];
    RPG_viewport_y = save_temp_buf[2];
    RPG_team_number = save_temp_buf[3];
    scene_to_load = save_temp_buf[4];
    RPG_curr_scene = scene_to_load;
    RPG_color_begin_ptr = save_temp_buf[5];
    RPG_team_direction = save_temp_buf[6];
    RPG_music_number = save_temp_buf[7];
    RPG_battle_music_number = save_temp_buf[8];
    RPG_battle_scene_number = save_temp_buf[9];
    RPG_screen_wave_grade = save_temp_buf[10];
    RPG_current_calabash_number = save_temp_buf[12];
    RPG_role_locate_layer = save_temp_buf[13];
    RPG_ememy_chase_rate = save_temp_buf[14];
    RPG_change_chaserate_times = save_temp_buf[15];
    RPG_other_peoples = save_temp_buf[16];
    PAL_ReadFile(handle, &RPG_money, 4);
    PAL_ReadFile(handle, &party[0].role, 50);
    PAL_ReadFile(handle, &trail[0].x, 30);
    PAL_ReadFile(handle, &playerExp, 384);
    PAL_ReadFile(handle, &playerRoles, 900);
    uint8_t vbPoison[320];
    int pr, ps;
    PAL_ReadFile(handle, vbPoison, 320);
    for (pr = 0; pr < MAX_PLAYABLE_PLAYER_ROLES; pr++)
      for (ps = 0; ps < MAX_POISONS; ps++)
        memcpy(&poison_status[pr][ps], vbPoison + (pr + 5 * ps) * 4, 4);
    PAL_ReadFile(handle, &inventory[0].item, 1536);
    PAL_ReadFile(handle, &scenes[1].mapNum, 2400);
    PAL_ReadFile(handle, &objects[0].data[0], 8400);
    PAL_ReadFile(handle, events, event_object_count * 32);
    redraw_flag |= 7;
    fade_in(1);
  }
  PAL_CloseFile(handle);
}

void enemy_attack_enemy(int16_t attackerIdx, int16_t targetIdx) {
  int32_t i;
  int16_t stepSize;
  int32_t damage;
  int16_t result, result2;

  effect_particle_count = 0;
  load_battle_effect_sprites();

  for (i = 1; i <= 3; i++) {
    enemy_battle_data[attackerIdx].x = (enemy_battle_data[attackerIdx].x + enemy_battle_data[targetIdx].x) / 2;
    enemy_battle_data[attackerIdx].y = (enemy_battle_data[attackerIdx].y + enemy_battle_data[targetIdx].y) / 2;
    draw_battle_scene(1, 0);
  }
  effect_particle_count = 1;
  effect_x_coords[1] = (enemy_battle_data[attackerIdx].x + enemy_battle_data[targetIdx].x) / 2;
  effect_y_coords[1] = (enemy_battle_data[targetIdx].y - (enemy_battle_data[targetIdx].tileData / 3)) + 10;
  effect_frames[1] = 99;

  stepSize = 16;
  for (i = 0; i <= 2; i++) {
    enemy_battle_data[targetIdx].x -= stepSize;
    stepSize = -stepSize / 2;
    effect_frame_count = 9 + i;
    draw_battle_scene(1, 0);
  }
  effect_particle_count = 0;

  result = calc_level_bonus(attackerIdx, 6);
  result2 = calc_level_bonus(targetIdx, 4);
  damage = (calc_battle_damage(enemy_runtime_data[attackerIdx].attackStrength + result,
                               enemy_runtime_data[targetIdx].defense + result2) *
            2) /
           enemy_runtime_data[targetIdx].physicalResistance;
  add_damage_number(enemy_battle_data[targetIdx].x, enemy_battle_data[targetIdx].y - 110, damage, 1);
  player_hit_flags[targetIdx] = (uint16_t)-1;
  draw_battle_scene(1, 0);

  enemy_battle_data[attackerIdx].x = enemy_battle_data[attackerIdx].origX;
  enemy_battle_data[attackerIdx].y = enemy_battle_data[attackerIdx].origY;
  draw_battle_scene(2, 0);

  enemy_battle_data[targetIdx].hp -= damage;
  if (enemy_battle_data[targetIdx].hp > 0) {
    enemy_battle_data[targetIdx].x = enemy_battle_data[targetIdx].origX;
    enemy_battle_data[targetIdx].y = enemy_battle_data[targetIdx].origY;
    return;
  } else {
    npc_sprite_num = -1;
    return;
  }
}

void add_sprite_to_tree(int16_t x, int16_t y, int16_t depth, int16_t roleIdx, int16_t frameIdx, int16_t useNPC) {
  int16_t sdepth = depth;
  uint32_t spriteBase;
  int16_t frameW, frameH;
  if (useNPC == 0) {
    spriteBase = word_glyph_index[party[roleIdx].imageOffset + frameIdx] + party[roleIdx].imageOffset;
    frameW = word_glyph_index[spriteBase];
    frameH = word_glyph_index[spriteBase + 1];
    PAL_AddToTree((int16_t)x - frameW / 2, (int16_t)y + depth + 10, depth + 6,
                  spriteBase * 2 + (uintptr_t)word_glyph_index);
  } else {
    spriteBase =
        fire_mkf_data[npc_display_data[roleIdx].spritePtrOffset + frameIdx] + npc_display_data[roleIdx].spritePtrOffset;
    frameW = fire_mkf_data[spriteBase];
    frameH = fire_mkf_data[spriteBase + 1];
    PAL_AddToTree((int16_t)x - frameW / 2, (int16_t)y + depth + 9, depth + 2,
                  spriteBase * 2 + (uintptr_t)fire_mkf_data);
  }

  if (multi_event_state == 0 && sdepth < 72) {
    int16_t thresholdY = y + RPG_viewport_y;
    int16_t mapCol = x + RPG_viewport_x;
    int16_t rowY = thresholdY;
    int16_t half;
    int16_t tileIdx, l;
    int rows = (frameH + 15) / 16;
    int cols = frameW / 64;
    PAL_ExRij((uint16_t *)&half, (uint16_t *)&mapCol, (uint16_t *)&rowY);
    for (l = rowY - rows; l <= rowY; l++) {
      for (tileIdx = mapCol - cols; tileIdx <= mapCol + cols; tileIdx++) {
        PAL_ExBB(thresholdY, half, tileIdx, l, (const uint8_t *)map_data_buf, (uint8_t *)global_buf_2);
        PAL_ExBB(thresholdY, half, tileIdx - 1, l, (const uint8_t *)map_data_buf, (uint8_t *)global_buf_2);
        PAL_ExBB(thresholdY, half, tileIdx + 1, l, (const uint8_t *)map_data_buf, (uint8_t *)global_buf_2);
        if (half == 0) {
          PAL_ExBB(thresholdY, 1, tileIdx, l, (const uint8_t *)map_data_buf, (uint8_t *)global_buf_2);
          PAL_ExBB(thresholdY, 1, tileIdx - 1, l, (const uint8_t *)map_data_buf, (uint8_t *)global_buf_2);
        } else {
          PAL_ExBB(thresholdY, 0, tileIdx + 1, l + 1, (const uint8_t *)map_data_buf, (uint8_t *)global_buf_2);
          PAL_ExBB(thresholdY, 0, tileIdx, l + 1, (const uint8_t *)map_data_buf, (uint8_t *)global_buf_2);
        }
      }
    }
  }
}

void set_team_draw(void) {
  int32_t i, j;
  int16_t roleID;
  int16_t trailX, trailY;
  int16_t trailIdx;

  if (playerRoles(party[0].role, 64) == 4) {
    party[0].frame = (RPG_team_direction * 4) + scene_flags;
  } else {
    party[0].frame = (RPG_team_direction * 3) + viewport_flags;
  }
  party[0].x = team_abstract_x;
  party[0].y = team_abstract_y;
  trail[0].direction = RPG_team_direction;
  trail[0].x = x_off;
  trail[0].y = y_off;

  for (i = 1; i <= RPG_team_number; i++) {
    roleID = party[i].role;
    trailIdx = 1;
    trailX = (int16_t)(trail[trailIdx].x - RPG_viewport_x);
    trailY = (int16_t)(trail[trailIdx].y - RPG_viewport_y);
    if (i == 2) {
      trailY += 8;
      if ((trail[trailIdx].direction & 1) == 0) {
        trailX += 16;
      } else {
        trailX -= 16;
      }
    } else {
      trailX -= fh_M_MSG_global[trail[trailIdx].direction];
      trailY -= (int16_t)key_repeat_delay[trail[trailIdx].direction];
    }
    if (!load_map_data(RPG_viewport_x + trailX, RPG_viewport_y + trailY, 0)) {
      trailX = (int16_t)(trail[trailIdx].x - RPG_viewport_x);
      trailY = (int16_t)(trail[trailIdx].y - RPG_viewport_y);
    }
    party[i].x = trailX;
    party[i].y = trailY;
    if (playerRoles(roleID, 64) == 4) {
      party[i].frame = (trail[2].direction * 4) + scene_flags;
    } else {
      party[i].frame = (trail[2].direction * 3) + viewport_flags_2;
    }
  }

  for (j = 1; j <= RPG_other_peoples; j++) {
    i = RPG_team_number + j;
    int16_t frameOffset = 2 + j;
    party[i].x = trail[frameOffset].x - RPG_viewport_x;
    party[i].y = trail[frameOffset].y - RPG_viewport_y;
    party[i].frame = (trail[frameOffset].direction * 3) + viewport_flags_2;
  }
}

void calc_display_exp(int16_t exp) {
  int32_t i, j, k;
  int32_t l;
  double rng;
  experience_t *expSlots[8] = {playerExp.primary,    playerExp.health,  playerExp.magic,     playerExp.attack,
                               playerExp.magicPower, playerExp.defense, playerExp.dexterity, playerExp.flee};

  frame_menu(76, 60, 8, -1);
  draw_string(90, 70, 30, 3, 0);
  display_number(200, 75, exp, 0);
  wait_frame(310);

  for (i = 0; i <= enemy_pos_count; i++) {
    k = party[i].role;
    if (playerRoles(k, 9) > 0) {
      expSlots[0][k].level = playerRoles(k, 6);
      l = 0;
      for (j = 1; j <= 7; j++)
        l += expSlots[j][k].count;
      if (l <= 0)
        l = 1;

      for (j = 0; j <= 7; j++) {
        if (j > 0) {
          expSlots[j][k].exp = (float)vb_round_banker_u32_d(
              (double)expSlots[j][k].exp + (((2.0 * (double)exp) / (double)l) * (double)expSlots[j][k].count));
        } else {
          expSlots[0][k].exp += exp;
        }
        PAL_CopyMem(battle_order_array, data_levelup_exp, 200);
        while (expSlots[j][k].exp >= battle_order_array[expSlots[j][k].level]) {
          expSlots[j][k].exp -= battle_order_array[expSlots[j][k].level];
          expSlots[j][k].level++;
          if (expSlots[j][k].level > 99) {
            expSlots[j][k].level = 99;
            continue;
          } else if (j == 0) {
            show_equip_detail(k, 1);
            continue;
          } else {
            rng = VB_rtcRandomNext();
            increase_player_attr(k, j, vb_round_banker_i16_d(1.0 + rng));
          }
        }
      }
    }
  }
}

void show_status_icons(void) {
  int32_t i, j, k;
  int32_t iconX, statusIcon, roleID;
  poison_status_t l;

  PAL_CopyMem(global_buf_2, menu_bg_data, menu_bg_size);
  if (flag_battling)
    iconX = 90;
  else
    iconX = 42;

  for (i = 0; i <= RPG_team_number; i++) {
    player_battle_sprite[i].objectID = 0;
    for (j = 0; j <= 14; j++) {
      for (k = j + 1; k <= 15; k++) {
        if (objects[poison_status[i][j].poisonID].data[0] < objects[poison_status[i][k].poisonID].data[0]) {
          l = poison_status[i][j];
          poison_status[i][j] = poison_status[i][k];
          poison_status[i][k] = l;
        }
      }
    }
    for (j = 15; (int16_t)j >= 0; j--) {
      if (poison_status[i][j].poisonID > 0) {
        statusIcon = objects[poison_status[i][j].poisonID].data[1];
        if (statusIcon > 0)
          player_battle_sprite[i].objectID = statusIcon;
      }
    }
    roleID = party[i].role;
    if (playerRoles(roleID, 9) <= 0)
      player_battle_sprite[i].objectID = 2;

    PAL_PutP(iconX, 165, (const uint8_t *)(global_buf_2 + global_buf_2[18]), (void *)screen_buffer_ptr, 0, 0);
    draw_player_status(iconX, 160, party[i].role, player_battle_sprite[i].objectID);

    if (player_status[i][3] > 0)
      draw_text_at(iconX + 50, 160, 26, 44);
    if (player_status[i][2] > 0)
      draw_text_at(iconX + 24, 186, 28, 238);
    if (player_status[i][1] > 0)
      draw_text_at(iconX + 42, 178, 27, 15);
    if (player_status[i][0] > 0)
      draw_text_at(iconX + 60, 182, 29, 94);
    iconX += 78;
  }
}

void draw_enemy_battle_frame(void) {
  int32_t i, j, k, spriteIdx;
  int32_t drawX, drawY;
  intptr_t bufPtr = PAL_ArrayPtr(global_buf_1);
  intptr_t tmp = bufPtr;

  PAL_ExMyll(dialog_y_pos);
  for (i = team_number; (int16_t)i >= 0; i--) {
    if (enemy_battle_data[i].hp > 0) {
      spriteIdx =
          map_data_buf[enemy_battle_data[i].mapValue + enemy_battle_data[i].direction] + enemy_battle_data[i].mapValue;
      drawX = enemy_battle_data[i].x - (map_data_buf[spriteIdx] / 2);
      drawY = enemy_battle_data[i].y - map_data_buf[spriteIdx + 1];
      if (player_hit_flags[i])
        PAL_PutP(drawX, drawY, (const uint8_t *)(map_data_buf + spriteIdx), (void *)tmp, 6, 1);
      else
        PAL_PutP(drawX, drawY, (const uint8_t *)(map_data_buf + spriteIdx), (void *)tmp, 0, 0);
    }
  }
  if (npc_dir_frame) {
    spriteIdx = mgo_frame_offsets[curr_npc_idx];
    drawX = npc_curr_frame - (mgo_frame_offsets[spriteIdx] / 2);
    drawY = npc_direction - mgo_frame_offsets[spriteIdx + 1];
    PAL_PutP(drawX, drawY, (const uint8_t *)(mgo_frame_offsets + spriteIdx), (void *)tmp, 0, 0);
    return;
  } else if (battle_select_max == 0) {
    for (j = 0; j <= enemy_pos_count; j++) {
      save_temp_buf[j + 10] = player_battle_sprite[j].y;
      save_temp_buf[j] = j;
    }
    for (j = 0; j < enemy_pos_count; j++) {
      for (k = j + 1; k <= enemy_pos_count; k++) {
        if (save_temp_buf[j + 10] > save_temp_buf[k + 10]) {
          swap_values((int16_t *)&save_temp_buf[j], (int16_t *)&save_temp_buf[k]);
          swap_values((int16_t *)&save_temp_buf[j + 10], (int16_t *)&save_temp_buf[k + 10]);
        }
      }
    }
    for (j = 0; j <= enemy_pos_count; j++) {
      i = save_temp_buf[j];
      spriteIdx = mgo_frame_offsets[player_battle_sprite[i].spriteBase + player_battle_sprite[i].direction] +
                  player_battle_sprite[i].spriteBase;
      drawX = player_battle_sprite[i].x - (mgo_frame_offsets[spriteIdx] / 2);
      drawY = player_battle_sprite[i].y - mgo_frame_offsets[spriteIdx + 1];
      PAL_PutP(drawX, drawY, (const uint8_t *)(mgo_frame_offsets + spriteIdx), (void *)tmp, 0, 0);
    }
    return;
  }
}

void buy_item_menu(int16_t storeID) {
  int16_t i, itemCount = -1;
  int32_t itemID, cursorPos, selection;
  int16_t menuX, menuY, textColor, keyResult, wordIdx, invIdx, answer;

  for (i = 0; i <= 8; i++) {
    itemID = data_store[storeID][i];
    if (itemID > 0) {
      itemCount++;
      battle_order_array[itemCount] = itemID;
      battle_order_array[itemCount + 10] = objects[itemID].data[1];
    }
  }

  draw_menu_with_text_and_hp(21, 20, 141, RPG_money, (uint16_t)-1);
  draw_menu_with_text_and_hp(35, 20, 100, 0, (uint16_t)-1);
  PAL_PutP(46, 13, (const uint8_t *)(global_buf_2 + global_buf_2[70]), (void *)screen_buffer_ptr, 0, 3);
  PAL_PutP(40, 8, (const uint8_t *)(global_buf_2 + global_buf_2[70]), (void *)screen_buffer_ptr, 0, 0);
  menuX = 122;
  draw_menu_table(menuX, 8, 9, 9, 9, -1);
  cursorPos = 0;
  selection = (uint16_t)-1;

  while (cursorPos != (uint16_t)-1) {
    menuY = 21;
    for (i = 0; i <= itemCount; i++) {
      textColor = (i == (int16_t)cursorPos) ? 47 : 78;
      draw_text_at(menuX + 28, menuY, battle_order_array[i], textColor);
      display_number(menuX + 140, menuY + 5, battle_order_array[i + 10], 0);
      menuY += 18;
    }
    draw_menu_with_text_and_hp(21, 20, 141, RPG_money, 0);
    wordIdx = count_equipped_items(battle_order_array[cursorPos]);
    invIdx = find_inventory_item(battle_order_array[cursorPos]);
    if (invIdx >= 0)
      wordIdx += inventory[invIdx].amount;
    draw_menu_with_text_and_hp(35, 20, 100, wordIdx, 0);
    if (cursorPos != selection)
      draw_object_icon(40, 8, battle_order_array[cursorPos]);
    selection = cursorPos;
    keyResult = read_key();
    if (keyResult == 2) {
      if (RPG_money >= battle_order_array[cursorPos + 10]) {
        push_screen_buffer();
        answer = yes_no_menu(0, 19);
        if (answer == 1) {
          RPG_money -= battle_order_array[cursorPos + 10];
          add_inventory_item(battle_order_array[cursorPos], 1);
        }
        restore_screen();
      }
    }
    if (keyResult == 3) {
      if (cursorPos > 0)
        cursorPos--;
    }
    if (keyResult == 4) {
      if (cursorPos < (uint16_t)itemCount)
        cursorPos++;
    }
    if (keyResult == 1)
      cursorPos = (uint16_t)-1;
  }
}

void level_up_player(int16_t roleID, int16_t levelCount, int16_t restoreHPMP) {
  int32_t i, j;
  double rng;
  for (i = 1; i <= levelCount; i++) {
    playerRoles(roleID, 6)++;
    playerExp.primary[roleID].level = playerRoles(roleID, 6);
    if (playerRoles(roleID, 6) > 99)
      playerRoles(roleID, 6) = 99;

    rng = VB_rtcRandomNext();
    playerRoles(roleID, 7) = (uint16_t)(VB_CSng(playerRoles(roleID, 7) + 10) + VB_Int(rng * 8.0));
    if (playerRoles(roleID, 7) > 999)
      playerRoles(roleID, 7) = 999;

    rng = VB_rtcRandomNext();
    playerRoles(roleID, 8) = (uint16_t)(VB_CSng(playerRoles(roleID, 8) + 8) + VB_Int(rng * 6.0));
    if (playerRoles(roleID, 8) > 999)
      playerRoles(roleID, 8) = 999;

    rng = VB_rtcRandomNext();
    playerRoles(roleID, 17) = vb_round_banker_i16_d(VB_CSng(playerRoles(roleID, 17) + 4) + rng);
    rng = VB_rtcRandomNext();
    playerRoles(roleID, 18) = vb_round_banker_i16_d(VB_CSng(playerRoles(roleID, 18) + 4) + rng);
    rng = VB_rtcRandomNext();
    playerRoles(roleID, 19) = vb_round_banker_i16_d(VB_CSng(playerRoles(roleID, 19) + 2) + rng);
    rng = VB_rtcRandomNext();
    playerRoles(roleID, 20) = vb_round_banker_i16_d(VB_CSng(playerRoles(roleID, 20) + 2) + rng);
    playerRoles(roleID, 21) += 2;

    for (j = 17; j <= 21; j++)
      if (playerRoles(roleID, j) > 999)
        playerRoles(roleID, j) = 999;
  }
  if (restoreHPMP) {
    playerRoles(roleID, 9) = playerRoles(roleID, 7);
    playerRoles(roleID, 10) = playerRoles(roleID, 8);
  }
}

void player_attack_player(int16_t attackerIdx, int16_t targetIdx) {
  int32_t targetRoleID = party[targetIdx].role;
  int32_t result, result2, result3;
  int16_t k;
  int32_t i;

  result = get_player_attribute_total(targetRoleID, 19);
  if (player_battle_sprite[targetIdx].direction == 3)
    result = result + result;

  effect_particle_count = 0;
  load_battle_effect_sprites();

  player_battle_sprite[attackerIdx].direction = 7;
  draw_battle_scene(3, 0);
  player_battle_sprite[attackerIdx].direction = 0;
  draw_battle_scene(2, 0);
  player_battle_sprite[attackerIdx].direction = 7;
  draw_battle_scene(1, 0);
  player_battle_sprite[attackerIdx].direction = 8;
  player_battle_sprite[attackerIdx].x = player_battle_sprite[targetIdx].x + 32;
  player_battle_sprite[attackerIdx].y = player_battle_sprite[targetIdx].y + 10;
  draw_battle_scene(2, 0);
  player_battle_sprite[attackerIdx].x -= 4;
  player_battle_sprite[attackerIdx].y += 2;
  draw_battle_scene(2, 0);
  player_battle_sprite[attackerIdx].direction = 9;

  effect_particle_count = 1;
  effect_frame_count = party[attackerIdx].role * 3;
  effect_x_coords[1] = player_battle_sprite[targetIdx].x;
  effect_y_coords[1] = player_battle_sprite[targetIdx].y - 10;
  effect_frames[1] = 99;

  k = 21;
  for (i = 0; i <= 2; i++) {
    if (i > 0) {
      enemy_battle_data_ext[targetIdx] = (uint16_t)-1;
      if (player_battle_sprite[targetIdx].direction == 0)
        player_battle_sprite[targetIdx].direction = 4;
    }
    player_battle_sprite[targetIdx].x -= k;
    k = k / 2;
    player_battle_sprite[targetIdx].y -= k;
    k = k / 2;
    draw_battle_scene(1, 0);
    effect_frame_count++;
  }
  effect_particle_count = 0;

  result3 = get_player_attribute_total(party[attackerIdx].role, 17);
  result2 = calc_battle_damage(result3, result);
  if (result2 > playerRoles(targetRoleID, 9))
    result2 = playerRoles(targetRoleID, 9);
  playerRoles(targetRoleID, 9) -= result2;
  add_damage_number(player_battle_sprite[targetIdx].x, player_battle_sprite[targetIdx].y - 70, result2, 1);
  draw_battle_scene(1, 0);
  reset_battle_sprite_pos(attackerIdx);
  draw_battle_scene(2, 0);
  player_battle_sprite[targetIdx].direction = player_battle_sprite[targetIdx].origDirection;
  draw_battle_scene(1, 0);
}

void show_item_description(int16_t *roleIdx, int16_t labelIdx, int16_t rowTop, int16_t invIdx) {
  int32_t itemID, amount, descIdx;
  int16_t cursorPos;
  int32_t i, rowY, colX, colorNum;

  PAL_CopyMem(global_buf_2, menu_bg_data, menu_bg_size);
  if (*roleIdx < 0 || *roleIdx > (int16_t)RPG_team_number)
    *roleIdx = 0;
  itemID = inventory[invIdx].item;
  screen_buffer_ptr = PAL_ArrayPtr(global_buf_1);
  PAL_PushScreen(global_buf_1);
  draw_menu_table(labelIdx, rowTop, 0, 10, 8, 0);
  PAL_PutP(labelIdx + 14, rowTop + 84, (const uint8_t *)(global_buf_2 + global_buf_2[70]), (void *)screen_buffer_ptr, 0,
           3);
  PAL_PutP(labelIdx + 8, rowTop + 79, (const uint8_t *)(global_buf_2 + global_buf_2[70]), (void *)screen_buffer_ptr, 0,
           0);
  PAL_CopyMem(bg_buf, global_buf_1, 64000);

L_004135B4:
  for (;;) {
    PAL_CopyMem(global_buf_1, bg_buf, 64000);
    show_role_attributes(labelIdx + 84, rowTop + 12, party[*roleIdx].role, -1);
    rowY = rowTop + 12;
    colX = labelIdx + 14;
    for (i = 0; i <= RPG_team_number; i++) {
      colorNum = (*roleIdx == (int16_t)i) ? 250 : 78;
      draw_text_at(colX, rowY, playerRoles(party[i].role, 3), colorNum);
      rowY += 22;
    }
    amount = inventory[invIdx].amount;
    if (amount > 0) {
      read_ball_mkf_index(objects[itemID].data[0]);
      PAL_PutP(labelIdx + 16, rowTop + 86, (const uint8_t *)(global_buf_2 + global_buf_2[0]), (void *)screen_buffer_ptr,
               0, 0);
      draw_text_at(labelIdx + 4, rowTop + 142, itemID, 13);
    }
    if (amount > 1)
      display_number(labelIdx + 62, rowTop + 132, amount, 2);
    PAL_PopScreen(global_buf_1);
    if (amount <= 0) {
      wait_frame(150);
      goto L_00413902;
    }

    cursorPos = read_key();
    switch (cursorPos) {
    case 3:
      *roleIdx -= 1;
      if (*roleIdx < 0)
        *roleIdx = (int16_t)RPG_team_number;
      goto L_004135B4;
    case 4:
      *roleIdx += 1;
      if (*roleIdx > (int16_t)RPG_team_number)
        *roleIdx = 0;
      goto L_004135B4;
    case 2:
      process_Script(*roleIdx, &objects[itemID].data[2]);
      if (redraw_hp_mp_flag) {
        PAL_GetBin((uint16_t *)&descIdx, objects[itemID].data[6], 3);
        if (descIdx)
          remove_inventory_item(itemID, 1);
      }
      break;
    case 1:
      PAL_PopScreen((uint8_t *)(global_buf_1 + 64000));
      *roleIdx = -1;
    L_00413902:
      screen_buffer_ptr = 0;
      return;
    default:
      if (cursorPos > 4)
        goto L_004135B4;
      goto L_00413902;
    }
  }
}

void process_event_objects(int16_t checkTrigger) {
  int32_t i, triggerScript;
  int16_t range, dxDiff, dyDiff, newX, newY;

  if (palette_fade_active != 0)
    return;

  for (i = 1; i <= curr_scene_event_count; i++) {
    if (npc_display_data[i].state != 0) {
      npc_display_data[i].vanishTime -=
          (int16_t)(npc_display_data[i].vanishTime > 0 ? 1 : (npc_display_data[i].vanishTime < 0 ? -1 : 0));
      if (npc_display_data[i].state > 0) {
        if (checkTrigger && (npc_display_data[i].vanishTime == 0) && (npc_display_data[i].triggerMode >= 4)) {
          range = ((npc_display_data[i].triggerMode - 4) * 32) + 16;
          dxDiff = (int16_t)(party_abs_x - npc_display_data[i].x);
          dyDiff = (int16_t)(party_abs_y - npc_display_data[i].y);
          if ((VB_Abs(dxDiff) + (VB_Abs(dyDiff) * 2)) < range) {
            if (npc_display_data[i].spriteFrames > 0) {
              npc_display_data[i].currentFrame = 0;
              init_walk_frames();
              PAL_ExTF((uint16_t *)&npc_display_data[i].direction, dxDiff, dyDiff);
              check_in_battle(1);
            }
            scene_enter_flag = 0;
            scene_leave_flag = 0;
            process_Script(i, &npc_display_data[i].triggerScript);
          }
        }
      } else if (npc_display_data[i].vanishTime == 0) {
        newX = (int16_t)(npc_display_data[i].x - RPG_viewport_x);
        newY = (int16_t)(npc_display_data[i].y - RPG_viewport_y);
        if (newX < 0 || newX > 320 || newY < 0 || newY > 220) {
          npc_display_data[i].state = VB_Abs(npc_display_data[i].state);
          npc_display_data[i].currentFrame = 0;
        }
      }
    }
    if (scene_to_load != RPG_curr_scene)
      return;
  }

  for (i = 1; i <= curr_scene_event_count; i++) {
    if (npc_display_data[i].state > 0) {
      if (npc_display_data[i].vanishTime == 0)
        process_AutoScript(i, &npc_display_data[i].autoScript);
      if (checkTrigger && npc_display_data[i].state == 2 && npc_display_data[i].spriteNum > 0) {
        dxDiff = party_abs_x - npc_display_data[i].x;
        dyDiff = party_abs_y - npc_display_data[i].y;
        if (VB_Abs(dxDiff) + VB_Abs(dyDiff) * 2 < 13) {
          triggerScript = npc_display_data[i].direction;
          do {
            triggerScript = (triggerScript + 1) & 3;
            newX = party_abs_x + fh_M_MSG_global[triggerScript];
            newY = party_abs_y + key_repeat_delay[triggerScript];
            if (load_map_data(newX, newY, 0)) {
              x_off = party_abs_x;
              y_off = party_abs_y;
              party_abs_x = newX;
              party_abs_y = newY;
              viewport_x_bak = RPG_viewport_x;
              viewport_y_bak = RPG_viewport_y;
              RPG_viewport_x = party_abs_x - team_abstract_x;
              RPG_viewport_y = party_abs_y - team_abstract_y;
              update_viewport_scroll();
              break;
            }
          } while (triggerScript != npc_display_data[i].direction);
        }
      }
    }
  }

  RPG_change_chaserate_times--;
  if (RPG_change_chaserate_times <= 0) {
    RPG_ememy_chase_rate = 1;
    RPG_change_chaserate_times = 0;
  }
}

void Load_system_files(void) {
  int32_t fh;
  int32_t fileSize;
  int32_t i;

  fh = open_file_required("SSS.MKF");
  if (fh > 0) {
    PAL_ReadFile(fh, file_offset_table, 24);
    read_mkf_subfile(fh, 0, events);
    event_object_count = (uint16_t)(tmp_file_size / 32);
    read_mkf_subfile(fh, 1, (uint8_t *)global_buf_1);
    PAL_CopyMem((uint8_t *)&scenes[1], global_buf_1, tmp_file_size);
    read_mkf_subfile(fh, 2, (uint8_t *)global_buf_1);
    PAL_CopyMem(&objects[0], global_buf_1, tmp_file_size);
    read_mkf_subfile(fh, 3, sss_script_data);
    read_mkf_subfile(fh, 4, sss_script_data_2);
    sss_subfile_count = (uint16_t)((tmp_file_size / 8.0f) - 1);
    PAL_CloseFile(fh);
  }

  fh = open_file_required("DATA.MKF");
  if (fh > 0) {
    PAL_ReadFile(fh, file_offset_table, 64);
    read_mkf_subfile(fh, 0, (uint8_t *)global_buf_1);
    PAL_CopyMem(&data_store[0][0], global_buf_1, tmp_file_size);
    read_mkf_subfile(fh, 1, data_enemy);
    read_mkf_subfile(fh, 2, data_enemy_team);
    read_mkf_subfile(fh, 3, (uint8_t *)global_buf_1);
    PAL_CopyMem(&playerRoles, global_buf_1, tmp_file_size);
    read_mkf_subfile(fh, 4, data_magic);
    read_mkf_subfile(fh, 5, data_battlefield);
    read_mkf_subfile(fh, 6, (uint8_t *)global_buf_1);
    PAL_CopyMem(&data_levelup_magic[0], global_buf_1, tmp_file_size);
    read_mkf_subfile(fh, 9, menu_bg_data);
    menu_bg_size = tmp_file_size;
    read_mkf_subfile(fh, 10, data_object_ext);
    data_mkf_handle = tmp_file_size;
    read_mkf_subfile(fh, 11, (uint8_t *)global_buf_1);
    PAL_CopyMem(battle_effect_data, global_buf_1, tmp_file_size);
    read_mkf_subfile(fh, 12, (uint8_t *)global_buf_1);
    PAL_CopyMem(data_mkf_chunk12, global_buf_1, tmp_file_size);
    read_mkf_subfile(fh, 13, (uint8_t *)global_buf_1);
    PAL_CopyMem(&enemy_pos_data, global_buf_1, tmp_file_size);
    read_mkf_subfile(fh, 14, data_levelup_exp);
    PAL_CloseFile(fh);
  }

  fh = open_file_required("BALL.MKF");
  if (fh > 0) {
    fileSize = PAL_GetFileSize(fh);
    PAL_ReadFile(fh, ball_mkf_data, fileSize);
    PAL_CloseFile(fh);
  }

  pal_vbOpenWordDat();
  for (i = 0; i <= 564; i++) {
    pal_vbReadWordDat((uint8_t *)word_dat_data[i]);
  }
  pal_vbCloseWordDat();
}

void player_attack_all(int16_t roleIdx, int16_t animate) {
  int32_t roleID = party[roleIdx].role;
  int32_t soundAttrIdx, i, j;
  float dmgScale;
  int32_t atkPower, tmp, damage;

  soundAttrIdx = 69;
  dmgScale = 1.0f;
  int32_t rngHit = VB_Int(VB_rtcRandomNext() * 6.0);
  if ((player_status[roleIdx][5] > 0) || (rngHit == 4)) {
    dmgScale = 3.0f;
    soundAttrIdx = 71;
  }
  soundAttrIdx = playerRoles(roleID, soundAttrIdx);

  effect_particle_count = 0;
  load_battle_effect_sprites();

  if ((int16_t)animate == 0) {
    player_battle_sprite[roleIdx].direction = 7;
    draw_battle_scene(4, 0);
  }
  play_sound(soundAttrIdx, 1);
  player_battle_sprite[roleIdx].direction = 8;
  if ((int16_t)animate == 0) {
    player_battle_sprite[roleIdx].x -= 40;
    player_battle_sprite[roleIdx].y -= 18;
  }
  draw_battle_scene(2, 0);
  player_battle_sprite[roleIdx].x -= 10;
  player_battle_sprite[roleIdx].y -= 5;
  player_battle_sprite[roleIdx].direction = 9;
  draw_battle_scene(1, 0);
  player_battle_sprite[roleIdx].x -= 2;
  player_battle_sprite[roleIdx].y -= 1;

  effect_particle_count = 0;
  effect_frame_count = battle_effect_data[player_battle_sprite[roleIdx].spriteNum].frames * 3;

  for (i = 0; i <= team_number; i++) {
    if (enemy_battle_data[i].hp > 0) {
      effect_particle_count++;
      effect_x_coords[effect_particle_count] = enemy_battle_data[i].x;
      effect_y_coords[effect_particle_count] = (enemy_battle_data[i].y - (enemy_battle_data[i].tileData / 3)) + 10;
      effect_frames[effect_particle_count] = 99;
    }
  }
  play_sound(playerRoles(roleID, 70), 1);
  draw_battle_scene(1, 0);
  effect_frame_count++;

  for (j = 0; j <= 4; j++) {
    i = enemy_attack_order[j];
    if (enemy_battle_data[i].hp > 0) {
      battle_order_array[i] = (uint16_t)-1;
      atkPower = get_player_attribute_total(roleID, 17);
      tmp = calc_level_bonus(i, 4);
      damage = (calc_battle_damage(atkPower, enemy_runtime_data[i].defense + tmp) * 2) /
               enemy_runtime_data[i].physicalResistance;
      damage = vb_round_banker_i16_d((double)damage * dmgScale);
      battle_order_array[i + 20] += damage;
      dmgScale = dmgScale / 2.0f;
      add_damage_number(enemy_battle_data[i].x, enemy_battle_data[i].y - 110, damage, 1);
      player_hit_flags[i] = (uint16_t)-1;
    }
  }
  draw_battle_scene(1, 0);
  effect_frame_count++;
  player_battle_sprite[roleIdx].x += 2;
  player_battle_sprite[roleIdx].y += 1;
  draw_battle_scene(1, 0);
  effect_particle_count = 0;
  enemy_hit_reaction(3);
}

void update_viewport_scroll(void) {
  int16_t srcX, srcY, tmp3, tmp;
  int16_t scrollDirX = 0, scrollDirY = 0;
  int16_t srcTileX1 = 0, srcTileX2 = 0, srcTileX1b = 0, srcTileX2b = 0;
  int16_t srcTileY1 = 0, srcTileY2 = 0, srcTileY1b = 0, srcTileY2b = 0;
  int16_t dstTileX, dstTileY;

  viewport_scroll_x = relative_viewport_x;
  viewport_scroll_y = relative_viewport_y;
  relative_viewport_x = (relative_viewport_x + RPG_viewport_x - viewport_x_bak + 320) % 320;
  relative_viewport_y = (relative_viewport_y + RPG_viewport_y - viewport_y_bak + 200) % 200;

  if (multi_event_state != 0)
    return;
  if (VB_Abs((int16_t)(RPG_viewport_x - viewport_x_bak)) >= 320 ||
      VB_Abs((int16_t)(RPG_viewport_y - viewport_y_bak)) >= 200)
    return;

  if (RPG_viewport_x != viewport_x_bak) {
    scrollDirX = 1;
    if (RPG_viewport_x > viewport_x_bak) {
      srcX = viewport_x_bak + 320;
      switch (relative_viewport_x) {
      case 0:
        srcTileX1 = viewport_scroll_x;
        srcTileX2 = 319;
        break;
      default:
        if (relative_viewport_x > viewport_scroll_x) {
          srcTileX1 = viewport_scroll_x;
          srcTileX2 = relative_viewport_x - 1;
        } else {
          scrollDirX = 2;
          srcTileX1 = viewport_scroll_x;
          srcTileX2 = 319;
          srcX = viewport_x_bak + 320;
          srcTileX1b = 0;
          srcTileX2b = relative_viewport_x - 1;
          tmp3 = srcX + (320 - srcTileX1);
        }
        break;
      }
    } else {
      srcX = RPG_viewport_x;
      if (viewport_scroll_x == 0) {
        srcTileX1 = relative_viewport_x;
        srcTileX2 = 319;
      } else if (relative_viewport_x > viewport_scroll_x) {
        scrollDirX = 2;
        srcTileX1 = relative_viewport_x;
        srcTileX2 = 319;
        srcTileX1b = 0;
        srcTileX2b = viewport_scroll_x - 1;
        tmp3 = srcX + (320 - srcTileX1);
      } else {
        srcTileX1 = relative_viewport_x;
        srcTileX2 = viewport_scroll_x - 1;
      }
    }
  }

  if (RPG_viewport_y != viewport_y_bak) {
    scrollDirY = 1;
    if (RPG_viewport_y > viewport_y_bak) {
      srcY = viewport_y_bak + 200;
      switch (relative_viewport_y) {
      case 0:
        srcTileY1 = viewport_scroll_y;
        srcTileY2 = 199;
        break;
      default:
        if (relative_viewport_y > viewport_scroll_y) {
          srcTileY1 = viewport_scroll_y;
          srcTileY2 = relative_viewport_y - 1;
        } else {
          scrollDirY = 2;
          srcTileY1 = viewport_scroll_y;
          srcTileY2 = 199;
          srcY = viewport_y_bak + 200;
          srcTileY1b = 0;
          srcTileY2b = relative_viewport_y - 1;
          tmp = srcY + (200 - srcTileY1);
        }
        break;
      }
    } else {
      srcY = RPG_viewport_y;
      if (viewport_scroll_y == 0) {
        srcTileY1 = relative_viewport_y;
        srcTileY2 = 199;
      } else if (relative_viewport_y > viewport_scroll_y) {
        scrollDirY = 2;
        srcTileY1 = relative_viewport_y;
        srcTileY2 = 199;
        srcTileY1b = 0;
        srcTileY2b = viewport_scroll_y - 1;
        tmp = srcY + (200 - srcTileY1);
      } else {
        srcTileY1 = relative_viewport_y;
        srcTileY2 = viewport_scroll_y - 1;
      }
    }
  }

  dstTileY = RPG_viewport_y + (200 - relative_viewport_y);
  dstTileX = RPG_viewport_x + (320 - relative_viewport_x);

  if (scrollDirX > 0) {
    if (relative_viewport_y > 0)
      redraw_tile(srcTileX1, 0, srcTileX2, relative_viewport_y - 1, srcX, dstTileY);
    redraw_tile(srcTileX1, relative_viewport_y, srcTileX2, 199, srcX, RPG_viewport_y);
  }
  if (scrollDirX == 2) {
    if (relative_viewport_y > 0)
      redraw_tile(srcTileX1b, 0, srcTileX2b, relative_viewport_y - 1, tmp3, dstTileY);
    redraw_tile(srcTileX1b, relative_viewport_y, srcTileX2b, 199, tmp3, RPG_viewport_y);
  }
  if (scrollDirY > 0) {
    if (relative_viewport_x > 0)
      redraw_tile(0, srcTileY1, relative_viewport_x - 1, srcTileY2, dstTileX, srcY);
    redraw_tile(relative_viewport_x, srcTileY1, 319, srcTileY2, RPG_viewport_x, srcY);
  }
  if (scrollDirY == 2) {
    if (relative_viewport_x > 0)
      redraw_tile(0, srcTileY1b, relative_viewport_x - 1, srcTileY2b, dstTileX, tmp);
    redraw_tile(relative_viewport_x, srcTileY1b, 319, srcTileY2b, RPG_viewport_x, tmp);
  }
  PAL_VWindow(0, 0, 319, 199);
}

void enemy_attack_role(int16_t enemyIdx, int16_t targetRole, int16_t itemID) {
  int16_t i = 0;
  int32_t magicIdx;
  int16_t attackerDmg, itemObj;
  int16_t targetFound, targetSelected;
  double rng;
  int32_t j, k, l, m;

  in_battle_action = -1;
  if (enemy_status[enemyIdx][0] <= 0) {
    process_Script(enemyIdx, &enemy_battle_data[enemyIdx].battleEndScript);
    i = 0;
    rng = VB_rtcRandomNext();
    if ((float)enemy_runtime_data[enemyIdx].magicRate >= (float)(rng * 10.0))
      magicIdx = enemy_runtime_data[enemyIdx].magic;
    else
      magicIdx = 0;

    if (magicIdx > 0 && enemy_status[enemyIdx][3]) {
      magicIdx = 0;
      i = (int16_t)vb_round_banker_u16(VB_rtcRandomNext());
    }
    if ((int16_t)magicIdx < 0 || i)
      return;

    if (magicIdx == 0) {
      attackerDmg = enemy_runtime_data[enemyIdx].attackStrength + (uint16_t)calc_level_bonus(enemyIdx, 6);
      enemy_physical_attack(enemyIdx, targetRole, attackerDmg, itemID);
    } else {
      itemObj = objects[magicIdx].data[0];
      attackerDmg = enemy_runtime_data[enemyIdx].magicStrength + (uint16_t)calc_level_bonus(enemyIdx, 6);
      enemy_magical_attack(enemyIdx, targetRole, magicIdx, attackerDmg);
      process_Script(targetRole, &objects[magicIdx].data[2]);
    }

    targetFound = -1;
    for (j = 0; j <= enemy_pos_count; j++) {
      itemObj = party[j].role;
      if (player_battle_sprite[j].origDirection == 0 && playerRoles(itemObj, 9) > 0) {
        if (playerRoles(itemObj, 9) < 50 && playerRoles(itemObj, 9) <= playerRoles(itemObj, 7) / 5)
          play_sound(playerRoles(itemObj, 74), 1);
        if (playerRoles(itemObj, 9) < 10) {
          if (vb_round_banker_u16(VB_rtcRandomNext()) && playerRoles(playerRoles(itemObj, 31), 9) > 0)
            targetFound = (int16_t)j;
        }
      }
    }
    update_player_battle_status();
    targetSelected = -1;
    k = 10;
    for (j = 1; j <= 5; j++) {
      for (l = 0; l <= enemy_pos_count; l++) {
        if (battle_role_action[l].flag == -1) {
          player_battle_sprite[l].x += k;
          player_battle_sprite[l].y += k / 2;
          for (m = 0; m <= enemy_pos_count; m++) {
            if (vb_round_banker_u16(VB_rtcRandomNext()) && party[m].role == playerRoles(party[l].role, 31))
              targetSelected = (int16_t)m;
          }
        }
      }
      draw_battle_scene(1, -1);
      k /= 2;
    }
    if (targetSelected >= 0) {
      if (playerRoles(party[targetSelected].role, 9) > 0) {
        process_Script((int16_t)targetSelected, &objects[playerRoles(party[targetSelected].role, 3)].data[2]);
      } else if (targetFound >= 0) {
        process_Script((int16_t)targetFound, &objects[playerRoles(party[targetFound].role, 3)].data[3]);
      }
    } else if (targetFound >= 0) {
      process_Script((int16_t)targetFound, &objects[playerRoles(party[targetFound].role, 3)].data[3]);
    }
    goto L_enemy_attack_role_done;
  } else {
    k = random_enemy_id();
    if (k != enemyIdx)
      enemy_attack_enemy(enemyIdx, k);
  L_enemy_attack_role_done:
    in_battle_action = 0;
    return;
  }
}

void role_physical_attack(int16_t attackerIdx, int16_t targetIdx, int16_t *targetRole, int16_t isCritical) {
  int16_t attackerRole = party[attackerIdx].role;
  int32_t damage = 0;
  int16_t hitCount, soundNum;
  int16_t frameDelay;
  int16_t targetX, targetY, j;
  double rng;

  effect_particle_count = 0;
  load_battle_effect_sprites();

  if (targetIdx > 2)
    damage = (targetIdx - attackerIdx) * 8;

  if (isCritical == 0) {
    player_battle_sprite[attackerIdx].direction = 7;
    draw_battle_scene(4, 0);
  }

  rng = VB_rtcRandomNext();
  *targetRole = vb_round_banker_i16_d((double)(int16_t)(*targetRole + 1) + rng);

  if ((VB_Int(VB_rtcRandomNext() * 6.0) == 3) || (player_status[attackerIdx][5] > 0)) {
    *targetRole *= 3;
    hitCount = 2;
    soundNum = playerRoles(attackerRole, 71);
  } else {
    hitCount = 1;
    soundNum = playerRoles(attackerRole, 69);
  }

  rng = VB_rtcRandomNext();
  *targetRole = vb_round_banker_i16_d((double)*targetRole * (1.0 + ((double)rng / 8.0)));

  int32_t rngCrit = VB_Int(VB_rtcRandomNext() * 12.0);
  if ((attackerRole == 0) && (rngCrit == 3)) {
    *targetRole *= 2;
    hitCount = 2;
    soundNum = playerRoles(attackerRole, 71);
  }

  play_sound(soundNum, 1);
  player_battle_sprite[attackerIdx].direction = 8;
  player_battle_sprite[attackerIdx].x = (enemy_battle_data[targetIdx].x + 64) - damage;
  player_battle_sprite[attackerIdx].y = (enemy_battle_data[targetIdx].y + 20) + damage;
  draw_battle_scene(2, 0);
  player_battle_sprite[attackerIdx].animOffset = 4;
  player_battle_sprite[attackerIdx].x -= 10;
  player_battle_sprite[attackerIdx].y -= 2;
  draw_battle_scene(1, 0);
  player_battle_sprite[attackerIdx].direction = 9;
  player_battle_sprite[attackerIdx].x -= 16;
  player_battle_sprite[attackerIdx].y -= 4;

  effect_particle_count = hitCount;
  effect_frame_count = battle_effect_data[player_battle_sprite[attackerIdx].spriteNum].frames * 3;
  targetX = enemy_battle_data[targetIdx].x;
  targetY = (enemy_battle_data[targetIdx].y - (enemy_battle_data[targetIdx].tileData / 3)) + 10;

  for (j = 1; j <= effect_particle_count; j++) {
    effect_x_coords[j] = targetX;
    effect_y_coords[j] = targetY;
    effect_frames[j] = 50;
    targetX -= 16;
    targetY += 16;
  }

  play_sound(playerRoles(attackerRole, 70), 1);
  draw_battle_scene(1, 0);
  effect_frame_count++;
  add_damage_number(enemy_battle_data[targetIdx].x, enemy_battle_data[targetIdx].y - 110, *targetRole, 1);
  player_hit_flags[targetIdx] = (uint16_t)-1;
  draw_battle_scene(1, 0);
  effect_frame_count++;
  player_battle_sprite[attackerIdx].x += 2;
  player_battle_sprite[attackerIdx].y += 1;
  draw_battle_scene(1, 0);
  effect_particle_count = 0;

  frameDelay = 8;
  for (j = 0; j <= 2; j++) {
    enemy_battle_data[targetIdx].x -= frameDelay;
    frameDelay /= 2;
    enemy_battle_data[targetIdx].y -= frameDelay;
    frameDelay = -frameDelay;
    draw_battle_scene(1, 0);
  }
}

void inventory_use_menu(void) {
  int16_t itemObj;
  int32_t j = 0;
  int32_t i, roleID, equipSlot, currEquip;
  int32_t rowY, colorNum;
  int16_t k, m;
  int16_t itemType, invIdx;
  int16_t maxHP;

  for (;;) {
    itemObj = select_item_with_filter(&magic_select_tmp, 0, 2);
    if (itemObj < 0)
      return;

    load_fbp_subfile(1);
    screen_buffer_ptr = PAL_ArrayPtr(global_buf_1);
    PAL_CopyMem(bg_buf, global_buf_1, 64000);
    j = 0;

  L_00415BA6:
    for (;;) {
      itemType = objects[itemObj].data[6] / 64;
      for (i = 0; i <= RPG_team_number; i++) {
        battle_order_array[i] = playerRoles(party[i].role, 3);
        PAL_GetBin(&battle_order_array[100 + i], itemType, party[i].role);
      }

    L_00415C46:
      for (;;) {
        PAL_CopyMem(global_buf_1, bg_buf, 64000);
        read_ball_mkf_index(objects[itemObj].data[0]);
        PAL_PutP(16, 15, (const uint8_t *)(global_buf_2 + global_buf_2[0]), (void *)screen_buffer_ptr, 0, 0);
        draw_text_at(4, 69, itemObj, 13);
        if (find_inventory_item(itemObj) >= 0) {
          invIdx = find_inventory_item(itemObj);
          display_number(72, 72, inventory[invIdx].amount, 2);
        }

        roleID = party[j].role;
        rowY = 110;
        draw_menu_table(3, rowY - 12, 0, 3, RPG_team_number + 1, -1);

        for (i = 0; i <= RPG_team_number; i++) {
          if (battle_order_array[i + 100]) {
            colorNum = (j == i) ? 250 : 78;
          } else if (j == i) {
            colorNum = 28;
          } else {
            colorNum = 24;
          }
          draw_text_at(16, 110 + i * 18, playerRoles(party[i].role, 3), colorNum);
        }

        rowY = 11;
        for (equipSlot = 11; equipSlot <= 16; equipSlot++) {
          draw_text_at(132, rowY, playerRoles(roleID, equipSlot), 78);
          rowY += 22;
        }
        rowY = 16;
        for (equipSlot = 17; equipSlot <= 21; equipSlot++) {
          currEquip = playerRoles(roleID, equipSlot);
          for (k = 11; k <= 16; k++)
            currEquip += equipment_effect(roleID, k, equipSlot);
          display_number(278, rowY, currEquip, 0);
          rowY += 22;
        }

        PAL_PopScreen(global_buf_1);
        k = read_key();
        if (itemObj <= 0)
          goto L_004160F6;
        m = k;
        switch (m) {
        case 3:
          j--;
          if (j < 0)
            j = RPG_team_number;
          goto L_00415C46;
        case 4:
          j++;
          if (j > RPG_team_number)
            j = 0;
          goto L_00415C46;
        case 2:
          if (battle_order_array[j + 100]) {
            // NOTE: 有意偏离 golden：原版用未初始化局部（恒 0），作用对象恒为角色 0；
            // remake 按明显意图取 roleID（存/恢复列 65 = 防物品脚本改战斗形象）。
            maxHP = playerRoles(roleID, 65);
            process_Script(j, &objects[itemObj].data[3]);
            playerRoles(roleID, 65) = maxHP;
            remove_inventory_item(itemObj, 1);
            add_inventory_item(coop_magic_tmp, 1);
            itemObj = coop_magic_tmp;
            if (itemObj > 0)
              goto L_00415BA6;
          }
          goto L_00415C46;
        default:
          if (m > 4)
            break;
          goto L_004160E6;
        }
      }
    }
  L_004160E6:
    if (m != 1)
      break;
  }
L_004160F6:
  compact_inventory();
  screen_buffer_ptr = 0;
  return;
}

void show_role_status(int16_t roleIdx) {
  int32_t roleID = party[roleIdx].role;
  int32_t j, rowY, attrVal;
  int32_t i, equipObjID, equipAttr;

  load_fbp_subfile(0);
  screen_buffer_ptr = PAL_ArrayPtr(global_buf_1);

  draw_text_at(6, 8, 2, 78);
  display_number(66, 8, (uint16_t)vb_round_banker_i16(playerExp.primary[roleID].exp), 0);
  PAL_CopyMem(&battle_order_array[0], &data_levelup_exp[0], 200);
  display_number(66, 18, battle_order_array[playerExp.primary[roleID].level], 1);
  draw_text_at(6, 34, 48, 78);
  display_number(64, 38, playerRoles(roleID, 6), 0);

  rowY = 58;
  for (j = 7; j <= 8; j++) {
    draw_text_at(6, rowY - 1, 42 + j, 78);
    display_number(70, rowY, playerRoles(roleID, j + 2), 0);
    PAL_PutP(74, rowY + 3, (const uint8_t *)(global_buf_2 + global_buf_2[39]), (void *)screen_buffer_ptr, 0, 0);
    display_number(90, rowY + 6, playerRoles(roleID, j), 1);
    rowY += 22;
  }
  for (j = 17; j <= 21; j++) {
    draw_text_at(6, rowY - 4, j + 34, 78);
    display_number(70, rowY, get_player_attribute_total(roleID, j), 0);
    rowY += 20;
  }

  battle_sprite_data[0] = 190;
  battle_sprite_data_ext[0] = 0;
  battle_sprite_data[1] = 248;
  battle_sprite_data_ext[1] = 40;
  battle_sprite_data[2] = 252;
  battle_sprite_data_ext[2] = 102;
  battle_sprite_data[3] = 202;
  battle_sprite_data_ext[3] = 134;
  battle_sprite_data[4] = 142;
  battle_sprite_data_ext[4] = 142;
  battle_sprite_data[5] = 82;
  battle_sprite_data_ext[5] = 126;
  for (j = 0; j <= 5; j++) {
    attrVal = playerRoles(roleID, j + 11);
    if (attrVal > 0) {
      read_ball_mkf_index(objects[attrVal].data[0]);
      PAL_PutP(battle_sprite_data[j], battle_sprite_data_ext[j],
               (const uint8_t *)(global_buf_2 + ((const uint16_t *)global_buf_2)[0]), (void *)screen_buffer_ptr, 0, 0);
      draw_text_at(battle_sprite_data[j] + 4, battle_sprite_data_ext[j] + 42, attrVal, 189);
    }
  }

  show_face(155, 78, playerRoles(roleID, 0));
  draw_text_at(110, 8, playerRoles(roleID, 3), 78);

  attrVal = 0;
  for (i = 0; i <= 15; i++) {
    equipObjID = poison_status[roleIdx][i].poisonID;
    equipAttr = objects[equipObjID].data[1];
    if ((equipAttr > 0) && (equipObjID > 0) && (attrVal < 4)) {
      draw_text_at(188, 60 + (attrVal * 18), equipObjID, equipAttr + 10);
      attrVal++;
    }
  }

  PAL_PopScreen(global_buf_1);
  screen_buffer_ptr = 0;
  if (flag_battling)
    PAL_CopyMem(global_buf_1, bg_buf, 64000);
  (void)read_key();
}

int16_t select_theurgy(int16_t roleID, int16_t *cursorIdx, int16_t filterMask) {
  int16_t maxMagic = -1;
  int16_t i, j;
  int32_t magicObjID;
  int32_t menuX = 10, menuY = 40;
  int32_t colCount = 3, rowCount = 5, maxPage = 2;
  int16_t k = colCount - 1, tmp = rowCount - 1;
  int16_t maxScroll;
  int16_t rowPos, colPos, l = 0;
  int16_t prevCol = 0, prevRow = 0, prevScroll = 0;
  int16_t inputResult = -2;
  intptr_t savedScreen;
  int32_t currMagicObj;
  int16_t highlightColor;
  int16_t m, n, tmp2, tmp3, tmp4;

  sort_player_magic(roleID);
  PAL_ClearMenu((uint8_t *)battle_order_array, 32);

  for (i = 32; i <= 63; i++) {
    j = i - 32;
    magicObjID = playerRoles(roleID, i);
    if (magicObjID > 0) {
      battle_order_array[j] = 1;
      maxMagic++;
      if (objects[magicObjID].data[6] & filterMask) {
        copy_subfile_data(objects[playerRoles(roleID, i)].data[0]);
        if (playerRoles(roleID, 10) >= theurgy_data.costMP)
          battle_order_array[j] = 2;
      }
    }
  }

  maxScroll = ((maxMagic + k) / colCount) - maxPage;
  if (maxScroll < 0)
    maxScroll = 0;
  if (*cursorIdx > maxMagic)
    *cursorIdx = maxMagic;
  rowPos = *cursorIdx / 3;
  colPos = *cursorIdx % 3;
  if (rowPos > maxPage) {
    l = rowPos - maxPage;
    rowPos = maxPage;
  }

  if (filterMask == 1) {
    screen_buffer_ptr = PAL_ArrayPtr(global_buf_1);
    PAL_PushScreen(global_buf_1);
  } else {
    screen_buffer_ptr = PAL_ArrayPtr(fire_mkf_data);
    PAL_PushScreen((uint8_t *)fire_mkf_data);
  }

  while (inputResult == -2) {
    draw_menu_table(menuX, menuY, 9, 17, rowCount, 0);
    if (l < 0)
      l = 0;
    if (l > maxScroll)
      l = maxScroll;
    if (l > 0 && rowPos < maxPage) {
      rowPos = maxPage;
      l--;
    }
    if (l < maxScroll && rowPos > maxPage) {
      rowPos = maxPage;
      l++;
    }
    PAL_Ffxy(&colPos, &rowPos, k, tmp);
    *cursorIdx = ((l + rowPos) * colCount) + colPos;
    if (battle_order_array[*cursorIdx] == 0) {
      colPos = prevCol;
      rowPos = prevRow;
      l = prevScroll;
      *cursorIdx = ((l + rowPos) * colCount) + colPos;
      if (*cursorIdx > maxMagic) {
        colPos = 0;
        rowPos = 0;
        *cursorIdx = 0;
        l = 0;
      }
    }

    currMagicObj = playerRoles(roleID, 32 + *cursorIdx);
    copy_subfile_data(objects[currMagicObj].data[0]);
    frame_menu(0, 0, 5, 0);
    display_number(20, 13, theurgy_data.costMP, 0);
    PAL_PutP(28, 13, (const uint8_t *)(global_buf_2 + global_buf_2[39]), (void *)screen_buffer_ptr, 0, 0);
    display_number(46, 13, playerRoles(roleID, 10), 2);

    m = menuY + 12;
    tmp3 = l * colCount;
    for (n = 0; n <= tmp; n++) {
      tmp2 = menuX + 24;
      for (tmp4 = 0; tmp4 <= k; tmp4++) {
        if (battle_order_array[tmp3] > 0) {
          if (battle_order_array[tmp3] == 2) {
            if ((tmp3 == *cursorIdx) && (inputResult == -2))
              highlightColor = 250;
            else
              highlightColor = 78;
          } else if ((tmp3 == *cursorIdx) && (inputResult == -2)) {
            highlightColor = 28;
          } else {
            highlightColor = 24;
          }
          draw_text_at(tmp2, m, playerRoles(roleID, tmp3 + 32), highlightColor);
          if (tmp3 == *cursorIdx)
            make_dialog_frame(tmp2 + 24, m + 11);
        }
        tmp2 += 88;
        tmp3++;
      }
      m += 18;
    }

    if (filterMask == 1)
      PAL_PopScreen(global_buf_1);
    else
      PAL_PopScreen((uint8_t *)fire_mkf_data);

    savedScreen = screen_buffer_ptr;
    screen_buffer_ptr = 0;
    process_Script(2, &objects[currMagicObj].data[5]);
    screen_buffer_ptr = savedScreen;

    j = read_key();
    prevCol = colPos;
    prevRow = rowPos;
    prevScroll = l;
    if (j == 3)
      rowPos--;
    if (j == 4)
      rowPos++;
    if (j == 5)
      colPos--;
    if (j == 6)
      colPos++;
    if (j == 7)
      l -= maxPage;
    if (j == 8)
      l += maxPage;
    if (j == 1)
      inputResult = -1;
    if (j == 2 && battle_order_array[*cursorIdx] == 2)
      inputResult = *cursorIdx;
  }

  screen_buffer_ptr = 0;
  if (inputResult >= 0)
    inputResult = playerRoles(roleID, inputResult + 32);
  return inputResult;
}

void process_Script(int16_t eventObjID, uint16_t *scriptEntry) {
  int16_t dialogX, dialogY, dialogTemp, imgOffset;
  int16_t continueFlag;
  int32_t savedEntry;
  uint16_t subfileIdx;
  int16_t opcode, operand1, operand2, operand3;
  int16_t tmp, tmp2;
  int32_t battleResult;
  int16_t i;
  double rng;

  redraw_hp_mp_flag = -1;
  continueFlag = 0;
  savedEntry = (*scriptEntry);
  flag_trigger = 0;
  flag_key_updown = 0;
  dialog_x = 79;
  dialog_y = 45;
  dialog_color = 26;
  menu_cursor_pos = 141;
  dialog_type = 1;
  dialog_text_x = 12;
  dialog_text_y = 8;
  dialog_width = 44;
  dialog_height = 26;

L_004178B2:
  for (;;) {
    PAL_CvLong((uint16_t)(*scriptEntry), &subfileIdx);
    if (subfileIdx > sss_subfile_count || *scriptEntry == 0)
      break;

    PAL_CopyMem(save_temp_buf, &sss_script_data_2[subfileIdx * 8], 8);
    opcode = save_temp_buf[0];
    operand1 = save_temp_buf[1];
    operand2 = save_temp_buf[2];
    operand3 = save_temp_buf[3];
    tmp2 = opcode;

    switch (tmp2) {
    case -1:
      if (flag_trigger > 3) {
        show_dialog_image_and_wait();
        flag_trigger = 0;
        restore_screen();
      }
      if (flag_trigger == 0) {
        dialogX = dialog_width;
        dialogY = dialog_height;
        if (mutex_can_change_palette == 0)
          push_screen_buffer();
      }
      if (dialog_type >= 10) {
        load_script_data(operand1);
        imgOffset = image_offset_table[0] / 2;
        dialogX = dialog_width - (imgOffset * 8);
        frame_menu(dialogX, dialogY, imgOffset, 0);
        PAL_DrawString((const char *)&image_offset_table[1], (int16_t)(dialogX + 6), (int16_t)(dialogY + 8), 3, 64,
                       (void *)screen_buffer_ptr);
        dialogY += 18;
        flag_key_updown++;
        flag_trigger = flag_key_updown;
      } else {
        load_script_data(operand1);
        imgOffset = image_offset_table[0];
        if ((dialog_type > 0 && flag_trigger == 0) &&
            (((image_offset_table[imgOffset - 1] == 161 && image_offset_table[imgOffset] == 71) ||
              (image_offset_table[imgOffset - 1] == 163 && image_offset_table[imgOffset] == 186)) ||
             image_offset_table[imgOffset] == 58)) {
          PAL_DrawString((const char *)&image_offset_table[1], (int16_t)dialog_text_x, (int16_t)dialog_text_y, 0, 140,
                         (void *)screen_buffer_ptr);
        } else {
          if (dialog_type == 9) {
            dialogTemp = (dialogY > 100) ? 0 : 28;
            PAL_DrawString((const char *)&image_offset_table[1], (int16_t)(dialogX + dialogTemp), (int16_t)dialogY, 0,
                           (uint8_t)dialog_x, (void *)screen_buffer_ptr);
            dialog_height += 16;
          } else {
            flag_trigger++;
            render_dialog_control(dialogX, dialogY);
          }
          dialogTemp = (dialog_type == 0) ? 18 : 16;
          dialogY += dialogTemp;
        }
      }
      break;
    case 0:
      *scriptEntry = savedEntry;
      continueFlag = -1;
      break;
    case 1:
      *scriptEntry = increment_script_ip((*scriptEntry));
      continueFlag = -1;
      break;
    case 2:
      if (operand2 == 0) {
        *scriptEntry = operand1;
        continueFlag = -1;
      } else {
        npc_display_data[eventObjID].scriptIdleFrame++;
        if (npc_display_data[eventObjID].scriptIdleFrame < operand2) {
          *scriptEntry = operand1;
          continueFlag = -1;
        } else
          npc_display_data[eventObjID].scriptIdleFrame = 0;
      }
      break;
    case 3:
      if (operand2 == 0) {
        *scriptEntry = operand1;
        goto L_004178B2;
      } else {
        npc_display_data[eventObjID].scriptIdleFrame++;
        if (npc_display_data[eventObjID].scriptIdleFrame < operand2) {
          *scriptEntry = operand1;
          goto L_004178B2;
        } else {
          npc_display_data[eventObjID].scriptIdleFrame = 0;
        }
      }
      break;
    case 4:
      if (operand2 > 0) {
        tmp = operand2 - scenes[RPG_curr_scene].eventObjectIndex;
        if (tmp > 0 && tmp <= curr_scene_event_count)
          process_Script(tmp, (uint16_t *)&operand1);
      } else {
        process_Script(eventObjID, (uint16_t *)&operand1);
      }
      break;
    case 5:
      check_trigger_flag();
      if (mutex_can_change_palette == 0) {
        sprite_frame_count = operand1;
        if (operand2 == 0)
          operand2 = 1;
        if (operand3)
          init_walk_frames();
        check_in_battle(operand2);
        sprite_frame_count = 0;
      } else {
        restore_screen();
      }
      break;
    case 6:
      rng = VB_rtcRandomNext();
      if ((float)operand1 < (float)(rng * 100.0)) {
        if (operand2 != 0) {
          *scriptEntry = operand2;
          goto L_004178B2;
        } else {
          continueFlag = -1;
        }
      } else {
        goto L_0041805E;
      }
      break;
    case 7:
      check_trigger_flag();
      battleResult = process_Battle(operand1, operand3);
      *scriptEntry = increment_script_ip((*scriptEntry));
      if (operand2 != 0 && battleResult == 1)
        *scriptEntry = operand2;
      if (operand3 != 0 && battleResult == 2)
        *scriptEntry = operand3;
      goto L_004178B2;
    case 8:
      savedEntry = increment_script_ip((*scriptEntry));
      break;
    case 9:
      check_trigger_flag();
      if (operand1 == 0)
        operand1 = 1;
      for (i = 1; i <= operand1; i++) {
        if (operand3) {
          update_walk_frame();
          set_team_draw();
        }
        process_event_objects(operand2);
        check_in_battle(1);
      }
      break;
    case 10:
      flag_trigger = 0;
      flag_key_updown = 0;
      i = -1;
      while (i < 0)
        i = yes_no_menu(0, 19);
      if (i == 0) {
        *scriptEntry = operand1;
        goto L_004178B2;
      }
      break;
    default:
      if (tmp2 > 10)
        check_trigger_flag();
      break;
    }

L_0041805E:
    if (continueFlag)
      break;
    process_scripts(eventObjID, scriptEntry, opcode, &operand1, &operand2, &operand3);
    *scriptEntry = increment_script_ip((*scriptEntry));
  }
  check_trigger_flag();
}

void init_key_definitions(void) {
  float tmp = (float)PAL_GetTicks() / 1000.0f;
  (void)tmp;
  // NOTE: golden 经 PAL_GetKeyDefine 读 PAL.INI 头 4 字节覆盖方向键（缺文件
  // = 默认小键盘 8/2/4/6 = 0x48/0x50/0x4B/0x4D），命中默认时同步登记扩展
  // 箭头键（0xC8/0xD0/0xCB/0xCD）；remake 删除 PAL.INI 路径（PAL_GetKeyDefine
  // 桥一并退役），恒为默认组合。
  key_scan_codes[0] = 0x48;
  key_scan_codes[1] = 0x50;
  key_scan_codes[2] = 0x4B;
  key_scan_codes[3] = 0x4D;
  key_scan_codes[4] = 200;
  key_scan_codes[5] = 208;
  key_scan_codes[6] = 203;
  key_scan_codes[7] = 205;

  enemy_attack_order[0] = 2;
  enemy_attack_order[1] = 1;
  enemy_attack_order[2] = 0;
  enemy_attack_order[3] = 4;
  enemy_attack_order[4] = 3;
  key_scan_map[0] = 28;
  key_action_map[0] = 140;
  key_scan_map[1] = 0;
  key_action_map[1] = 155;
  key_scan_map[2] = 55;
  key_action_map[2] = 155;
  key_scan_map[3] = 27;
  key_action_map[3] = 170;

  rng_anim_frames[0] = 0;
  rng_anim_frames[1] = 3;
  rng_anim_frames[2] = 1;
  rng_anim_frames[3] = 5;
  rng_anim_frames[4] = 2;
  rng_anim_frames[5] = 4;

  fh_M_MSG_global[0] = -16;
  key_repeat_delay[0] = 8;
  key_pressed_flags[0] = (uint16_t)-1;
  key_direction_flags[0] = 1;
  fh_M_MSG_global[1] = -16;
  key_repeat_delay[1] = (uint16_t)-8;
  key_pressed_flags[1] = (uint16_t)-1;
  key_direction_flags[1] = (uint16_t)-1;
  fh_M_MSG_global[2] = 16;
  key_repeat_delay[2] = (uint16_t)-8;
  key_pressed_flags[2] = 1;
  key_direction_flags[2] = (uint16_t)-1;
  fh_M_MSG_global[3] = 16;
  key_repeat_delay[3] = 8;
  key_pressed_flags[3] = 1;
  key_direction_flags[3] = 1;

  key_mapping_table[0][0] = 27;
  key_mapping_table[0][1] = 1;
  key_mapping_table[1][0] = 45;
  key_mapping_table[1][1] = 1;
  // NOTE: 偏离 golden：原 [2]={18,1}（VK_MENU/Alt→取消）清零，见
  // pal_ext.c pal_keymap 同项登记（终端场景 Alt 被终端菜单拦截，易误触）。
  key_mapping_table[2][0] = 0;
  key_mapping_table[2][1] = 0;
  key_mapping_table[3][0] = 13;
  key_mapping_table[3][1] = 2;
  key_mapping_table[4][0] = 32;
  key_mapping_table[4][1] = 2;
  // NOTE: 偏离 golden：原 [5]={17,2}（VK_CONTROL/Ctrl→确认）清零。
  key_mapping_table[5][0] = 0;
  key_mapping_table[5][1] = 0;
  key_mapping_table[6][0] = 38;
  key_mapping_table[6][1] = 3;
  key_mapping_table[7][0] = 40;
  key_mapping_table[7][1] = 4;
  key_mapping_table[8][0] = 37;
  key_mapping_table[8][1] = 5;
  key_mapping_table[9][0] = 39;
  key_mapping_table[9][1] = 6;
  key_mapping_table[10][0] = 33;
  key_mapping_table[10][1] = 7;
  key_mapping_table[11][0] = 34;
  key_mapping_table[11][1] = 8;
  key_mapping_table[12][0] = 82;
  key_mapping_table[12][1] = 9;
  key_mapping_table[13][0] = 65;
  key_mapping_table[13][1] = 10;
  key_mapping_table[14][0] = 68;
  key_mapping_table[14][1] = 11;
  key_mapping_table[15][0] = 69;
  key_mapping_table[15][1] = 12;
  key_mapping_table[16][0] = 87;
  key_mapping_table[16][1] = 13;
  key_mapping_table[17][0] = 81;
  key_mapping_table[17][1] = 14;
  key_mapping_table[18][0] = 83;
  key_mapping_table[18][1] = 15;
  key_mapping_table[19][0] = 70;
  key_mapping_table[19][1] = 16;

  screen_buffer_ptr = 0;
  viewport_row_count = 200;
  viewport_row_stride = 320;
  viewport_row_limit = 200;
  max_subfile_size = 0;
}

int16_t select_item_with_filter(int16_t *cursorIdx, int16_t bgMode, int16_t filterMask) {
  int16_t j = 0, itemCount = 0, menuX = 0;
  int32_t rowCount = 3, maxPage = 7;
  int16_t k = 4, tmp2 = 0, menuY, colCount, maxScroll;
  int16_t prevRow, prevScroll, prevCol, colPos, tmp3;
  int16_t rowPos, invIdx, o, n, tmp4;
  int16_t tmp5;
  intptr_t tmp7;
  int16_t highlightColor;
  int16_t inputResult;
  int32_t l;

  menuY = rowCount - 1;
  colCount = maxPage - 1;
  maxScroll = compact_inventory();
  PAL_ClearMenu((uint8_t *)battle_order_array, 256);

  for (rowPos = 0; rowPos <= maxScroll; rowPos++)
    battle_order_array[rowPos] = objects[inventory[rowPos].item].data[6] & filterMask;

  colPos = maxScroll + 1;

  if ((bgMode == 0) && (filterMask == 1)) {
    for (rowPos = 0; rowPos <= RPG_team_number; rowPos++) {
      for (l = 11; l <= 16; l++) {
        prevCol = playerRoles(party[rowPos].role, l);
        if (prevCol > 0) {
          if (objects[prevCol].data[6] & 1) {
            maxScroll++;
            battle_order_array[maxScroll] = 1;
            inventory[maxScroll].item = prevCol;
            inventory[maxScroll].amount = 1;
            inventory[maxScroll].amountInUse = 0;
          }
        }
      }
    }
  }

  prevRow = ((maxScroll + menuY) / rowCount) - k;
  if (prevRow < 0)
    prevRow = 0;
  if ((int16_t)(*cursorIdx) > maxScroll)
    *cursorIdx = (uint16_t)maxScroll;
  tmp3 = (int16_t)(*cursorIdx) / 3;
  prevScroll = (int16_t)(*cursorIdx) % 3;
  if (tmp3 > k) {
    tmp2 = tmp3 - k;
    tmp3 = k;
  }

  if (bgMode == -1) {
    screen_buffer_ptr = PAL_ArrayPtr(fire_mkf_data);
    PAL_PushScreen((uint8_t *)fire_mkf_data);
    PAL_CopyMem(global_buf_1 + 64000, fire_mkf_data, 64000);
  } else {
    screen_buffer_ptr = PAL_ArrayPtr(global_buf_1);
    PAL_PushScreen(global_buf_1);
    PAL_CopyMem(global_buf_1 + 64000, global_buf_1, 64000);
  }

  highlightColor = -2;
  while (highlightColor == -2) {
    draw_menu_table(2, 0, 9, 18, maxPage, 0);
    if (tmp3 < 0)
      tmp3 = 0;
    if (tmp2 < 0)
      tmp2 = 0;
    if (tmp2 > prevRow)
      tmp2 = prevRow;
    if (tmp2 > 0 && tmp3 < k) {
      tmp3 = k;
      tmp2--;
    }
    if (tmp2 < prevRow && tmp3 > k) {
      tmp3 = k;
      tmp2++;
    } else if (tmp3 > colCount)
      tmp3 = colCount;
    if (prevScroll < 0)
      prevScroll = 0;
    if (prevScroll > menuY)
      prevScroll = menuY;

    *cursorIdx = ((tmp2 + tmp3) * rowCount) + prevScroll;
    if ((int16_t)(*cursorIdx) > maxScroll) {
      prevScroll = j;
      tmp3 = itemCount;
      tmp2 = menuX;
      *cursorIdx = ((tmp2 + tmp3) * rowCount) + prevScroll;
    }

    invIdx = 12;
    o = tmp2 * rowCount;
    for (n = 0; n <= (uint16_t)colCount; n++) {
      for (tmp4 = 0; tmp4 <= (uint16_t)menuY; tmp4++) {
        inputResult = inventory[o].amount - inventory[o].amountInUse;
        if (battle_order_array[o] && (inputResult > 0)) {
          if ((o == *cursorIdx) && (highlightColor == -2))
            tmp5 = 250;
          else {
            tmp5 = 78;
            if (o >= colPos)
              tmp5 = 200;
          }
        } else if ((o == *cursorIdx) && (highlightColor == -2)) {
          tmp5 = 28;
        } else {
          tmp5 = 24;
        }
        if (inventory[o].amount > 0)
          draw_text_at(16 + tmp4 * 100, invIdx, inventory[o].item, tmp5);
        if (inputResult > 1)
          display_number(16 + tmp4 * 100 + 84, invIdx + 6, inputResult, 0);
        if (o == *cursorIdx)
          make_dialog_frame(16 + tmp4 * 100 + 24, invIdx + 11);
        o++;
      }
      invIdx += 18;
    }

    if (bgMode == 1) {
      if (battle_order_array[*cursorIdx])
        draw_menu_with_text_and_hp(25, 224, 150, objects[inventory[*cursorIdx].item].data[1] / 2, 0);
      else
        frame_menu(224, 150, 5, 0);
    }
    draw_object_icon(0, 140, inventory[*cursorIdx].item);

    j = prevScroll;
    itemCount = tmp3;
    menuX = tmp2;

    if (bgMode == -1)
      PAL_PopScreen((uint8_t *)fire_mkf_data);
    else
      PAL_PopScreen(global_buf_1);

    if (bgMode <= 0) {
      tmp7 = screen_buffer_ptr;
      screen_buffer_ptr = 0;
      process_Script(151, &objects[inventory[*cursorIdx].item].data[5]);
      screen_buffer_ptr = tmp7;
    }

    tmp5 = read_key();
    if (tmp5 == 1)
      highlightColor = -1;
    if (battle_order_array[*cursorIdx] && (inventory[*cursorIdx].amount > inventory[*cursorIdx].amountInUse)) {
      if (tmp5 == 2)
        highlightColor = *cursorIdx;
    }
    if (tmp5 == 3)
      tmp3--;
    if (tmp5 == 4)
      tmp3++;
    if (tmp5 == 5)
      prevScroll--;
    if (tmp5 == 6)
      prevScroll++;
    if (tmp5 == 7)
      tmp2 -= 5;
    if (tmp5 == 8)
      tmp2 += 5;
  }

  screen_buffer_ptr = 0;
  if (highlightColor >= 0)
    highlightColor = inventory[highlightColor].item;
  for (rowPos = colPos; rowPos <= maxScroll; rowPos++) {
    inventory[rowPos].amount = 0;
    inventory[rowPos].item = 0;
  }
  return highlightColor;
}

void enemy_physical_attack(int16_t enemyIdx, int16_t targetIdx, int16_t targetRole, int16_t useMagic) {
  int16_t attackerRole = party[targetIdx].role;
  int16_t dmgRoll;
  int16_t atkPower, attackSound;
  int16_t rngVal, hitChance = -1;
  int16_t damage;
  int32_t i, callSound;
  double rng, rng2, rng3;

  PAL_ClearMenu((uint8_t *)battle_order_array, 20);
  atkPower = get_player_attribute_total(attackerRole, 19);
  if (player_battle_sprite[targetIdx].direction == 3)
    atkPower = atkPower + atkPower;
  battle_order_array[targetIdx] = (uint16_t)-1;
  rngVal = vb_round_banker_i16_d(-(double)VB_rtcRandomNext() * 0.85);
  if (check_player_alive(targetIdx) <= 1) {
    if (rngVal == -1) {
      rngVal = 1;
      hitChance = -1;
      for (i = 0; i <= enemy_pos_count; i++) {
        if (party[i].role == playerRoles(attackerRole, 31))
          hitChance = i;
      }
      if (hitChance >= 0) {
        if (check_player_alive(hitChance) > 1)
          rngVal = -(hitChance + 10);
      }
    } else {
      rngVal = 1;
    }
  }
  battle_order_array[targetIdx + 10] = (uint16_t)rngVal;

  attackSound = enemy_runtime_data[enemyIdx].attackSound;
  if (enemy_runtime_data[enemyIdx].magic == 0 && useMagic)
    attackSound = enemy_runtime_data[enemyIdx].magicSound;
  play_sound(attackSound, 0);

  if (enemy_runtime_data[enemyIdx].magicFrames > 0) {
    attack_done_flag = 0;
    for (i = 1; i <= enemy_runtime_data[enemyIdx].magicFrames; i++) {
      enemy_battle_data[enemyIdx].direction = (enemy_runtime_data[enemyIdx].idleFrames + i) - 1;
      draw_battle_scene(2, 0);
    }
  }
  for (i = 1; i <= (3 - enemy_runtime_data[enemyIdx].magicFrames); i++) {
    enemy_battle_data[enemyIdx].x -= 2;
    enemy_battle_data[enemyIdx].y -= 1;
    draw_battle_scene(1, 0);
  }
  play_sound(enemy_runtime_data[enemyIdx].actionSound, 0);
  draw_battle_scene(1, 0);

  attack_done_flag = 0;
  enemy_battle_data[enemyIdx].x = player_battle_sprite[targetIdx].x - 44;
  enemy_battle_data[enemyIdx].y = player_battle_sprite[targetIdx].y - 16;
  callSound = enemy_runtime_data[enemyIdx].callSound;
  if (rngVal == -1) {
    player_battle_sprite[targetIdx].direction = 3;
    callSound = playerRoles(party[targetIdx].role, 73);
  }
  if (rngVal <= -10) {
    hitChance = VB_Abs(rngVal + 10);
    player_battle_sprite[hitChance].direction = 3;
    callSound = playerRoles(party[hitChance].role, 73);
    player_battle_sprite[hitChance].x = player_battle_sprite[targetIdx].x - 24;
    player_battle_sprite[hitChance].y = player_battle_sprite[targetIdx].y - 12;
  }

  if (enemy_runtime_data[enemyIdx].attackFrames == 0) {
    enemy_battle_data[enemyIdx].direction = enemy_runtime_data[enemyIdx].idleFrames - 1;
    draw_battle_scene(3, 0);
  } else {
    for (i = 1; i <= enemy_runtime_data[enemyIdx].attackFrames; i++) {
      enemy_battle_data[enemyIdx].direction =
          (enemy_runtime_data[enemyIdx].idleFrames + enemy_runtime_data[enemyIdx].magicFrames + i) - 1;
      draw_battle_scene(enemy_runtime_data[enemyIdx].actWaitFrames, 0);
    }
  }

  if (rngVal == 0)
    player_battle_sprite[targetIdx].direction = 4;
  if (rngVal >= 0) {
    enemy_battle_data_ext[targetIdx] = (uint16_t)-1;
    rng = VB_rtcRandomNext();
    damage = calc_battle_damage(vb_round_banker_i16_d((double)targetRole + rng), atkPower);
    rng = VB_rtcRandomNext();
    rng2 = VB_rtcRandomNext();
    damage = vb_round_banker_i16_d((double)damage * (1.0 + ((double)rng / 8.0)) + (double)rng2);
    if (player_status[targetIdx][6] > 0)
      damage /= 2;
    if (damage > (int16_t)playerRoles(attackerRole, 9))
      damage = playerRoles(attackerRole, 9);
    playerRoles(attackerRole, 9) -= damage;
    add_damage_number(player_battle_sprite[targetIdx].x, player_battle_sprite[targetIdx].y - 70, damage, 1);
  }

  play_sound(callSound, 0);
  draw_battle_scene(1, 0);
  if (rngVal > -10) {
    player_battle_sprite[targetIdx].x += 8;
    player_battle_sprite[targetIdx].y += 4;
  } else {
    enemy_battle_data[enemyIdx].x -= 10;
    enemy_battle_data[enemyIdx].y -= 8;
    player_battle_sprite[hitChance].x += 4;
    player_battle_sprite[hitChance].y += 2;
  }
  draw_battle_scene(1, 0);
  if (playerRoles(attackerRole, 9) <= 0)
    play_sound(playerRoles(attackerRole, 68), 1);
  player_battle_sprite[targetIdx].x += 2;
  player_battle_sprite[targetIdx].y += 1;
  draw_battle_scene(3, 0);
  enemy_battle_data[enemyIdx].x = enemy_battle_data[enemyIdx].origX;
  enemy_battle_data[enemyIdx].y = enemy_battle_data[enemyIdx].origY;
  enemy_battle_data[enemyIdx].direction = 0;
  draw_battle_scene(1, 0);
  player_battle_sprite[targetIdx].direction = player_battle_sprite[targetIdx].origDirection;
  draw_battle_scene(1, 0);
  attack_done_flag = -1;
  rng2 = VB_rtcRandomNext();
  dmgRoll = get_player_attribute_total(party[targetIdx].role, 22);
  rng3 = VB_rtcRandomNext();
  if (rngVal >= 0 && enemy_runtime_data[enemyIdx].attackEquivItemRate > (float)(rng2 * 10.0) &&
      (float)(rng3 * 100.0) > (float)dmgRoll) {
    process_Script(targetIdx, &objects[enemy_runtime_data[enemyIdx].attackEquivItem].data[2]);
  }
}

void calc_display_theurgy(int16_t enemyIdx, int16_t targetRole, int16_t itemID, int16_t power) {
  int16_t itemObj, theurgyBaseDmg, effectIdx;
  int32_t j, k, frameCount;
  int16_t attrIdx, baseDmg;
  int16_t resistVal;
  double rng;
  int16_t damage;

  PAL_ClearMenu((uint8_t *)battle_order_array, 25);
  itemObj = objects[itemID].data[0];
  copy_subfile_data(itemObj);
  if ((int16_t)theurgy_data.effect < 0)
    return;

  theurgyBaseDmg = theurgy_data.baseDamage;
  if (enemyIdx <= enemy_pos_count) {
    load_theurgy_image(itemObj);
    if (theurgy_data.fireDelay == 0)
      player_battle_sprite[enemyIdx].direction = 6;
  }
  if (enemyIdx == 9) {
    itemObj = theurgy_data.effect;
    copy_subfile_data(itemObj);
  }
  attack_done_flag = 0;

  if (theurgy_data.type < 4 && theurgy_data.type > 0) {
    for (j = 0; j <= team_number; j++) {
      if (enemy_battle_data[j].hp > 0)
        battle_order_array[j] = (uint16_t)-1;
    }
  }

  effectIdx = theurgy_data.type;
  switch (effectIdx) {
  case 7:
  case 3:
    effect_particle_count = 1;
    effect_x_coords[1] = 160 + theurgy_data.xOffset;
    effect_y_coords[1] = 200 + theurgy_data.yOffset;
    effect_frames[1] = theurgy_data.layerOffset;
    break;
  case 6:
    effect_particle_count = 1;
    effect_x_coords[1] = 240 + theurgy_data.xOffset;
    effect_y_coords[1] = 160 + theurgy_data.yOffset;
    effect_frames[1] = theurgy_data.layerOffset;
    break;
  case 5:
    effect_particle_count = 0;
    for (j = 0; j <= enemy_pos_count; j++) {
      effect_particle_count++;
      effect_x_coords[effect_particle_count] = player_battle_sprite[j].x + theurgy_data.xOffset;
      effect_y_coords[effect_particle_count] = player_battle_sprite[j].y + theurgy_data.yOffset;
      effect_frames[effect_particle_count] = theurgy_data.layerOffset;
      battle_order_array[j] = (uint16_t)-1;
    }
    break;
  case 4:
    effect_particle_count = 1;
    effect_x_coords[1] = player_battle_sprite[targetRole].x + theurgy_data.xOffset;
    effect_y_coords[1] = player_battle_sprite[targetRole].y + theurgy_data.yOffset;
    effect_frames[1] = theurgy_data.layerOffset;
    battle_order_array[targetRole] = (uint16_t)-1;
    break;
  case 2:
    effect_particle_count = 1;
    effect_x_coords[1] = 120 + theurgy_data.xOffset;
    effect_y_coords[1] = 100 + theurgy_data.yOffset;
    effect_frames[1] = theurgy_data.layerOffset;
    break;
  case 1:
    effect_particle_count = 3;
    effect_x_coords[1] = 70 + theurgy_data.xOffset;
    effect_y_coords[1] = 140 + theurgy_data.yOffset;
    effect_x_coords[2] = 100 + theurgy_data.xOffset;
    effect_y_coords[2] = 110 + theurgy_data.yOffset;
    effect_x_coords[3] = 160 + theurgy_data.xOffset;
    effect_y_coords[3] = 100 + theurgy_data.yOffset;
    effect_frames[1] = effect_frames[2] = effect_frames[3] = theurgy_data.layerOffset;
    break;
  case 0:
    effect_particle_count = 1;
    effect_x_coords[1] = enemy_battle_data[targetRole].x + theurgy_data.xOffset;
    effect_y_coords[1] = enemy_battle_data[targetRole].y + theurgy_data.yOffset;
    effect_frames[1] = theurgy_data.layerOffset;
    battle_order_array[targetRole] = (uint16_t)-1;
    break;
  }

  effect_frame_count = 0;
  targetRole = enemyIdx;
  if (theurgy_data.fireDelay > 0) {
    if (enemyIdx < 9)
      play_sound(theurgy_data.sound, 0);
    for (frameCount = 0; frameCount < theurgy_data.fireDelay; frameCount++) {
      draw_battle_scene(1, theurgy_data.speed);
      effect_frame_count++;
    }
    player_battle_sprite[enemyIdx].direction = 6;
    targetRole = 10;
  }

  play_theurgy_anim(itemObj, effect_frame_count, targetRole);
  effect_particle_count = 0;
  blow_away_flag = 0;

  if (theurgy_data.type <= 3) {
    for (j = 0; j <= team_number; j++) {
      rng = VB_rtcRandomNext();
      damage = power + vb_round_banker_i16_d((double)power * rng) / 10;
      if (battle_order_array[j]) {
        attrIdx = theurgy_data.elemental;
        baseDmg = calc_level_bonus(j, 4) + enemy_runtime_data[j].defense;
        damage = (calc_battle_damage(damage, baseDmg) / 2) + (int16_t)theurgyBaseDmg;
        if (attrIdx > 0 && attrIdx < 7) {
          resistVal = enemy_runtime_data[j].elemResistance[0];
          if (attrIdx >= 2 && attrIdx <= 5)
            resistVal = enemy_runtime_data[j].elemResistance[attrIdx - 1];
          if (attrIdx == 6)
            resistVal = enemy_runtime_data[j].poisonResistance;
          damage = vb_round_banker_i16_d((double)damage * ((double)(10 - resistVal) / 5.0));
        }
        damage = calc_magic_damage(damage, theurgy_data.elemental);
        if (damage < 0)
          damage = 0;
        battle_order_array[20 + j] = (uint16_t)damage;
      }
    }
  } else {
    draw_battle_scene(1, -4);
    if (theurgy_data.type <= 7) {
      for (k = 2; k <= 16; k++) {
        PAL_WaitTime(4);
        for (j = 0; j <= enemy_pos_count; j++) {
          if (battle_order_array[j])
            get_sprite_frame_data(j, 10 - VB_Abs(k - 10));
        }
      }
    }
  }
  attack_done_flag = -1;
  battle_dest_x = 0;
}

void enemy_magical_attack(int16_t enemyIdx, int16_t targetRole, int16_t itemID, int16_t power) {
  int16_t itemObj, j, k;
  int16_t targetStart, targetEnd, minFrames;
  int16_t targetIdx, theurgyIdx;
  int16_t spriteIdx;
  double rng;
  int16_t damageMult;
  int16_t baseDamage;
  int16_t frameDelay = 16;

  itemObj = objects[itemID].data[0];
  copy_subfile_data(itemObj);
  attack_done_flag = 0;
  enemy_battle_data[enemyIdx].x += 12;
  enemy_battle_data[enemyIdx].y += 6;
  draw_battle_scene(1, 0);
  enemy_battle_data[enemyIdx].x += 4;
  enemy_battle_data[enemyIdx].y += 2;
  draw_battle_scene(1, -4);

  targetStart = (enemy_runtime_data[enemyIdx].idleFrames + enemy_runtime_data[enemyIdx].magicFrames) - 1;
  targetEnd = targetStart + enemy_runtime_data[enemyIdx].attackFrames - 1;
  spriteIdx = enemy_runtime_data[enemyIdx].magicSound;
  minFrames = 0;
  if (spriteIdx < 0) {
    spriteIdx = -spriteIdx;
    minFrames = 128;
  }
  play_sound(spriteIdx, 0);
  enemy_cast_anim(enemyIdx);
  load_theurgy_image(itemObj);
  if (battle_sub_flag == 0)
    PAL_WaitTime(20);

  if (theurgy_data.fireDelay == 0) {
    for (j = targetStart; j <= targetEnd; j++) {
      enemy_battle_data[enemyIdx].direction = j;
      draw_battle_scene(enemy_runtime_data[enemyIdx].actWaitFrames, 0);
    }
  }

  switch (theurgy_data.type) {
  case 3:
    effect_particle_count = 1;
    effect_x_coords[1] = 160 + theurgy_data.xOffset;
    effect_y_coords[1] = 200 + theurgy_data.yOffset;
    effect_frames[1] = theurgy_data.layerOffset;
    break;
  case 2:
    effect_particle_count = 1;
    effect_x_coords[1] = 240 + theurgy_data.xOffset;
    effect_y_coords[1] = 150 + theurgy_data.yOffset;
    effect_frames[1] = theurgy_data.layerOffset;
    break;
  case 1:
    effect_particle_count = 3;
    effect_x_coords[1] = 180 + theurgy_data.xOffset;
    effect_y_coords[1] = 180 + theurgy_data.yOffset;
    effect_x_coords[2] = 234 + theurgy_data.xOffset;
    effect_y_coords[2] = 170 + theurgy_data.yOffset;
    effect_x_coords[3] = 270 + theurgy_data.xOffset;
    effect_y_coords[3] = 146 + theurgy_data.yOffset;
    effect_frames[1] = effect_frames[2] = effect_frames[3] = theurgy_data.layerOffset;
    break;
  case 0:
    effect_particle_count = 1;
    effect_x_coords[1] = player_battle_sprite[targetRole].x + theurgy_data.xOffset;
    effect_y_coords[1] = player_battle_sprite[targetRole].y + theurgy_data.yOffset;
    effect_frames[1] = theurgy_data.layerOffset;
    break;
  }

  process_Script(targetRole, &objects[itemID].data[3]);
  effect_frame_count = 0;

  if (theurgy_data.fireDelay > 0) {
    play_sound(theurgy_data.sound, 0);
    enemy_battle_data[enemyIdx].direction = targetStart;
    for (k = 0; k < theurgy_data.fireDelay; k++) {
      draw_battle_scene(1, theurgy_data.speed);
      effect_frame_count++;
    }
    for (j = targetStart + 1; j <= targetEnd; j++) {
      enemy_battle_data[enemyIdx].direction = j;
      draw_battle_scene(enemy_runtime_data[enemyIdx].actWaitFrames, 0);
    }
    minFrames = 10;
  }

  PAL_ClearMenu((uint8_t *)battle_order_array, 20);
  if (theurgy_data.type > 0) {
    for (k = 0; k <= enemy_pos_count; k++) {
      targetRole = party[k].role;
      battle_order_array[10 + k] = 1;
      battle_order_array[k] = 0;
      if (playerRoles(targetRole, 9) > 0) {
        if (player_battle_sprite[k].direction == 3)
          battle_order_array[10 + k] = 2;
        if (player_status[k][6] > 0)
          battle_order_array[10 + k] *= 2;
        battle_order_array[k] = (uint16_t)-1;
        damageMult = vb_round_banker_i16_d(-(double)VB_rtcRandomNext() * 0.75);
        if (check_player_alive(k) < 2)
          damageMult = 1;
        if (damageMult == -1) {
          player_battle_sprite[k].direction = 3;
          battle_order_array[10 + k]++;
        }
      }
    }
  } else {
    battle_order_array[10 + targetRole] = 1;
    battle_order_array[targetRole] = (uint16_t)-1;
    if (player_battle_sprite[targetRole].direction == 3)
      battle_order_array[10 + targetRole] = 2;
    if (player_status[targetRole][6] > 0)
      battle_order_array[10 + targetRole] *= 2;
    damageMult = vb_round_banker_i16_d(-(double)VB_rtcRandomNext() * 0.75);
    if (player_battle_sprite[targetRole].direction == 1 ||
        (player_status[targetRole][2] + player_status[targetRole][1]) > 0)
      damageMult = 1;
    if (damageMult == -1) {
      player_battle_sprite[targetRole].direction = 3;
      battle_order_array[10 + targetRole]++;
    }
  }

  play_theurgy_anim(itemObj, effect_frame_count, (uint16_t)minFrames);
  effect_particle_count = 0;

  for (k = 0; k <= enemy_pos_count; k++) {
    targetRole = party[k].role;
    rng = VB_rtcRandomNext();
    baseDamage = vb_round_banker_i16_d((double)power + rng * 4.0);
    if (battle_order_array[k]) {
      targetIdx = get_player_attribute_total(targetRole, 19);
      baseDamage = (calc_battle_damage(baseDamage, targetIdx) / 2) + theurgy_data.baseDamage;
      baseDamage /= battle_order_array[10 + k];
      if (theurgy_data.elemental > 0 && theurgy_data.elemental < 7) {
        int16_t element = theurgy_data.elemental;
        if (element == 6)
          element = 0;
        theurgyIdx = get_player_attribute_total(targetRole, 22 + element);
        baseDamage = vb_round_banker_i16_d((double)baseDamage * ((double)(100 - theurgyIdx) / 100.0));
      }
      baseDamage = calc_magic_damage(baseDamage, theurgy_data.elemental);
      if (baseDamage > playerRoles(targetRole, 9))
        baseDamage = playerRoles(targetRole, 9);
      if (baseDamage < 0)
        baseDamage = 0;
      playerRoles(targetRole, 9) -= baseDamage;
      if (playerRoles(targetRole, 9) <= 0)
        play_sound(playerRoles(targetRole, 68), 1);
      add_damage_number(player_battle_sprite[k].x, player_battle_sprite[k].y - 70, baseDamage, 1);
    }
  }

  for (j = 0; j <= 5; j++) {
    for (k = 0; k <= enemy_pos_count; k++) {
      if (battle_order_array[k]) {
        if (battle_order_array[k + 10] < 4) {
          player_battle_sprite[k].direction = 4;
          if (j > 0) {
            player_battle_sprite[k].x += frameDelay;
            player_battle_sprite[k].y += frameDelay / 2;
          }
        }
        if (j < 3)
          enemy_battle_data_ext[k] = (uint16_t)-1;
      }
    }
    frameDelay /= 2;
    draw_battle_scene(1, 0);
  }

  enemy_battle_data[enemyIdx].direction = 0;
  enemy_battle_data[enemyIdx].x = enemy_battle_data[enemyIdx].origX;
  enemy_battle_data[enemyIdx].y = enemy_battle_data[enemyIdx].origY;
  draw_battle_scene(1, 0);
  for (k = 0; k <= enemy_pos_count; k++) {
    if (battle_order_array[k])
      player_battle_sprite[k].direction = player_battle_sprite[k].origDirection;
  }
  attack_done_flag = -1;
  (void)spriteIdx;
}

static void process_theurgy_gosub(int16_t itemObjID, int16_t *theurgyType) {
  int32_t i;
  for (i = 0; i <= enemy_pos_count; i++) {
    battle_role_data_ext[i][0] = playerRoles(party[i].role, 9);
    battle_role_data_ext[i][1] = playerRoles(party[i].role, 10);
  }
  process_Script(battle_role_action[battle_enemy_idx].target, &objects[itemObjID].data[2]);
  *theurgyType = 0;
  show_party_hp_mp_change(theurgyType);
  if (*theurgyType) {
    draw_battle_scene(7, 0);
    update_player_battle_status();
  }
}

int16_t process_Battle(int16_t battleScene, int16_t surpriseFlag) {
  int16_t battleResult;
  int32_t i, j, k, l, m, n;
  int16_t enemyObjID;
  int16_t itemObjID, roleID;
  int16_t directionParam, actionParam;
  int16_t dirResult, tmp2, tmp3;
  int16_t enemyTarget, prevRoleIdx, isTargeted;
  int16_t theurgyType = 0, theurgyResult, theurgyResult3, theurgyResult4, theurgyResult2;
  int16_t theurgyResult5, theurgyResult6, theurgyResult7 = 0, theurgyResult8;
  int16_t menuResult = 0, menuResult2;
  int16_t actionType, actionTypeSel;
  int16_t targetIdx;
  int16_t hasEffect, hasMagic;
  int16_t maxDmg;
  int16_t mpCost;
  double rng, rng2;
  int16_t floatVal;
  int16_t hpLost;
  int16_t equipID, magicSubfile2;
  int16_t equipObjID;
  int16_t menuParam1 = 0, menuParam2 = 0;

  battle_param = surpriseFlag;
  for (i = 0; i <= RPG_team_number; i++) {
    k = party[i].role;
    if (playerRoles(k, 9) <= 0)
      playerRoles(k, 9) = 1;
  }
  init_battle_state();
  curr_scene_id_cache = 0;
  flag_battling = -1;
  PAL_ClearTree();
  for (i = 0; i <= 11; i++)
    damage_numbers[i].timer = 0;
  for (i = 0; i <= 4; i++) {
    playerExp.primary[i].count = 0;
    playerExp.health[i].count = 0;
    playerExp.magic[i].count = 0;
    playerExp.attack[i].count = 0;
    playerExp.magicPower[i].count = 0;
    playerExp.defense[i].count = 0;
    playerExp.dexterity[i].count = 0;
    playerExp.flee[i].count = 0;
  }
  battleResult = 0;
  magic_select_idx = 0;
  fade_step_count = 0;
  dialog_y_pos = 200;
  effect_sub_count = 0;
  effect_particle_count = 0;
  npc_dir_frame = 0;
  battle_role_idx_2 = 0;
  battle_select_max = 0;
  battle_action_param = 0;
  enemy_max_id = 0;
  battle_max_hp_sum = 0;
  battle_action_type = 0;

  PAL_CopyMem(save_temp_buf, data_enemy_team + battleScene * 10, 10);
  for (i = 0; i <= 4; i++) {
    enemy_battle_data[i].hp = 0;
    enemyObjID = save_temp_buf[i];
    if (enemyObjID > 0)
      load_enemy_data(enemy_max_id, enemyObjID);
    if (enemyObjID >= 0)
      enemy_max_id++;
  }
  team_number = enemy_max_id - 1;
  if (enemy_max_id <= 0) {
    goto L_00420C0E;
  } else {

    load_enemy_sprites();
    load_battle_sprites();
    if (battle_extra_param <= 1) {
      player_battle_sprite[0].x = 240;
      player_battle_sprite[0].y = 170;
    }
    if (battle_extra_param == 2) {
      player_battle_sprite[0].x = 200;
      player_battle_sprite[0].y = 176;
      player_battle_sprite[1].x = 256;
      player_battle_sprite[1].y = 152;
    }
    if (battle_extra_param == 3) {
      player_battle_sprite[0].x = 180;
      player_battle_sprite[0].y = 180;
      player_battle_sprite[1].x = 234;
      player_battle_sprite[1].y = 170;
      player_battle_sprite[2].x = 270;
      player_battle_sprite[2].y = 146;
    }
    for (i = 0; i <= enemy_pos_count; i++) {
      player_battle_sprite[i].origX = player_battle_sprite[i].x;
      player_battle_sprite[i].origY = player_battle_sprite[i].y;
    }
    update_player_battle_status();
    if (RPG_battle_music_number > 0)
      play_all_kinds_music(RPG_battle_music_number, 1);
    PAL_CopyMem(&theurgy_effect_count, data_battlefield + RPG_battle_scene_number * 12, 2);
    load_fbp_subfile(RPG_battle_scene_number);
    PAL_CopyMem(bg_buf, global_buf_1, 64000);
    draw_enemy_battle_frame();
    for (i = 0; i <= 5; i++) {
      PAL_PopScreen6(global_buf_1, rng_anim_frames[i]);
      PAL_WaitTime(6);
    }
    fade_out_palette(0);
    PAL_CopyMem(global_buf_1, bg_buf, 64000);
    PAL_ClearMenu((uint8_t *)battle_enemy_data_ext, 5);

    for (;;) {
      PAL_CopyMem((uint8_t *)word_glyph_index, menu_bg_data, menu_bg_size);
      for (i = 0; i <= team_number; i++) {
        if (enemy_battle_data[i].hp > 0)
          process_Script(i, &enemy_battle_data[i].useScript);
      }
      if (fade_step_count) {
        battleResult = fade_step_count;
        goto L_00420C0E;
      }
      if (check_party_alive() == 0) {
        battleResult = 1;
        goto L_00420C0E;
      }
      if (count_alive_enemies() == 0) {
        battleResult = 3;
        goto L_00420C0E;
      }

      directionParam = 0;
      actionParam = 0;
      battle_curr_role_idx = 0;
      menuResult = 0;
      for (i = 0; i <= 255; i++)
        inventory[i].amountInUse = 0;
      PAL_ClearMenu((uint8_t *)battle_role_action, 40);

      if (auto_battle_flag == 0) {
        for (i = 0; i <= enemy_pos_count; i++)
          player_battle_sprite[i].actionState = 0;
        update_player_battle_status();
        draw_battle_status_bar();
      }

    L_0041DBEE:
    for (;;) {
      attack_done_flag = -1;
      effect_particle_count = 0;

      for (;;) {
        if (battle_curr_role_idx > enemy_pos_count)
          goto L_0041EAC0;
        if (player_status[battle_curr_role_idx][4]) {
          battle_role_action[battle_curr_role_idx].target = random_enemy_id();
          battle_role_action[battle_curr_role_idx].actionType = 0;
        } else if ((player_status[battle_curr_role_idx][1] + player_status[battle_curr_role_idx][2] +
                    player_status[battle_curr_role_idx][0]) == 0) {
          if (playerRoles(party[battle_curr_role_idx].role, 9) > 0)
            break;
        } else {
          draw_battle_scene(4, 0);
        }
        battle_curr_role_idx++;
      }
      for (i = battle_curr_role_idx; i <= enemy_pos_count; i++) {
        if (battle_role_action[i].actionType == 3 || battle_role_action[i].actionType == 4) {
          if (inventory[battle_role_action[i].invIndex].amountInUse > 0)
            inventory[battle_role_action[i].invIndex].amountInUse--;
          battle_role_action[i].actionType = 0;
        }
      }

      if (auto_battle_flag != 0)
        break;
      battle_target_cursor = -1;
      multi_event_param_3 = -1;
      for (;;) {
        battle_role_idx_2 = (uint16_t)-1;
        PAL_ClearMenu((uint8_t *)battle_order_array, 4);
        if (enemy_pos_count == 0 || player_battle_sprite[battle_curr_role_idx].direction != 0)
          battle_order_array[2] = (uint16_t)-1;
        if (player_status[battle_curr_role_idx][3])
          battle_order_array[1] = (uint16_t)-1;
        dirResult = select_direction_menu((uint16_t *)&directionParam);

        if (dirResult > -9)
          goto L_0041DECC;
        if (dirResult == -9) {
          menuResult = -1;
          for (i = battle_curr_role_idx; i <= enemy_pos_count; i++) {
            memcpy(&battle_role_action[i], &battle_role_data_copy[i], 10);
            if (battle_role_action[i].actionType == 10 && playerRoles(party[i].role, 4) == 0)
              battle_role_action[i].actionType = 0;
            if (battle_role_action[i].actionType == 0 && playerRoles(party[i].role, 4) != 0)
              battle_role_action[i].actionType = 10;
            if (battle_role_action[i].actionType == 1 || battle_role_action[i].actionType == 2) {
              itemObjID = battle_role_action[i].itemID;
              k = objects[itemObjID].data[0];
              copy_subfile_data(k);
              menuResult2 = find_magic_index(i, itemObjID);
              battle_role_action[i].invIndex = menuResult2 - 32;
              if (player_status[i][3] || menuResult2 == 0 || playerRoles(party[i].role, 10) < theurgy_data.costMP) {
                if (battle_role_action[i].actionType == 1)
                  battle_role_action[i].actionType = 5;
                else
                  battle_role_action[i].actionType = 0;
              }
            }
            if (battle_role_action[i].actionType == 3 || battle_role_action[i].actionType == 4) {
              menuResult2 = find_inventory_item(battle_role_action[i].itemID);
              if (menuResult2 >= 0) {
                battle_role_action[i].invIndex = menuResult2;
                if (inventory[menuResult2].amount <= inventory[menuResult2].amountInUse) {
                  if (battle_role_action[i].actionType == 3)
                    battle_role_action[i].actionType = 5;
                  else
                    battle_role_action[i].actionType = 0;
                  goto L_0041E7AC;
                }
                inventory[menuResult2].amountInUse++;
                goto L_0041E7AC;
              }
              if (battle_role_action[i].actionType == 3)
                battle_role_action[i].actionType = 5;
              else
                battle_role_action[i].actionType = 0;
            }
          L_0041E7AC:;
          }
          battle_curr_role_idx = enemy_pos_count;
        }
        theurgyResult2 = 0;
        if (dirResult == -10) {
          theurgyResult7 = set_auto_battle_targets();
        }
        if (dirResult == -11)
          battle_set_action_code();
        if (dirResult == -12) {
          theurgyResult2 = select_battle_action(0);
        }
        if (dirResult == -13) {
          theurgyResult2 = select_battle_action(1);
        }
        if (dirResult == -14)
          battle_set_action_walk();
        if (dirResult == -15) {
          entry_stub_show_text_and_dialog();
          goto L_0041DBEE;
        }
        if (dirResult == -16) {
          for (i = battle_curr_role_idx; i <= enemy_pos_count; i++) {
            k = party[i].role;
            battle_role_action[i].target = random_enemy_id();
            hasMagic = 0;
            maxDmg = 0;
            for (j = 32; j <= 63; j++) {
              equipObjID = playerRoles(k, j);
              if (equipObjID > 0) {
                magicSubfile2 = objects[equipObjID].data[0];
                copy_subfile_data(magicSubfile2);
                if (playerRoles(k, 10) >= theurgy_data.costMP && (theurgy_data.type == 9 || theurgy_data.type <= 3) &&
                    theurgy_data.costMP > 1) {
                  rng2 = VB_rtcRandomNext();
                  floatVal = theurgy_data.baseDamage + VB_Int(rng2 * 60.0);
                  if (floatVal > maxDmg) {
                    maxDmg = floatVal;
                    hasMagic = equipObjID;
                  }
                }
              }
            }
            if (hasMagic > 0 && player_status[i][3] <= 0) {
              battle_role_action[i].actionType = 2;
              battle_role_action[i].itemID = hasMagic;
            } else {
              battle_role_action[i].actionType = 0;
            }
          }
          battle_curr_role_idx = enemy_pos_count;
        }
        if (theurgyResult2 != -2)
          break;
      }
      check_battle_action(&directionParam, &actionParam);
      draw_battle_status_bar();
      goto L_0041EAAA;
      L_0041DECC:
        if (dirResult == -1) {
          prevRoleIdx = battle_curr_role_idx;
          for (i = 0; i < battle_curr_role_idx; i++) {
            if (check_player_alive(i) > 0)
              prevRoleIdx = i;
          }
          battle_curr_role_idx = prevRoleIdx;
          continue;
        } else {
          tmp2 = dirResult;
          switch (tmp2) {
          case 0:
            if (playerRoles(party[battle_curr_role_idx].role, 4)) {
              battle_role_action[battle_curr_role_idx].actionType = 10;
              check_battle_action(&directionParam, &actionParam);
            } else {
              enemyTarget = select_enemy_target();
              if (enemyTarget >= 0) {
                battle_role_action[battle_curr_role_idx].target = enemyTarget;
                battle_role_action[battle_curr_role_idx].actionType = 0;
                check_battle_action(&directionParam, &actionParam);
              }
            }
            break;
          case 1:
          L_0041DFF8:
            for (;;) {
              if (player_status[battle_curr_role_idx][3])
                theurgyType = 0;
              else
                theurgyType = 2;
              theurgyResult3 = select_theurgy(party[battle_curr_role_idx].role, &actionParam, theurgyType);
              draw_battle_status_bar();
              if (theurgyResult3 < 0)
                goto L_0041E404;
              PAL_GetBin((uint16_t *)&prevRoleIdx, objects[theurgyResult3].data[6], 3);
              PAL_GetBin((uint16_t *)&isTargeted, objects[theurgyResult3].data[6], 4);
              battle_role_action[battle_curr_role_idx].itemID = theurgyResult3;
              battle_role_action[battle_curr_role_idx].invIndex = actionParam;
              if (prevRoleIdx) {
                if (isTargeted) {
                  battle_role_action[battle_curr_role_idx].target = battle_curr_role_idx;
                  goto L_0041E134;
                }
                enemyTarget = select_enemy_target();
                if (enemyTarget >= 0) {
                  battle_role_action[battle_curr_role_idx].target = enemyTarget;
                  goto L_0041E134;
                }
                goto L_0041DFF8;
              } else if (isTargeted) {
                battle_role_action[battle_curr_role_idx].target = battle_curr_role_idx;
                break;
              } else {
                theurgyResult4 = select_battle_target();
                if (theurgyResult4 >= 0) {
                  battle_role_action[battle_curr_role_idx].target = theurgyResult4;
                  break;
                }
              }
            }
            battle_role_action[battle_curr_role_idx].actionType = 1;
          L_0041E1C0:
            check_battle_action(&directionParam, &actionParam);
            goto L_0041E404;
          case 2:
            PAL_GetBin((uint16_t *)&isTargeted, objects[player_battle_sprite[battle_curr_role_idx].equipID].data[6], 4);
            if (isTargeted == 0) {
              enemyTarget = select_enemy_target();
            } else
              enemyTarget = 0;
            if (enemyTarget >= 0) {
              for (i = 0; i <= enemy_pos_count; i++)
                battle_role_action[i].actionType = -1;
              battle_role_action[battle_curr_role_idx].actionType = 7;
              battle_role_action[battle_curr_role_idx].target = enemyTarget;
              battle_curr_role_idx = enemy_pos_count;
              check_battle_action(&directionParam, &actionParam);
            }
            break;
          case 3:
            for (;;) {
              theurgyResult2 = menu_loop(&menuParam1, 4, 16, 56, 3, 5);
              if (theurgyResult2 < 0) {
                draw_battle_status_bar();
                goto L_0041E404;
              }
              tmp3 = theurgyResult2;
              if (tmp3 == 0) {
                theurgyResult7 = set_auto_battle_targets();
              } else if (tmp3 == 1) {
                theurgyResult = menu_loop(&menuParam2, 24, 50, 23, 2, 2);
                if (theurgyResult < 0)
                  theurgyResult2 = -2;
                else
                  theurgyResult2 = select_battle_action(theurgyResult);
              } else if (tmp3 == 2) {
                battle_set_action_code();
              } else if (tmp3 == 3) {
                battle_set_action_walk();
              } else if (tmp3 == 4) {
                entry_stub_show_text_and_dialog();
              }
              if (theurgyResult2 != -2)
                break;
            }
            if (theurgyResult2 != 4) {
              check_battle_action(&directionParam, &actionParam);
              draw_battle_status_bar();
            }
            break;
          }
        L_0041E404:
          battle_role_idx_2 = 0;
        }
        goto L_0041EAAA;
      L_0041E134:
        battle_role_action[battle_curr_role_idx].actionType = 2;
        goto L_0041E1C0;
      L_0041EAAA:
        if (battle_curr_role_idx >= battle_extra_param)
          break;
    }
    L_0041EAC0:
      dialog_y_pos = 200;
      draw_battle_scene(1, 0);

      battle_action_queue = -1;
      for (battle_enemy_idx = 0; battle_enemy_idx <= team_number; battle_enemy_idx++) {
        float prod = VB_rtcRandomNext() * (float)enemy_runtime_data[battle_enemy_idx].dualMove;
        rng = (prod > 0) ? 1 : ((prod < 0) ? -1 : 0);
        rng2 = VB_rtcRandomNext();
        prevRoleIdx = vb_round_banker_i16_d(
            (double)(calc_level_bonus(battle_enemy_idx, 3) + enemy_runtime_data[battle_enemy_idx].dexterity) *
            (0.9 + ((double)rng2 / 5.0)));
        for (i = 0; (int16_t)i <= (int16_t)rng; i++) {
          battle_action_queue++;
          battle_action_queue_ext[battle_action_queue] = 10 + battle_enemy_idx;
          save_temp_buf[battle_action_queue] = prevRoleIdx;
          battle_sprite_ext[battle_action_queue] = i;
        }
      }
      for (battle_enemy_idx = 0; battle_enemy_idx <= enemy_pos_count; battle_enemy_idx++) {
        if (menuResult == 0)
          memcpy(&battle_role_data_copy[battle_enemy_idx], &battle_role_action[battle_enemy_idx], 10);
        actionType = battle_role_action[battle_enemy_idx].actionType;
        battle_action_queue++;
        battle_action_queue_ext[battle_action_queue] = battle_enemy_idx;
        rng2 = VB_rtcRandomNext();
        prevRoleIdx = vb_round_banker_i16_d((double)get_player_attribute_total(party[battle_enemy_idx].role, 20) *
                                            (0.9 + ((double)rng2 / 5.0)));
        if (actionType == 7)
          prevRoleIdx *= 10;
        if (actionType == 5)
          prevRoleIdx *= 5;
        if (actionType == 1 || actionType == 3)
          prevRoleIdx *= 3;
        if (actionType == 8)
          prevRoleIdx /= 2;
        if (player_status[battle_enemy_idx][7] > 0)
          prevRoleIdx *= 3;
        if (player_battle_sprite[battle_enemy_idx].direction == 1)
          prevRoleIdx /= 2;
        if (player_battle_sprite[battle_enemy_idx].direction == 2)
          prevRoleIdx = 0;
        save_temp_buf[battle_action_queue] = prevRoleIdx;
      }

      for (i = 0; i < battle_action_queue; i++) {
        for (j = i + 1; j <= battle_action_queue; j++) {
          if (save_temp_buf[i] < save_temp_buf[j]) {
            swap_values(&save_temp_buf[i], &save_temp_buf[j]);
            swap_values((int16_t *)&battle_action_queue_ext[i], (int16_t *)&battle_action_queue_ext[j]);
            swap_values((int16_t *)&battle_sprite_ext[i], (int16_t *)&battle_sprite_ext[j]);
          }
        }
      }

      for (l = 0; l <= battle_action_queue; l++) {
        npc_sprite_num = 0;
        battle_sub_flag = 0;

        if (battle_action_queue_ext[l] >= 10) {
          if (battle_select_max == 0) {
            battle_enemy_idx = battle_action_queue_ext[l] - 10;
            if (!(enemy_status[battle_enemy_idx][2] || enemy_status[battle_enemy_idx][1])) {
              if (l > 0)
                battle_sub_flag = (battle_action_queue_ext[l] == battle_action_queue_ext[l - 1]);
              if (enemy_battle_data[battle_enemy_idx].hp > 0) {
                theurgyResult8 = random_alive_party_member();
                enemy_attack_role(battle_enemy_idx, theurgyResult8, battle_sprite_ext[l]);
              }
              goto L_00420322;
            }
          }
        } else {
          battle_enemy_idx = battle_action_queue_ext[l];
          if ((int16_t)battle_enemy_idx >= 0) {
            roleID = party[battle_enemy_idx].role;
            if (playerRoles(roleID, 9) > 0 || (int16_t)player_status[battle_enemy_idx][4] > 0) {
              if ((player_status[battle_enemy_idx][2] <= 0) && (player_status[battle_enemy_idx][1] <= 0)) {

                if (auto_battle_flag >= 2) {
                  battle_role_action[battle_enemy_idx].target = theurgyResult7;
                  if (auto_battle_flag == 2)
                    battle_role_action[battle_enemy_idx].actionType = 0;
                  if (auto_battle_flag == 9) {
                    battle_role_action[battle_enemy_idx].actionType = 2;
                    rng2 = VB_rtcRandomNext();
                    battle_role_action[battle_enemy_idx].itemID = playerRoles(roleID, 32 + VB_Int(rng2 * 4.0));
                  }
                }
                if (player_status[battle_enemy_idx][0] > 0)
                  battle_role_action[battle_enemy_idx].actionType = 9;

                targetIdx = battle_role_action[battle_enemy_idx].target;
                if (targetIdx >= 0) {
                  while (enemy_battle_data[targetIdx].hp <= 0)
                    targetIdx = (targetIdx + 1) % enemy_max_id;
                } else {
                  targetIdx = 0;
                }
                magic_select_idx = 0;
                in_battle_action = 0;
                battle_enemy_hp = 0;
                theurgyResult5 = count_alive_enemies();
                for (i = 0; i <= 4; i++)
                  battle_enemy_data_ext[i] = enemy_battle_data[i].hp;

                actionTypeSel = battle_role_action[battle_enemy_idx].actionType;
                switch (actionTypeSel) {
                case 0:
                  calc_player_attack_damage(battle_enemy_idx, roleID, targetIdx);
                  break;
                case 1: {
                  itemObjID = battle_role_action[battle_enemy_idx].itemID;
                  k = objects[itemObjID].data[0];
                  copy_subfile_data(k);
                  theurgyResult6 = get_player_attribute_total(roleID, 18);
                  hasEffect = (theurgy_data.type == 8) ? 0 : 1;
                  player_attack_anim(battle_enemy_idx, hasEffect);
                  process_Script(battle_enemy_idx, &objects[itemObjID].data[3]);
                  if (redraw_hp_mp_flag) {
                    if (theurgy_data.type == 8) {
                      play_sound(theurgy_data.sound, 0);
                      animate_battle_sprites(battle_enemy_idx, battle_enemy_idx);
                    }
                    if ((int16_t)theurgy_data.effect >= 0)
                      calc_display_theurgy(battle_enemy_idx, battle_role_action[battle_enemy_idx].target, itemObjID,
                                           theurgyResult6);
                    playerRoles(roleID, 10) -= theurgy_data.costMP;
                    process_theurgy_gosub(itemObjID, &theurgyType);
                    if (theurgy_data.type == 8) {
                      player_battle_sprite[battle_enemy_idx].x = player_battle_sprite[battle_enemy_idx].origX;
                      player_battle_sprite[battle_enemy_idx].y = player_battle_sprite[battle_enemy_idx].origY;
                      load_battle_sprites();
                      npc_sprite_num = -1;
                    }
                    rng2 = VB_rtcRandomNext();
                    playerExp.magic[roleID].count = vb_round_banker_i16_d((double)playerExp.magic[roleID].count + rng2 * 2.0);
                    playerExp.magicPower[roleID].count++;
                  }
                  break;
                }
                case 2: {
                  itemObjID = battle_role_action[battle_enemy_idx].itemID;
                  k = objects[itemObjID].data[0];
                  copy_subfile_data(k);
                  mpCost = theurgy_data.costMP;
                  hasEffect = (theurgy_data.type == 9) ? 0 : 1;
                  player_attack_anim(battle_enemy_idx, hasEffect);
                  process_Script(battle_enemy_idx, &objects[itemObjID].data[3]);
                  if (redraw_hp_mp_flag) {
                    actionParam = battle_enemy_idx;
                    npc_dir_frame = 0;
                    theurgyResult6 = get_player_attribute_total(roleID, 18);
                    if (theurgy_data.type == 9) {
                      npc_dir_frame = -1;
                      cast_theurgy_anim(itemObjID);
                      reset_battle_sprite_pos(battle_enemy_idx);
                      actionParam = 9;
                    }
                    calc_display_theurgy(actionParam, targetIdx, itemObjID, theurgyResult6);
                    process_Script(targetIdx, &objects[itemObjID].data[2]);
                    apply_enemy_poison_damage();
                    show_enemy_damage(&theurgyType);
                    if (theurgyType)
                      enemy_hit_reaction(5);
                    playerRoles(roleID, 10) -= mpCost;
                    rng2 = VB_rtcRandomNext();
                    playerExp.magic[roleID].count = vb_round_banker_i16_d((double)playerExp.magic[roleID].count + rng2 * 2.0);
                    playerExp.magicPower[roleID].count++;
                  }
                  break;
                }
                case 3: {
                  player_attack_anim(battle_enemy_idx, 0);
                  itemObjID = battle_role_action[battle_enemy_idx].itemID;
                  draw_text_at(200, 50, itemObjID, 14);
                  play_sound(28, 0);
                  int16_t targetStart, targetEnd;
                  if (battle_role_action[battle_enemy_idx].target == (uint16_t)-1) {
                    targetStart = 0;
                    targetEnd = enemy_pos_count;
                    battle_role_action[battle_enemy_idx].target = battle_enemy_idx;
                  } else {
                    targetStart = battle_role_action[battle_enemy_idx].target;
                    targetEnd = battle_role_action[battle_enemy_idx].target;
                  }
                  for (m = 2; m <= 16; m++) {
                    for (n = targetStart; n <= targetEnd; n++)
                      get_sprite_frame_data(n, 10 - VB_Abs(m - 10));
                    PAL_WaitTime(5);
                  }
                  process_theurgy_gosub(itemObjID, &theurgyType);
                  PAL_GetBin((uint16_t *)&prevRoleIdx, objects[itemObjID].data[6], 3);
                  if (prevRoleIdx)
                    remove_inventory_item(itemObjID, 1);
                  break;
                }
                case 4: {
                  itemObjID = battle_role_action[battle_enemy_idx].itemID;
                  player_attack_anim(battle_enemy_idx, 0);
                  draw_text_at(200, 50, itemObjID, 14);
                  PAL_Delay(35);
                  player_battle_sprite[battle_enemy_idx].direction = 6;
                  for (i = 0; i <= 4; i++) {
                    if (party[i].role < 5) {
                      battle_role_data_ext[i][0] = playerRoles(party[i].role, 9);
                      battle_role_data_ext[i][1] = playerRoles(party[i].role, 10);
                    }
                  }
                  play_sound(27, 0);
                  process_Script(targetIdx, &objects[itemObjID].data[4]);
                  if (redraw_hp_mp_flag) {
                    PAL_GetBin((uint16_t *)&prevRoleIdx, objects[itemObjID].data[6], 3);
                    if (prevRoleIdx)
                      remove_inventory_item(itemObjID, 1);
                  }
                  theurgyType = 0;
                  show_party_hp_mp_change(&theurgyType);
                  show_enemy_damage(&theurgyType);
                  if (theurgyType) {
                    draw_battle_scene(3, 0);
                    draw_battle_scene(5, 0);
                  }
                  for (i = 0; i <= team_number; i++) {
                    if (battle_enemy_data_ext[i] > 0 && enemy_battle_data[i].hp <= 0) {
                      npc_sprite_num = -1;
                      battle_enemy_hp = enemy_runtime_data[i].deathSound;
                    }
                  }
                  break;
                }
                case 5:
                  player_battle_sprite[battle_enemy_idx].direction = 3;
                  player_battle_sprite[battle_enemy_idx].actionState = 3;
                  draw_battle_scene(4, 0);
                  playerExp.defense[roleID].count += 2;
                  break;
                case 6:
                  calc_player_attack_damage(battle_enemy_idx, roleID, targetIdx);
                  auto_battle_flag = 2;
                  break;
                case 7: {
                  equipID = player_battle_sprite[battle_enemy_idx].equipID;
                  magicSubfile2 = objects[equipID].data[0];
                  copy_subfile_data(magicSubfile2);
                  theurgyResult6 = 0;
                  for (i = 0; i <= enemy_pos_count; i++) {
                    k = party[i].role;
                    if (check_player_alive(i) > 1 && playerRoles(k, 9) >= theurgy_data.costMP) {
                      playerRoles(k, 9) -= theurgy_data.costMP;
                      theurgyResult6 += playerRoles(k, 17) + playerRoles(k, 18);
                    }
                  }
                  if (theurgy_data.type < 9) {
                    if (enemy_pos_count >= 2) {
                      battle_sprite_data[0] = 274;
                      battle_sprite_data[1] = 244;
                      battle_sprite_data[2] = 216;
                      battle_sprite_data_ext[0] = 186;
                      battle_sprite_data_ext[1] = 172;
                      battle_sprite_data_ext[2] = 158;
                    } else {
                      battle_sprite_data[0] = 244;
                      battle_sprite_data[1] = 216;
                      battle_sprite_data_ext[0] = 172;
                      battle_sprite_data_ext[1] = 158;
                    }
                    swap_values((int16_t *)&battle_sprite_data[enemy_pos_count],
                                (int16_t *)&battle_sprite_data[battle_enemy_idx]);
                    swap_values((int16_t *)&battle_sprite_data_ext[enemy_pos_count],
                                (int16_t *)&battle_sprite_data_ext[battle_enemy_idx]);
                    play_sound(29, 0);
                    adjust_battle_sprite_pos();
                    magic_select_idx = (uint16_t)-1;
                  }
                  rng = 0;
                  for (i = 0; i <= enemy_pos_count; i++) {
                    rng += (check_player_alive(i) > 1);
                    battle_order_array[i] = i;
                  }
                  swap_values((int16_t *)&battle_order_array[enemy_pos_count], (int16_t *)&battle_order_array[battle_enemy_idx]);
                  if (rng > 1) {
                    if (theurgy_data.type < 9) {
                      for (k = 0; k <= enemy_pos_count; k++) {
                        i = battle_order_array[k];
                        if (check_player_alive(i) > 1) {
                          if (i != battle_enemy_idx) {
                            player_battle_sprite[i].direction = 5;
                            draw_battle_scene(3, 0);
                            player_battle_sprite[i].direction = 6;
                            draw_battle_scene(1, 0);
                          } else {
                            enemy_battle_data_ext[i] = (uint16_t)-1;
                            player_battle_sprite[i].direction = 5;
                            draw_battle_scene(1, 8);
                          }
                        }
                      }
                    } else {
                      player_attack_anim(battle_enemy_idx, 0);
                    }
                    load_theurgy_image(magicSubfile2);
                    if (theurgy_data.type < 9 && player_battle_sprite[battle_enemy_idx].direction == 5)
                      player_battle_sprite[battle_enemy_idx].direction = 6;
                    actionParam = 3;
                    npc_dir_frame = 0;
                    if (theurgy_data.type == 9) {
                      npc_dir_frame = -1;
                      cast_theurgy_anim(equipID);
                      actionParam = 9;
                    }
                    theurgyResult6 /= 4;
                    calc_display_theurgy(actionParam, targetIdx, equipID, theurgyResult6);
                    apply_enemy_poison_damage();
                    show_enemy_damage(&theurgyType);
                    if (theurgyType)
                      enemy_hit_reaction(5);
                  } else {
                    draw_battle_scene(2, 0);
                    play_sound(30, 0);
                    for (i = 0; i <= enemy_pos_count; i++)
                      if (player_battle_sprite[i].direction == 0)
                        player_battle_sprite[i].direction = 5;
                    draw_battle_scene(7, 0);
                  }
                  for (i = 0; i <= battle_action_queue; i++)
                    if (battle_action_queue_ext[i] < 10)
                      battle_action_queue_ext[i] = -1;
                  break;
                }
                case 8: {
                  prevRoleIdx = 0;
                  for (i = 0; i <= team_number; i++) {
                    hasEffect = calc_level_bonus(i, 4) + enemy_runtime_data[i].dexterity;
                    if (enemy_battle_data[i].hp > 0)
                      prevRoleIdx += hasEffect;
                  }
                  rng2 = VB_rtcRandomNext();
                  hasEffect = ((float)get_player_attribute_total(party[battle_enemy_idx].role, 21) >=
                               (float)((double)prevRoleIdx * rng2));
                  if (surpriseFlag == 0)
                    hasEffect = 0;
                  flee_from_battle(battle_enemy_idx, &hasEffect);
                  break;
                }
                case 9:
                  menuResult2 = random_alive_party_member();
                  if (menuResult2 != battle_enemy_idx)
                    player_attack_player(battle_enemy_idx, menuResult2);
                  break;
                case 10:
                  player_attack_execute(battle_enemy_idx, roleID);
                  break;
                default:
                  break;
                }

                if (count_alive_enemies() < theurgyResult5) {
                  npc_sprite_num = -1;
                  play_sound(battle_enemy_hp, 0);
                }
              }
            }
          }
        }
      L_00420322:
        for (i = 0; i <= 4; i++)
          battle_enemy_data_ext[i] = enemy_battle_data[i].hp;
        if (npc_sprite_num) {
          play_rng_effect(1, 65);
          npc_sprite_num = 0;
          for (i = 0; i <= 11; i++)
            damage_numbers[i].timer = 0;
        }
        if (npc_dir_frame) {
          load_battle_sprites();
          npc_dir_frame = 0;
          play_theurgy_rng_anim();
        }
        if (count_alive_enemies() > 0 && magic_select_idx) {
          for (i = 0; i <= enemy_pos_count; i++) {
            if (player_battle_sprite[i].direction >= 5) {
              player_battle_sprite[i].direction = 0;
              battle_sprite_data[i] = player_battle_sprite[i].origX;
              battle_sprite_data_ext[i] = player_battle_sprite[i].origY;
            }
          }
          adjust_battle_sprite_pos();
          magic_select_idx = 0;
        }
        update_player_battle_status();
        if (check_party_alive() == 0) {
          battleResult = 1;
          goto L_00420C0E;
        }
        if (count_alive_enemies() <= 0) {
          battleResult = 3;
          goto L_00420C0E;
        }
        if (battle_action_param) {
          battleResult = battle_action_param;
          goto L_00420C0E;
        }
      }

      theurgyType = 2;
      for (i = 0; i <= team_number; i++) {
        for (j = 0; j <= 15; j++)
          if (enemy_status[i][j] > 0)
            enemy_status[i][j]--;
        battle_enemy_data_ext[i] = enemy_battle_data[i].hp;
        if (enemy_battle_data[i].hp > 0) {
          for (j = 0; j <= 15; j++) {
            if (enemy_poison_status[i][j].poisonID > 0)
              process_Script(i, &enemy_poison_status[i][j].poisonScript);
          }
          hpLost = battle_enemy_data_ext[i] - enemy_battle_data[i].hp;
          if (hpLost > 0) {
            add_damage_number(enemy_battle_data[i].x, enemy_battle_data[i].y - 110, hpLost, 1);
            theurgyType = 10;
          }
          if (enemy_battle_data[i].hp <= 0)
            npc_sprite_num = -1;
        }
      }
      for (i = 0; i <= enemy_pos_count; i++) {
        for (j = 0; j <= 15; j++)
          if (player_status[i][j] > 0)
            player_status[i][j]--;
        k = party[i].role;
        damage_target_hp = playerRoles(k, 9);
        battle_select_idx = playerRoles(k, 10);
        if (damage_target_hp > 0) {
          player_status[i][4] = 0;
          for (j = 0; j <= 15; j++) {
            if (poison_status[i][j].poisonID > 0)
              process_Script(i, &poison_status[i][j].poisonScript);
          }
          hpLost = damage_target_hp - playerRoles(k, 9);
          if (hpLost > 0) {
            add_damage_number(player_battle_sprite[i].x, player_battle_sprite[i].y - 70, hpLost, 1);
            theurgyType = 10;
          }
          hpLost = battle_select_idx - playerRoles(k, 10);
          if (hpLost > 0) {
            add_damage_number(player_battle_sprite[i].x, player_battle_sprite[i].y - 62, hpLost, 2);
            theurgyType = 10;
          }
        }
        battle_role_data_ext[i][0] = playerRoles(k, 9);
        battle_role_data_ext[i][1] = playerRoles(k, 10);
        playerRoles(k, 9) = damage_target_hp;
        playerRoles(k, 10) = battle_select_idx;
      }
      draw_battle_scene(theurgyType, 0);
      for (i = 0; i <= 4; i++)
        battle_enemy_data_ext[i] = enemy_battle_data[i].hp;
      for (i = 0; i <= enemy_pos_count; i++) {
        k = party[i].role;
        playerRoles(k, 9) = battle_role_data_ext[i][0];
        playerRoles(k, 10) = battle_role_data_ext[i][1];
      }
      if (battle_select_max > 0) {
        battle_select_max--;
        if (battle_select_max == 0)
          npc_sprite_num = 1;
      }
      if (npc_sprite_num) {
        play_rng_effect(1, 65);
        npc_sprite_num = 0;
      }
      PAL_ReadKey(key_state_array);
      if (auto_battle_flag <= 2) {
        if (key_state_array[1] > 0 || key_state_array[82] > 0)
          auto_battle_flag = 0;
      }
      DoEvents_check_exit();
    }
  }

L_00420C0E:
  if (battleResult == 1) {
    update_player_battle_status();
    draw_battle_scene(1, 0);
  }
  if (battleResult == 3) {
    if (battle_action_type > 0 || battle_max_hp_sum > 0) {
      hasEffect = surpriseFlag ? 3 : 2;
      play_all_kinds_music(hasEffect, 0);
      show_money(battle_action_type);
      calc_display_exp(battle_max_hp_sum);
    }
    for (i = 0; i <= team_number; i++)
      process_Script(i, &enemy_battle_data[i].battleStartScript);
    for (i = 0; i <= RPG_team_number; i++) {
      k = party[i].role;
      for (j = 0; j <= 19; j++) {
        if (data_levelup_magic[j].m[k].magic > 0) {
          if (data_levelup_magic[j].m[k].level <= playerRoles(k, 6))
            add_magic_to_player(k, data_levelup_magic[j].m[k].magic, (uint16_t)-1);
        }
      }
      playerRoles(k, 9) += (playerRoles(k, 7) - playerRoles(k, 9)) / 2;
      playerRoles(k, 10) += (playerRoles(k, 8) - playerRoles(k, 10)) / 2;
    }
  }
  PAL_ClearMenu((uint8_t *)enemy_battle_data, 75);
  PAL_ClearMenu((uint8_t *)&player_battle_sprite[0].spriteNum, 120);
  PAL_ClearMenu((uint8_t *)enemy_poison_status, 160);
  PAL_ClearMenu((uint8_t *)poison_status, 160);
  PAL_ClearMenu((uint8_t *)player_status, 80);
  PAL_ClearMenu((uint8_t *)enemy_status, 80);
  auto_battle_flag = 0;
  redraw_flag |= 3;
  PAL_ClearTree();
  PAL_FlushDSound();
  return battleResult;
}

void process_scripts(int16_t eventObjID, uint16_t *scriptEntry, int16_t opcode, int16_t *operand1, int16_t *operand2,
                     int16_t *operand3) {
  int16_t roleID = 0;
  int16_t i, j;
  int32_t k;
  int16_t eventObjIdx, eventIdx2;
  int16_t tmp = 0;
  int16_t stepSize, var3;
  int16_t targetX, targetY, tmp2, tmp3, npcX, npcY, tmp4, tmp5;
  int16_t rankDist;
  int16_t loopStart, loopEnd;
  int16_t totalChange, oldVal, oldMP;
  int16_t defenseVal;
  double rng;
  int16_t walkSpeed;
  int16_t walkDir;
  int16_t var1, attrIdx, var4, var2;
  int16_t cashAmount = 0;
  int16_t eventOffset;
  int16_t itemObjID;
  int16_t savedHP, savedUseScript, savedEndScript, savedDir;
  int16_t destX, destY;

  DoEvents_check_exit();
  if (opcode <= 10)
    return;
  if (eventObjID <= 4)
    roleID = party[eventObjID].role;

  switch (opcode) {
  case 11:
    npc_display_data[eventObjID].direction = 0;
    npc_walk_one_step(eventObjID, 2);
    return;
  case 12:
    npc_display_data[eventObjID].direction = 1;
    npc_walk_one_step(eventObjID, 2);
    return;
  case 13:
    npc_display_data[eventObjID].direction = 2;
    npc_walk_one_step(eventObjID, 2);
    return;
  case 14:
    npc_display_data[eventObjID].direction = 3;
    npc_walk_one_step(eventObjID, 2);
    return;
  case 15:
    if ((*operand1) >= 0)
      npc_display_data[eventObjID].direction = (*operand1);
    if ((*operand2) >= 0)
      npc_display_data[eventObjID].currentFrame = (*operand2);
    return;
  case 16:
    stepSize = 3;
  L_walk_to_pos:
    targetX = (((*operand1) + (*operand1)) + (*operand3)) * 16;
    targetY = (((*operand2) + (*operand2)) + (*operand3)) * 8;
    tmp2 = targetX - npc_display_data[eventObjID].x;
    tmp3 = targetY - npc_display_data[eventObjID].y;
    if ((VB_Abs(tmp2) >= (stepSize + stepSize)) || (VB_Abs(tmp3) >= stepSize)) {
      PAL_ExTF((uint16_t *)&npc_display_data[eventObjID].direction, tmp2, tmp3);
      npc_walk_one_step(eventObjID, stepSize);
    } else {
      npc_display_data[eventObjID].x = targetX;
      npc_display_data[eventObjID].y = targetY;
    }
    if (npc_display_data[eventObjID].x != targetX || npc_display_data[eventObjID].y != targetY) {
      *scriptEntry = (uint16_t)(*scriptEntry - 1);
    } else {
      npc_display_data[eventObjID].currentFrame = 0;
    }
    return;
  case 17:
    if ((eventObjID & 1) == mutex_shaking) {
      stepSize = 2;
      goto L_walk_to_pos;
    } else {
      *scriptEntry = (uint16_t)(*scriptEntry - 1);
      return;
    }
  case 18:
    if ((*operand1) <= 0)
      eventObjIdx = eventObjID;
    else
      eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
      npc_display_data[eventObjIdx].x = party_abs_x + (*operand2);
      npc_display_data[eventObjIdx].y = party_abs_y + (*operand3);
    }
    return;
  case 19:
    if ((*operand1) <= 0)
      eventObjIdx = eventObjID;
    else
      eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
      npc_display_data[eventObjIdx].x = (*operand2);
      npc_display_data[eventObjIdx].y = (*operand3);
    } else {
      save_temp_buf[0] = (*operand2);
      save_temp_buf[1] = (*operand3);
      PAL_CopyMem(events + ((*operand1) - 1) * 32 + 2, save_temp_buf, 4);
    }
    return;
  case 20:
    npc_display_data[eventObjID].currentFrame = (*operand1);
    npc_display_data[eventObjID].direction = 0;
    return;
  case 21:
    RPG_team_direction = (*operand1);
    party[(*operand3)].frame = (RPG_team_direction * 3) + (*operand2);
    return;
  case 22:
    if ((*operand1) == 0) {
      return;
    } else {
      if ((*operand1) < 0)
        eventObjIdx = eventObjID;
      else
        eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
      if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
        npc_display_data[eventObjIdx].direction = (*operand2);
        npc_display_data[eventObjIdx].currentFrame = (*operand3);
      } else {
        save_temp_buf[0] = (*operand2);
        save_temp_buf[1] = (*operand3);
        PAL_CopyMem(events + ((*operand1) - 1) * 32 + 20, save_temp_buf, 4);
      }
      return;
    }
  case 23:
    equipment_effect(roleID, (*operand1), (*operand2)) = (*operand3);
    return;
  case 24:
    for (i = 17; i <= 30; i++)
      equipment_effect(roleID, (*operand1), i) = 0;
    coop_magic_tmp = playerRoles(roleID, (*operand1));
    playerRoles(roleID, (*operand1)) = (*operand2);
    return;
  case 25:
    if ((*operand3) > 0)
      roleID = (*operand3) - 1;
    playerRoles(roleID, (*operand1)) += (*operand2);
    return;
  case 26:
    if ((*operand3) <= 0) {
      attrIdx = (*operand1);
      if (attrIdx == 1) {
        player_battle_sprite[eventObjID].spriteNum = (*operand2);
      } else if (attrIdx == 65) {
        player_battle_sprite[eventObjID].equipID = (*operand2);
      } else {
        playerRoles(roleID, (*operand1)) = (*operand2);
      }
      return;
    }
    playerRoles((*operand3) - 1, (*operand1)) = (*operand2);
    return;
  case 27:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = RPG_team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    totalChange = 0;
    for (j = loopStart; j <= (uint16_t)loopEnd; j++) {
      i = party[j].role;
      if (playerRoles(i, 9) > 0) {
        oldVal = playerRoles(i, 9);
        playerRoles(i, 9) += (int16_t)(*operand2);
        if ((int16_t)playerRoles(i, 9) < 0)
          playerRoles(i, 9) = 0;
        if (playerRoles(i, 9) > playerRoles(i, 7))
          playerRoles(i, 9) = playerRoles(i, 7);
        totalChange += VB_Abs((int16_t)playerRoles(i, 9) - oldVal);
      }
    }
    redraw_hp_mp_flag = (totalChange != 0) ? (uint16_t)-1 : 0;
    return;
  case 28:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = RPG_team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    totalChange = 0;
    for (j = loopStart; j <= (uint16_t)loopEnd; j++) {
      i = party[j].role;
      if (playerRoles(i, 9) > 0) {
        oldVal = playerRoles(i, 10);
        playerRoles(i, 10) += (int16_t)(*operand2);
        if ((int16_t)playerRoles(i, 10) < 0)
          playerRoles(i, 10) = 0;
        if (playerRoles(i, 10) > playerRoles(i, 8))
          playerRoles(i, 10) = playerRoles(i, 8);
        totalChange += VB_Abs((int16_t)playerRoles(i, 10) - oldVal);
      }
    }
    redraw_hp_mp_flag = (totalChange != 0) ? (uint16_t)-1 : 0;
    return;
  case 29:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = RPG_team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    totalChange = 0;
    for (j = loopStart; j <= (uint16_t)loopEnd; j++) {
      i = party[j].role;
      if (playerRoles(i, 9) > 0) {
        oldVal = playerRoles(i, 9);
        oldMP = playerRoles(i, 10);
        playerRoles(i, 9) += (int16_t)(*operand2);
        if ((int16_t)playerRoles(i, 9) < 0)
          playerRoles(i, 9) = 0;
        if (playerRoles(i, 9) > playerRoles(i, 7))
          playerRoles(i, 9) = playerRoles(i, 7);
        playerRoles(i, 10) += (int16_t)(*operand2);
        if ((int16_t)playerRoles(i, 10) < 0)
          playerRoles(i, 10) = 0;
        if (playerRoles(i, 10) > playerRoles(i, 8))
          playerRoles(i, 10) = playerRoles(i, 8);
        totalChange += VB_Abs((int16_t)playerRoles(i, 9) - oldVal) + VB_Abs((int16_t)playerRoles(i, 10) - oldMP);
      }
    }
    redraw_hp_mp_flag = (totalChange != 0) ? (uint16_t)-1 : 0;
    return;
  case 30:
    if ((int16_t)(*operand1) < 0 && ((int32_t)RPG_money + (int16_t)(*operand1)) < 0) {
      *scriptEntry = (uint16_t)((*operand2) - 1);
    } else {
      RPG_money += (int16_t)(*operand1);
    }
    return;
  case 31:
    compact_inventory();
    if ((int16_t)(*operand2) <= 0)
      (*operand2) = 1;
    add_inventory_item((*operand1), (*operand2));
    return;
  case 32:
    if ((*operand2) == 0)
      (*operand2) = 1;
    if (count_item_total((*operand1)) < (*operand2) && (*operand3) != 0) {
      *scriptEntry = (uint16_t)((*operand3) - 1);
    } else {
      remove_inventory_item((*operand1), (*operand2));
    }
    return;
  case 33:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    for (j = loopStart; j <= (uint16_t)loopEnd; j++)
      enemy_battle_data[j].hp -= (*operand2);
    return;
  case 34:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = RPG_team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    if ((*operand2) > 10)
      (*operand2) = 10;
    totalChange = 0;
    for (j = loopStart; j <= (uint16_t)loopEnd; j++) {
      i = party[j].role;
      oldVal = playerRoles(i, 9);
      if (playerRoles(i, 9) <= 0) {
        var1 = (playerRoles(i, 7) / 10) * (*operand2);
        if (var1 <= 0)
          var1 = 1;
        playerRoles(i, 9) = var1;
        for (k = 0; k <= 15; k++) {
          poison_status[j][k].poisonID = 0;
          if (player_status[j][k] < 999)
            player_status[j][k] = 0;
        }
      }
      totalChange += VB_Abs((int16_t)playerRoles(i, 9) - oldVal);
    }
    redraw_hp_mp_flag = (totalChange != 0) ? (uint16_t)-1 : 0;
    return;
  case 35:
    if ((*operand2) <= 0) {
      loopStart = 11;
      loopEnd = 16;
    } else {
      loopStart = 10 + (*operand2);
      loopEnd = loopStart;
    }
    for (i = loopStart; i <= (uint16_t)loopEnd; i++) {
      if (playerRoles((*operand1), i) > 0) {
        add_inventory_item(playerRoles((*operand1), i), 1);
        playerRoles((*operand1), i) = 0;
      }
    }
    return;
  case 36:
    if ((*operand1) == 0) {
      return;
    } else {
      if ((*operand1) < 0)
        eventObjIdx = eventObjID;
      else
        eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
      if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
        npc_display_data[eventObjIdx].autoScript = (*operand2);
      } else {
        PAL_CopyMem(events + ((*operand1) - 1) * 32 + 10, operand2, 2);
      }
      return;
    }
  case 37:
    if ((*operand1) == 0) {
      return;
    } else {
      if ((*operand1) < 0)
        eventObjIdx = eventObjID;
      else
        eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
      if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
        npc_display_data[eventObjIdx].triggerScript = (*operand2);
      } else {
        PAL_CopyMem(events + ((*operand1) - 1) * 32 + 8, operand2, 2);
      }
      return;
    }
  case 38:
    buy_item_menu((*operand1));
    return;
  case 39:
    sell_item_menu();
    return;
  case 40:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    for (j = loopStart; j <= (uint16_t)loopEnd; j++) {
      for (i = 0; i <= 15; i++)
        if (enemy_poison_status[j][i].poisonID == (*operand2))
          goto L_op40_done;
      rng = VB_rtcRandomNext();
      if ((float)objects[enemy_battle_data[j].objectID].data[1] < VB_Int(rng * 10.0)) {
        for (i = 0; i <= 15; i++) {
          if (enemy_poison_status[j][i].poisonID == 0) {
            enemy_poison_status[j][i].poisonID = (*operand2);
            enemy_poison_status[j][i].poisonScript = objects[(*operand2)].data[4];
            process_Script(j, &enemy_poison_status[j][i].poisonScript);
            break;
          }
        }
      }
    L_op40_done:;
    }
    return;
  case 41:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = RPG_team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    for (j = loopStart; j <= (uint16_t)loopEnd; j++) {
      defenseVal = get_player_attribute_total(party[j].role, 22);
      if ((float)(VB_rtcRandomNext() * 100.0) > (float)defenseVal) {
        for (i = 0; i <= 15; i++)
          if (poison_status[j][i].poisonID == (*operand2))
            goto L_op41_done;
        for (i = 0; i <= 15; i++) {
          if (poison_status[j][i].poisonID == 0) {
            poison_status[j][i].poisonID = (*operand2);
            poison_status[j][i].poisonScript = objects[(*operand2)].data[2];
            process_Script(j, &poison_status[j][i].poisonScript);
            break;
          }
        }
      }
    L_op41_done:;
    }
    return;
  case 42:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    for (j = loopStart; j <= (uint16_t)loopEnd; j++)
      for (i = 0; i <= 15; i++)
        if (enemy_poison_status[j][i].poisonID == (*operand2))
          enemy_poison_status[j][i].poisonID = 0;
    return;
  case 43:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = RPG_team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    for (j = loopStart; j <= (uint16_t)loopEnd; j++)
      for (i = 0; i <= 15; i++)
        if (poison_status[j][i].poisonID == (*operand2))
          poison_status[j][i].poisonID = 0;
    return;
  case 44:
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = RPG_team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    for (j = loopStart; j <= (uint16_t)loopEnd; j++)
      for (i = 0; i <= 15; i++)
        if (poison_status[j][i].poisonID > 0 && objects[poison_status[j][i].poisonID].data[0] <= (*operand2))
          poison_status[j][i].poisonID = 0;
    return;
  case 45:
    if ((*operand1) == 4) {
      if (playerRoles(roleID, 9) <= 0) {
        if (player_status[eventObjID][(*operand1)] < (*operand2))
          player_status[eventObjID][(*operand1)] = (*operand2);
        player_battle_sprite[eventObjID].direction = 1;
        draw_battle_scene(4, 0);
        player_battle_sprite[eventObjID].direction = 0;
      } else {
        redraw_hp_mp_flag = 0;
      }
      return;
    } else if (((*operand1) > 4) || (player_status[eventObjID][(*operand1)] <= 0)) {
      if (player_status[eventObjID][(*operand1)] < (*operand2))
        player_status[eventObjID][(*operand1)] = (*operand2);
    }
    return;
  case 46:
    rng = VB_rtcRandomNext();
    if ((float)objects[enemy_battle_data[eventObjID].objectID].data[1] < VB_Int(rng * 10.0)) {
      if ((*operand2) > 0)
        enemy_status[eventObjID][(*operand1)] = (*operand2);
    } else if ((*operand3) != 0) {
      *scriptEntry = (uint16_t)((*operand3) - 1);
    }
    return;
  case 47:
    player_status[eventObjID][(*operand1)] = 0;
    return;
  case 48:
    if ((*operand3) != 0)
      roleID = (*operand3) - 1;
    equipment_effect(roleID, 17, (*operand1)) =
        (uint16_t)vb_round_banker_i16_d((double)playerRoles(roleID, (*operand1)) * ((double)(*operand2) / 100.0));
    return;
  case 49:
    player_battle_sprite[eventObjID].spriteNum = (*operand1);
    return;
  case 50:
    if (flag_battling) {
      *scriptEntry = (uint16_t)((*operand1) - 1);
    } else {
      *scriptEntry = (uint16_t)((*operand2) - 1);
    }
    return;
  case 51:
    if (enemy_runtime_data[eventObjID].collectValue > 0) {
      RPG_current_calabash_number =
          VB_Abs((int16_t)(RPG_current_calabash_number + enemy_runtime_data[eventObjID].collectValue));
    } else {
      *scriptEntry = (uint16_t)((*operand1) - 1);
    }
    return;
  case 52:
    if (RPG_current_calabash_number > 0) {
      enemy_runtime_data[eventObjID].exp = 0;
      rng = VB_rtcRandomNext();
      var1 = VB_Int(VB_CSng(RPG_current_calabash_number) * rng);
      if (var1 > 8)
        var1 = 8;
      RPG_current_calabash_number -= var1 + 1;
      var1 = data_store[0][var1];
      add_inventory_item(var1, 1);
      frame_menu(88, 32, 8, (uint16_t)-1);
      draw_string(98, 42, 42, 3, 0);
      draw_string(156, 42, var1, 3, 23);
      read_ball_mkf_index(objects[var1].data[0]);
      PAL_PutP(128, 73, (const uint8_t *)(global_buf_2 + global_buf_2[70]), (void *)screen_buffer_ptr, 0, 0);
      PAL_PutP(135, 80, (const uint8_t *)(global_buf_2 + global_buf_2[0]), (void *)screen_buffer_ptr, 0, 0);
      wait_frame(100);
    } else {
      *scriptEntry = (uint16_t)((*operand1) - 1);
    }
    return;
  case 53:
    shake_intensity = (*operand1);
    shake_duration = (*operand2);
    if (shake_duration == 0)
      shake_duration = 4;
    return;
  case 54:
    read_rng_subfile((*operand1));
    redraw_flag |= 16;
    return;
  case 55:
    if ((*operand2) <= 0)
      (*operand2) = 999;
    if ((*operand3) <= 0)
      (*operand3) = 10;
    walk_party_fastest((*operand1), (*operand2), (*operand3));
    return;
  case 56:
    if (flag_battling || (scenes[RPG_curr_scene].scriptOnLeave == 0)) {
      redraw_hp_mp_flag = 0;
      *scriptEntry = (uint16_t)((*operand1) - 1);
    } else {
      process_Script(0, &scenes[RPG_curr_scene].scriptOnLeave);
    }
    return;
  case 57:
    if (in_battle_action) {
      playerRoles(roleID, 9) -= (*operand1);
      if ((int16_t)playerRoles(roleID, 9) < 0)
        playerRoles(roleID, 9) = 0;
      enemy_battle_data[battle_enemy_idx].hp += (*operand1);
    } else {
      roleID = party[battle_enemy_idx].role;
      enemy_battle_data[eventObjID].hp -= (*operand1);
      playerRoles(roleID, 9) += (*operand1);
      if (playerRoles(roleID, 9) > playerRoles(roleID, 7))
        playerRoles(roleID, 9) = playerRoles(roleID, 7);
    }
    return;
  case 58:
    if (battle_param) {
      flee_from_battle(eventObjID, &(int16_t){-1});
    } else if ((*operand1))
      *scriptEntry = (uint16_t)((*operand1) - 1);
    return;
  case 59:
    dialog_type = 0;
    dialog_width = 80;
    dialog_height = 40;
    if ((*operand1) > 0)
      dialog_x = (*operand1);
    return;
  case 60:
    dialog_type = 1;
    dialog_text_x = 12;
    dialog_text_y = 8;
    dialog_width = 44;
    dialog_height = 26;
    if ((*operand1) > 0) {
      if ((*operand3)) {
        mutex_can_change_palette = (*operand3);
        push_screen_buffer();
      }
      show_face(48, 55, (*operand1));
      dialog_text_x = 80;
      dialog_width = 96;
    }
    if ((*operand2) > 0)
      dialog_x = (*operand2);
    return;
  case 61:
    dialog_type = 2;
    dialog_text_x = 12;
    dialog_text_y = 108;
    dialog_width = 44;
    dialog_height = 126;
    if ((*operand1) > 0) {
      if ((*operand3)) {
        mutex_can_change_palette = (*operand3);
        push_screen_buffer();
      }
      show_face(270, 144, (*operand1));
      dialog_text_x = 4;
      dialog_width = 20;
    }
    if ((*operand2) > 0)
      dialog_x = (*operand2);
    return;
  case 62:
    dialog_type = 10;
    dialog_width = 152;
    dialog_height = 32;
    flag_parallel_mutex = (*operand1);
    return;
  case 63:
    stepSize = 2;
    goto L_walk_to_pos_68;
  case 64:
    if ((*operand1) == 0) {
      return;
    } else {
      if ((*operand1) < 0)
        eventObjIdx = eventObjID;
      else
        eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
      if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
        npc_display_data[eventObjIdx].triggerMode = (*operand2);
      } else {
        PAL_CopyMem(events + ((*operand1) - 1) * 32 + 14, operand2, 2);
      }
      return;
    }
  case 65:
    redraw_hp_mp_flag = 0;
    return;
  case 66:
  L_004234FA: {
    int16_t enemyIdx = eventObjID;
    if ((*operand3) > 0 && (*operand3) <= 5)
      enemyIdx = (*operand3) - 1;
    if ((*operand3) < 0)
      enemyIdx = VB_Int(VB_rtcRandomNext() * (float)enemy_max_id);
    while (enemy_battle_data[enemyIdx].hp <= 0) {
      enemyIdx = (enemyIdx + 1) % enemy_max_id;
    }
    itemObjID = objects[(*operand1)].data[0];
    load_theurgy_image(itemObjID);
    calc_display_theurgy(8, enemyIdx, (*operand1), (*operand2));
    apply_enemy_poison_damage();
  }
    return;
  case 67:
    if ((*operand2) <= 1)
      (*operand2) ^= 1;
    if ((*operand1) != RPG_music_number || (*operand1) == 0)
      play_all_kinds_music((*operand1), (*operand2));
    RPG_music_number = (*operand1);
    return;
  case 68:
    stepSize = 4;
  L_walk_to_pos_68:
    tmp = stepSize + stepSize;
    targetX = (((*operand1) + (*operand1)) + (*operand3)) * 16;
    targetY = (((*operand2) + (*operand2)) + (*operand3)) * 8;
    for (;;) {
      tmp2 = targetX - party_abs_x;
      tmp3 = targetY - party_abs_y;
      if ((tmp2 | tmp3) == 0)
        return;
      PAL_ExTF((uint16_t *)&walkDir, tmp2, tmp3);
      if (tmp2 != 0)
        tmp2 = key_pressed_flags[walkDir] * tmp;
      if (tmp3 != 0)
        tmp3 = key_direction_flags[walkDir] * stepSize;
      x_off = party_abs_x;
      y_off = party_abs_y;
      party_abs_x += tmp2;
      party_abs_y += tmp3;
      viewport_x_bak = RPG_viewport_x;
      viewport_y_bak = RPG_viewport_y;
      RPG_viewport_x = party_abs_x - team_abstract_x;
      RPG_viewport_y = party_abs_y - team_abstract_y;
      npc_display_data[eventObjID].direction = walkDir;
      npc_display_data[eventObjID].x += tmp2;
      npc_display_data[eventObjID].y += tmp3;
      update_trail_data();
      process_event_objects(0);
      update_viewport_scroll();
      check_in_battle(1);
    }
  case 69:
    RPG_battle_music_number = (*operand1);
    return;
  case 70:
    party_abs_x = (((*operand1) + (*operand1)) + (*operand3)) * 16;
    party_abs_y = (((*operand2) + (*operand2)) + (*operand3)) * 8;
    x_off = party_abs_x;
    y_off = party_abs_y;
    RPG_viewport_x = party_abs_x - team_abstract_x;
    RPG_viewport_y = party_abs_y - team_abstract_y;
    PAL_Ffxy(&RPG_viewport_x, &RPG_viewport_y, battle_viewport_x, battle_viewport_y);
    targetX = team_abstract_x;
    targetY = team_abstract_y;
    for (j = 0; j <= 4; j++) {
      party[j].x = targetX;
      party[j].y = targetY;
      party[j].frame = party[0].frame;
      trail[j].x = targetX + RPG_viewport_x;
      trail[j].y = targetY + RPG_viewport_y;
      trail[j].direction = RPG_team_direction;
      targetX -= fh_M_MSG_global[RPG_team_direction];
      targetY -= key_repeat_delay[RPG_team_direction];
    }
    if (!flag_battling)
      draw_battle_row_sprite();
    return;
  case 71:
    play_sound((*operand1), 0);
    return;
  case 72:
    return;
  case 73:
    if ((*operand1) == 0) {
      return;
    } else {
      if ((*operand1) < 0)
        eventObjIdx = eventObjID;
      else
        eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
      if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
        npc_display_data[eventObjIdx].state = (*operand2);
      } else {
        PAL_CopyMem(events + ((*operand1) - 1) * 32 + 12, operand2, 2);
      }
      return;
    }
  case 74:
    RPG_battle_scene_number = (*operand1);
    return;
  case 75:
    if ((*operand1) == 0)
      (*operand1) = 10;
    npc_display_data[eventObjID].vanishTime = -(int16_t)(*operand1);
    return;
  case 76:
    stepSize = 0;
    if ((*operand1) == 0)
      (*operand1) = 8;
    if ((*operand2) == 0)
      (*operand2) = 4;
    if (RPG_ememy_chase_rate) {
      targetX = npc_display_data[eventObjID].x;
      targetY = npc_display_data[eventObjID].y;
      tmp2 = party_abs_x - targetX;
      tmp3 = party_abs_y - targetY;
      if (VB_Abs(tmp2) + VB_Abs(tmp3) * 2 < (int16_t)(((*operand1) * 32) * RPG_ememy_chase_rate)) {
        if (tmp2 == 0)
          tmp2 = (int16_t)key_direction_flags[vb_round_banker_u16(VB_rtcRandomNext())];
        if (tmp3 == 0)
          tmp3 = (int16_t)key_direction_flags[vb_round_banker_u16(VB_rtcRandomNext())];
        walkDir = npc_display_data[eventObjID].direction;
        PAL_ExTF((uint16_t *)&walkDir, tmp2, tmp3);
        j = targetX;
        i = targetY;
        PAL_ExRij((uint16_t *)&var3, (uint16_t *)&j, (uint16_t *)&i);
        npcX = (int16_t)(((j + j) + var3) * 16);
        npcY = (int16_t)(((i + i) + var3) * 8);
        tmp4 = npcX + fh_M_MSG_global[walkDir & 3];
        tmp5 = npcY + key_repeat_delay[walkDir & 3];
        if ((*operand3)) {
          npc_display_data[eventObjID].direction = (uint16_t)walkDir;
          stepSize = (*operand2);
        } else if (load_map_data(tmp4, tmp5, eventObjID)) {
          npc_display_data[eventObjID].direction = (uint16_t)walkDir;
          stepSize = (*operand2);
        } else {
          npc_display_data[eventObjID].x = (uint16_t)npcX;
          npc_display_data[eventObjID].y = (uint16_t)npcY;
        }
        if (!(*operand3)) {
          for (k = 0; k <= 3; k++) {
            targetX = npc_display_data[eventObjID].x + (int16_t)key_pressed_flags[k] * 4;
            targetY = npc_display_data[eventObjID].y + (int16_t)key_direction_flags[k] * 2;
            PAL_ExGm1((uint16_t)targetX, (uint16_t)targetY, (const uint8_t *)map_data_buf, (uint16_t *)&var1);
            if (var1 == 0) {
              npc_display_data[eventObjID].x = (uint16_t)npcX;
              npc_display_data[eventObjID].y = (uint16_t)npcY;
            }
          }
        }
      }
    } else {
      npc_display_data[eventObjID].direction = (npc_display_data[eventObjID].direction + mutex_shaking) & 3;
    }
    npc_walk_one_step(eventObjID, (uint16_t)stepSize);
    if (!(*operand3) && stepSize == 0) {
      PAL_ExGm1(npc_display_data[eventObjID].x, npc_display_data[eventObjID].y, (const uint8_t *)map_data_buf,
                (uint16_t *)&var1);
      if (var1 == 0) {
        k = (uint16_t)VB_Int(VB_rtcRandomNext() * 4.0);
        targetX = npc_display_data[eventObjID].x + fh_M_MSG_global[k];
        targetY = npc_display_data[eventObjID].y + key_repeat_delay[k];
        PAL_ExGm1((uint16_t)targetX, (uint16_t)targetY, (const uint8_t *)map_data_buf, (uint16_t *)&var1);
        if (var1) {
          npc_display_data[eventObjID].x = (uint16_t)targetX;
          npc_display_data[eventObjID].y = (uint16_t)targetY;
        }
      }
    }
    return;
  case 77:
    var1 = read_key();
    return;
  case 78:
    redraw_flag |= 32;
    RPG_curr_scene = 0;
    return;
  case 79:
    if ((*operand1) <= 0) {
      fade_palette_to(1);
      PAL_Rblk(0, 0, 319, 199, 16);
      PAL_IntPalate((uint8_t *)&palette_data[RPG_color_begin_ptr]);
    } else {
      PAL_FuPalate((uint16_t)((*operand1) - 1), (uint8_t *)&palette_data[768],
                   (const uint8_t *)&palette_data[RPG_color_begin_ptr]);
      PAL_CopyMem(&palette_data[RPG_color_begin_ptr], &palette_data[768], 768);
      if (palette_fade_active == 0)
        PAL_IntPalate((uint8_t *)&palette_data[RPG_color_begin_ptr]);
    }
    return;
  case 80:
    var1 = (*operand1);
    if (var1 == 0)
      var1 = 1;
    fade_in(var1);
    return;
  case 81:
    var1 = (*operand1);
    if (var1 == 0)
      var1 = 1;
    fade_out_palette(var1);
    return;
  case 82:
    if ((*operand1) <= 0)
      (*operand1) = 800;
    npc_display_data[eventObjID].state = -npc_display_data[eventObjID].state;
    npc_display_data[eventObjID].vanishTime = (*operand1);
    return;
  case 83:
    RPG_color_begin_ptr = 0;
    return;
  case 84:
    RPG_color_begin_ptr = 384;
    return;
  case 85:
    if ((*operand2) > 0)
      roleID = (*operand2) - 1;
    add_magic_to_player(roleID, (*operand1), 0);
    return;
  case 86:
    if ((*operand2) > 0)
      roleID = (*operand2) - 1;
    for (j = 32; j <= 63; j++)
      if (playerRoles(roleID, j) == (*operand1))
        playerRoles(roleID, j) = 0;
    return;
  case 87:
    if ((*operand2) == 0)
      (*operand2) = 8;
    var1 = objects[(*operand1)].data[0];
    copy_subfile_data(var1);
    theurgy_data.baseDamage = playerRoles(roleID, 10) * (*operand2);
    PAL_CopyMem(data_magic + var1 * 32, &theurgy_data.effect, 32);
    playerRoles(roleID, 10) = 1;
    return;
  case 88:
    if ((*operand2) <= 0)
      (*operand2) = 1;
    if (count_item_total((*operand1)) < (*operand2)) {
      *scriptEntry = (uint16_t)((*operand3) - 1);
    }
    return;
  case 89:
    if ((*operand1) > 0 && (*operand1) != RPG_curr_scene) {
      redraw_flag |= 12;
      scene_to_load = (*operand1);
      RPG_role_locate_layer = 0;
    }
    return;
  case 90:
    playerRoles(roleID, 9) /= 2;
    return;
  case 91:
    totalChange = enemy_battle_data[eventObjID].hp / 2;
    if ((*operand1) != 0 && totalChange > (int16_t)(*operand1))
      totalChange = (*operand1);
    enemy_battle_data[eventObjID].hp -= totalChange;
    return;
  case 92:
    if ((*operand1) == 0)
      (*operand1) = 1;
    battle_select_max = (*operand1);
    npc_sprite_num = -1;
    return;
  case 93:
    var2 = -1;
    for (j = 0; j <= 15; j++)
      if (poison_status[eventObjID][j].poisonID == (*operand1))
        var2 = 0;
    if (var2) {
      *scriptEntry = (uint16_t)((*operand2) - 1);
    }
    return;
  case 94:
    var2 = -1;
    for (j = 0; j <= 15; j++)
      if (enemy_poison_status[eventObjID][j].poisonID == (*operand1))
        var2 = 0;
    if (var2) {
      *scriptEntry = (uint16_t)((*operand2) - 1);
    }
    return;
  case 95:
    playerRoles(roleID, 9) = 0;
    return;
  case 96:
    enemy_battle_data[eventObjID].hp = -1;
    return;
  case 97:
    var2 = -1;
    for (j = 0; j <= 15; j++)
      if (poison_status[eventObjID][j].poisonID > 0)
        var2 = 0;
    if (var2) {
      *scriptEntry = (uint16_t)((*operand1) - 1);
    }
    return;
  case 98:
    RPG_change_chaserate_times = (*operand1);
    RPG_ememy_chase_rate = 0;
    return;
  case 99:
    RPG_change_chaserate_times = (*operand1);
    RPG_ememy_chase_rate = 3;
    return;
  case 100:
    if ((*operand1) == 0)
      (*operand1) = 1;
    totalChange = enemy_battle_data[eventObjID].hp;
    if (totalChange <= 0)
      totalChange = 1;
    if ((float)enemy_runtime_data[eventObjID].health / (float)totalChange <= 100.0f / (float)(*operand1)) {
      *scriptEntry = (uint16_t)((*operand2) - 1);
    }
    return;
  case 101:
    playerRoles((*operand1), 2) = (*operand2);
    if (!flag_battling && (*operand3) != 0)
      load_team_mgo();
    return;
  case 102:
    rng = VB_rtcRandomNext();
    (*operand2) = vb_round_banker_i16((float)(int16_t)((*operand2) * 5) +
                                      (float)playerRoles(party[battle_enemy_idx].role, 17) * (float)VB_Int(rng * 4.0));
    goto L_004234FA;
  case 103:
    enemy_runtime_data[eventObjID].magic = (*operand1);
    if ((*operand2) == 0)
      (*operand2) = 10;
    enemy_runtime_data[eventObjID].magicRate = (*operand2);
    return;
  case 104:
    if (in_battle_action) {
      *scriptEntry = (uint16_t)((*operand1) - 1);
    }
    return;
  case 105:
    play_sound(45, 1);
    if ((*operand1)) {
      loopStart = 0;
      loopEnd = team_number;
    } else {
      loopStart = eventObjID;
      loopEnd = eventObjID;
    }
    for (k = 0; k <= 11; k++) {
      for (j = loopStart; j <= (uint16_t)loopEnd; j++)
        enemy_battle_data[j].x -= 10 + k;
      draw_battle_scene(1, 0);
    }
    for (j = loopStart; j <= (uint16_t)loopEnd; j++) {
      if (enemy_battle_data[j].hp > 0) {
        battle_max_hp_sum -= enemy_runtime_data[j].exp;
        battle_action_type -= enemy_runtime_data[j].cash;
        enemy_battle_data[j].hp = 0;
      }
    }
    return;
  case 106:
    loopStart = battle_role_action[battle_enemy_idx].target;
    while (enemy_battle_data[loopStart].hp <= 0)
      loopStart = (loopStart + 1) % enemy_max_id;
    rankDist = (loopStart - battle_enemy_idx) * 8;
    player_battle_sprite[battle_enemy_idx].direction = 10;
    player_battle_sprite[battle_enemy_idx].x = enemy_battle_data[loopStart].x + 64 - rankDist;
    player_battle_sprite[battle_enemy_idx].y = enemy_battle_data[loopStart].y + 22 + rankDist;
    draw_battle_scene(1, 0);
    for (j = 0; j <= 4; j++) {
      player_battle_sprite[battle_enemy_idx].x -= 8 + j;
      player_battle_sprite[battle_enemy_idx].y -= 4;
      player_hit_flags[loopStart] = (j == 4) ? (uint16_t)-1 : 0;
      draw_battle_scene(1, 0);
    }
    player_battle_sprite[battle_enemy_idx].x--;
    draw_battle_scene(3, 0);
    reset_battle_sprite_pos(battle_enemy_idx);
    draw_battle_scene(1, 0);
    if ((*operand1) == 0)
      (*operand1) = 10;
    rng = VB_rtcRandomNext();
    if (enemy_runtime_data[loopStart].stealItemCount > 0 && (float)(*operand1) >= VB_Int(rng * 10.0)) {
      if (enemy_runtime_data[loopStart].stealItem == 0) {
        rng = VB_rtcRandomNext();
        cashAmount = enemy_runtime_data[loopStart].stealItemCount / vb_round_banker_i16_d(2.0 + rng);
        if (cashAmount > 0) {
          enemy_runtime_data[loopStart].stealItemCount -= cashAmount;
          RPG_money += cashAmount;
          frame_menu(88, 32, 8, (uint16_t)-1);
          draw_string(100, 42, 34, 3, 23);
          show_small_number(148, 46, cashAmount, 5);
          draw_string(188, 42, 10, 3, 23);
        }
      } else {
        enemy_runtime_data[loopStart].stealItemCount--;
        var1 = enemy_runtime_data[loopStart].stealItem;
        add_inventory_item(var1, 1);
        frame_menu(88, 32, 8, (uint16_t)-1);
        draw_string(98, 42, 34, 3, 0);
        draw_string(156, 42, var1, 3, 23);
      }
      wait_frame(125);
    }
    return;
  case 107:
    blow_away_flag = (*operand1);
    return;
  case 108:
    if ((*operand1) <= 0)
      eventObjIdx = eventObjID;
    else
      eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
      npc_display_data[eventObjIdx].x += (*operand2);
      npc_display_data[eventObjIdx].y += (*operand3);
      npc_walk_one_step(eventObjIdx, 0);
    } else {
      eventOffset = ((*operand1) * 32) - 32;
      PAL_CopyMem(&npc_display_data[0].vanishTime, events + eventOffset, 32);
      npc_display_data[0].x += (*operand2);
      npc_display_data[0].y += (*operand3);
      npc_walk_one_step(0, 0);
      PAL_CopyMem(events + eventOffset, &npc_display_data[0].vanishTime, 32);
    }
    return;
  case 109:
    if ((*operand1) > 0) {
      if ((*operand2) != 0)
        scenes[(*operand1)].scriptOnEnter = (*operand2);
      if ((*operand3) != 0)
        scenes[(*operand1)].scriptOnLeave = (*operand3);
      if ((*operand2) == 0 && (*operand3) == 0) {
        scenes[(*operand1)].scriptOnEnter = 0;
        scenes[(*operand1)].scriptOnLeave = 0;
      }
    }
    return;
  case 110:
    x_off = party_abs_x;
    y_off = party_abs_y;
    viewport_x_bak = RPG_viewport_x;
    viewport_y_bak = RPG_viewport_y;
    RPG_viewport_x += (*operand1);
    RPG_viewport_y += (*operand2);
    RPG_role_locate_layer = (*operand3) * 8;
    if ((*operand1) || (*operand2)) {
      update_party_position();
      update_viewport_scroll();
    }
    return;
  case 111:
    eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    // NOTE: golden 无范围检查（VB 运行时下标检查兜底，非法脚本直接崩）；remake
    // 按 npc_display_data 合法域 0..160 防御性跳过（0 号 scratch 槽保持可用）。
    if (eventObjIdx >= 0 && eventObjIdx <= 160 && npc_display_data[eventObjIdx].state == (*operand2))
      npc_display_data[eventObjID].state = (*operand2);
    return;
  case 112:
    walkSpeed = 2;
  L_walk_party:
    do {
      destX = (((*operand1) + (*operand1)) + (*operand3)) * 16;
      destY = (((*operand2) + (*operand2)) + (*operand3)) * 8;
      x_off = party_abs_x;
      y_off = party_abs_y;
      tmp2 = destX - party_abs_x;
      tmp3 = destY - party_abs_y;
      if ((tmp2 | tmp3) == 0)
        break;
      PAL_ExTF((uint16_t *)&RPG_team_direction, tmp2, tmp3);
      viewport_x_bak = RPG_viewport_x;
      viewport_y_bak = RPG_viewport_y;
      RPG_viewport_x += (key_pressed_flags[RPG_team_direction] * walkSpeed) * 2;
      RPG_viewport_y += key_direction_flags[RPG_team_direction] * walkSpeed;
      update_party_position();
      process_event_objects(0);
      update_viewport_scroll();
      check_in_battle(1);
    } while (destX != party_abs_x || destY != party_abs_y);
    for (j = 0; j <= RPG_team_number; j++)
      party[j].frame = trail[j].direction * 3;
    return;
  case 113:
    RPG_screen_wave_grade = (*operand1);
    wave_progression = (*operand2);
    return;
  case 114:
    if ((*operand1) <= 0)
      eventObjIdx = eventObjID;
    else
      eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
      npc_display_data[eventObjIdx].spriteNum = (*operand2);
      if ((*operand3))
        load_npc_sprites();
    } else {
      PAL_CopyMem(events + ((*operand1) - 1) * 32 + 16, operand2, 2);
    }
    return;
  case 115:
    if ((*operand1) <= 0)
      (*operand1) = 1;
    play_rng_effect((*operand1), (*operand2));
    return;
  case 116:
    var2 = 0;
    for (j = 0; j <= RPG_team_number; j++) {
      if (playerRoles(j, 9) < playerRoles(j, 7) || playerRoles(j, 10) < playerRoles(j, 8))
        var2 = -1;
    }
    if (var2) {
      *scriptEntry = (uint16_t)((*operand1) - 1);
    }
    return;
  case 117:
    RPG_team_number = 0;
    if ((*operand1) <= 0)
      (*operand1) = 1;
    party[0].role = (*operand1) - 1;
    if ((*operand2) > 0) {
      RPG_team_number = 1;
      party[1].role = (*operand2) - 1;
    }
    if ((*operand3) > 0) {
      RPG_team_number = 2;
      party[2].role = (*operand3) - 1;
    }
    load_team_mgo();
    init_battle_state();
    set_team_draw();
    return;
  case 118:
    show_fbp_picture((*operand1), (*operand2));
    return;
  case 119:
    if ((*operand1) <= 0)
      (*operand1) = 1;
    if ((*operand2) == 0)
      cd_stop();
    midi_close();
    if (!flag_battling)
      RPG_music_number = 0;
    return;
  case 120:
    flag_battling = 0;
    scene_transition();
    return;
  case 121:
    for (j = 0; j <= RPG_team_number; j++) {
      if (playerRoles(party[j].role, 3) == (*operand1)) {
        *scriptEntry = (uint16_t)((*operand2) - 1);
      }
    }
    return;
  case 122:
    walkSpeed = 4;
    goto L_walk_party;
  case 123:
    walkSpeed = 8;
    goto L_walk_party;
  case 124:
    if (mutex_shaking) {
      stepSize = 4;
      goto L_walk_to_pos;
    } else {
      *scriptEntry = (uint16_t)(*scriptEntry - 1);
      return;
    }
  case 125:
    if ((*operand1) <= 0)
      eventObjIdx = eventObjID;
    else
      eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
      npc_display_data[eventObjIdx].x += (*operand2);
      npc_display_data[eventObjIdx].y += (*operand3);
    } else {
      eventOffset = ((*operand1) * 32) - 32;
      PAL_CopyMem(&npc_display_data[0].vanishTime, events + eventOffset, 32);
      npc_display_data[0].x += (*operand2);
      npc_display_data[0].y += (*operand3);
      PAL_CopyMem(events + eventOffset, &npc_display_data[0].vanishTime, 32);
    }
    return;
  case 126:
    if ((*operand1) <= 0)
      eventObjIdx = eventObjID;
    else
      eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
      npc_display_data[eventObjIdx].layer = (*operand2);
    } else {
      eventOffset = ((*operand1) * 32) - 32;
      PAL_CopyMem(&npc_display_data[0].vanishTime, events + eventOffset, 32);
      npc_display_data[0].layer = (*operand2);
      PAL_CopyMem(events + eventOffset, &npc_display_data[0].vanishTime, 32);
    }
    return;
  case 127:
    if ((*operand3) == -1 && (*operand1) == 0 && (*operand2) == 0) {
      team_abstract_x = 160;
      team_abstract_y = 112;
      RPG_viewport_x = party_abs_x - team_abstract_x;
      RPG_viewport_y = party_abs_y - team_abstract_y;
      var3 = 0;
    } else {
      var3 = (*operand3);
      if (var3 <= 0)
        var3 = 1;
    }
    for (j = 1; j <= (uint16_t)var3; j++) {
      viewport_x_bak = RPG_viewport_x;
      viewport_y_bak = RPG_viewport_y;
      int16_t oldTeamX = team_abstract_x, oldTeamY = team_abstract_y;
      if (((*operand1) | (*operand2) | (*operand3)) == 0) {
        team_abstract_x = 160;
        team_abstract_y = 112;
        RPG_viewport_x = party_abs_x - team_abstract_x;
        RPG_viewport_y = party_abs_y - team_abstract_y;
        draw_battle_row_sprite();
        (*operand3) = (uint16_t)-1;
      } else {
        if ((int16_t)(*operand3) < 0) {
          RPG_viewport_x = ((*operand1) * 32) - 160;
          RPG_viewport_y = ((*operand2) * 16) - 112;
          draw_battle_row_sprite();
        } else {
          RPG_viewport_x += (*operand1);
          RPG_viewport_y += (*operand2);
        }
        team_abstract_x = party_abs_x - RPG_viewport_x;
        team_abstract_y = party_abs_y - RPG_viewport_y;
      }
      party[0].x = team_abstract_x;
      party[0].y = team_abstract_y;
      for (i = 1; i <= RPG_team_number; i++) {
        party[i].x += (team_abstract_x - oldTeamX);
        party[i].y += (team_abstract_y - oldTeamY);
      }
      process_event_objects(0);
      if ((int16_t)(*operand3) >= 0)
        update_viewport_scroll();
      check_in_battle(1);
    }
    return;
  case 128:
    var1 = 384 - RPG_color_begin_ptr;
    PAL_CopyMem(&palette_data[768], &palette_data[RPG_color_begin_ptr], 768);
    for (j = 1; j <= 32; j++) {
      PAL_CvPalate((uint8_t *)&palette_data[768], (const uint8_t *)&palette_data[var1]);
      PAL_IntPalate((uint8_t *)&palette_data[768]);
      if ((*operand1) <= 0) {
        process_event_objects(0);
        check_in_battle(1);
      } else
        PAL_WaitTime((*operand1));
    }
    RPG_color_begin_ptr = var1;
    PAL_IntPalate((uint8_t *)&palette_data[RPG_color_begin_ptr]);
    palette_fade_active = 0;
    return;
  case 129:
    redraw_hp_mp_flag = 0;
    stepSize = ((*operand2) * 32) + 16;
    eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
      if (npc_display_data[eventObjIdx].state > 0 &&
          (VB_Abs((int16_t)(npc_display_data[eventObjIdx].x - (party_abs_x + fh_M_MSG_global[RPG_team_direction]))) +
           VB_Abs((int16_t)(npc_display_data[eventObjIdx].y - (party_abs_y + key_repeat_delay[RPG_team_direction]))) *
               2) < stepSize) {
        if ((*operand2) > 0)
          npc_display_data[eventObjIdx].triggerMode = 5 + (*operand2);
        redraw_hp_mp_flag = -1;
      }
    }
    if (redraw_hp_mp_flag == 0) {
      *scriptEntry = (uint16_t)((*operand3) - 1);
    }
    return;
  case 130:
    stepSize = 8;
    goto L_walk_to_pos;
  case 131:
    redraw_hp_mp_flag = 0;
    stepSize = ((*operand2) * 32) + 16;
    eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
      if (npc_display_data[eventObjIdx].state > 0 &&
          (VB_Abs((int16_t)(npc_display_data[eventObjID].x - npc_display_data[eventObjIdx].x)) +
           VB_Abs((int16_t)(npc_display_data[eventObjID].y - npc_display_data[eventObjIdx].y)) * 2) < stepSize)
        redraw_hp_mp_flag = -1;
    }
    if (redraw_hp_mp_flag == 0) {
      *scriptEntry = (uint16_t)((*operand3) - 1);
    }
    return;
  case 132:
    redraw_hp_mp_flag = 0;
    eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
    if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
      targetX = party_abs_x + fh_M_MSG_global[RPG_team_direction];
      targetY = party_abs_y + key_repeat_delay[RPG_team_direction];
      var1 = -1;
      if ((*operand3) != 0)
        PAL_ExGm1(targetX, targetY, (const uint8_t *)map_data_buf, (uint16_t *)&var1);
      if (var1) {
        npc_display_data[eventObjIdx].x = targetX;
        npc_display_data[eventObjIdx].y = targetY;
        npc_display_data[eventObjIdx].state = (*operand2);
        redraw_hp_mp_flag = -1;
      }
    }
    if (redraw_hp_mp_flag == 0) {
      *scriptEntry = (uint16_t)((*operand3) - 1);
    }
    return;
  case 133:
    PAL_Delay((*operand1) * 10);
    return;
  case 134:
    if ((*operand2) == 0)
      (*operand2) = 1;
    if (count_equipped_items((*operand1)) < (*operand2)) {
      *scriptEntry = (uint16_t)((*operand3) - 1);
    }
    return;
  case 135:
    npc_walk_one_step(eventObjID, 0);
    return;
  case 136:
    if (RPG_money > 5000)
      cashAmount = 5000;
    else
      cashAmount = RPG_money;
    var1 = objects[(*operand1)].data[0];
    copy_subfile_data(var1);
    theurgy_data.baseDamage = (cashAmount * 2) / 5;
    PAL_CopyMem(data_magic + var1 * 32, &theurgy_data.effect, 32);
    RPG_money -= cashAmount;
    return;
  case 137:
    if ((*operand1) == 0)
      (*operand1) = (uint16_t)-1;
    fade_step_count = (*operand1);
    return;
  case 138:
    auto_battle_flag = 9;
    return;
  case 139:
    read_palette((*operand1));
    if (palette_fade_active == 0)
      PAL_IntPalate((uint8_t *)&palette_data[RPG_color_begin_ptr]);
    return;
  case 140:
    if ((*operand2) == 0)
      (*operand2) = 1;
    PAL_CopyMem(&palette_data[768], &palette_data[RPG_color_begin_ptr], 768);
    PAL_CopyMem(&palette_data[1152], &palette_data[RPG_color_begin_ptr], 768);
    var1 = 768;
    var4 = 1152;
    PAL_CorPalate((uint16_t)(*operand1), (uint8_t *)&palette_data[var1]);
    if ((*operand3))
      swap_values(&var1, &var4);
    for (j = 1; j <= 63; j++) {
      PAL_CvPalate((uint8_t *)&palette_data[var4], (const uint8_t *)&palette_data[var1]);
      PAL_IntPalate((uint8_t *)&palette_data[var4]);
      PAL_WaitTime((*operand2));
    }
    return;
  case 141:
    if ((*operand1) == 0)
      (*operand1) = 1;
    level_up_player(roleID, (*operand1), 0);
    playerExp.primary[roleID].exp = 0.0f;
    return;
  case 142:
    restore_screen();
    return;
  case 143:
    RPG_money = vb_round_banker_u32((float)RPG_money / 2.0f);
    return;
  case 144:
    if ((*operand1) > 0) {
      if ((*operand3) == 0)
        objects[(*operand1)].data[2] = (*operand2);
      if ((*operand3) == 1)
        objects[(*operand1)].data[3] = (*operand2);
      if ((*operand3) == 2)
        objects[(*operand1)].data[4] = (*operand2);
    }
    return;
  case 145:
    var1 = enemy_battle_data[eventObjID].enemyID;
    var4 = 0;
    var2 = 0;
    for (j = 0; j <= team_number; j++) {
      if (var1 == enemy_battle_data[j].enemyID) {
        var4++;
        if (j == eventObjID)
          var2 = var4;
      }
    }
    if (var2 > 1) {
      *scriptEntry = (uint16_t)((*operand1) - 1);
    }
    return;
  case 146:
    if (flag_battling) {
      if ((*operand1) > 0) {
        int16_t loopRoleID = (*operand1) - 1;
        player_battle_sprite[loopRoleID].direction = 6;
        draw_battle_scene(1, 0);
        player_attack_anim(loopRoleID, 0);
      }
      animate_battle_sprites(0, enemy_pos_count);
      play_rng_effect(1, 65);
    }
    return;
  case 147:
    cross_fade_out(operand1);
    return;
  case 148:
    if ((*operand1) == 0) {
      return;
    } else {
      if ((*operand1) < 0)
        eventObjIdx = eventObjID;
      else
        eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
      if (eventObjIdx <= 0 || eventObjIdx > curr_scene_event_count) {
        eventObjIdx = 0;
        PAL_CopyMem(&npc_display_data[0].vanishTime, events + ((*operand1) - 1) * 32, 32);
      }
      if (npc_display_data[eventObjIdx].state == (*operand2)) {
        *scriptEntry = (uint16_t)((*operand3) - 1);
      }
      return;
    }
  case 149:
    if (RPG_curr_scene == (*operand1)) {
      *scriptEntry = (uint16_t)((*operand2) - 1);
    }
    return;
  case 150:
    play_opening_anim();
    redraw_flag |= 2;
    curr_scene_id_cache = 0;
    return;
  case 151:
    stepSize = 8;
    goto L_walk_to_pos_68;
  case 152:
    RPG_other_peoples = 0;
    if ((*operand1) > 0) {
      RPG_other_peoples = 1;
      party[RPG_team_number + 1].role = (*operand1);
    }
    if ((*operand2) > 0) {
      RPG_other_peoples = 2;
      party[RPG_team_number + 2].role = (*operand2);
    }
    if (RPG_other_peoples > 0) {
      load_team_mgo();
      set_team_draw();
    }
    return;
  case 153:
    if ((int16_t)(*operand1) < 0) {
      (*operand1) = RPG_curr_scene;
      scenes[(*operand1)].mapNum = (*operand2);
      get_scene_map_source((*operand1));
    } else {
      scenes[(*operand1)].mapNum = (*operand2);
    }
    return;
  case 154:
    if (!((*operand1) == 0 || (*operand2) == 0)) {
      eventObjIdx = (*operand1) - scenes[RPG_curr_scene].eventObjectIndex;
      eventIdx2 = (*operand2) - scenes[RPG_curr_scene].eventObjectIndex;
      if (eventObjIdx > 0 && eventObjIdx <= curr_scene_event_count) {
        for (j = (uint16_t)eventObjIdx; (int16_t)j <= eventIdx2; j++)
          npc_display_data[j].state = (*operand3);
      } else {
        for (j = (*operand1); j <= (*operand2); j++)
          PAL_CopyMem(events + (j - 1) * 32 + 12, operand3, 2);
      }
      return;
    }
    return;
  case 155:
    multi_event_state = (*operand1);
    relative_viewport_x = 0;
    relative_viewport_y = 0;
    if ((*operand1) == 0) {
      curr_scene_id_cache = 0;
      get_scene_map_source(RPG_curr_scene);
      draw_battle_row_sprite();
      multi_event_state = 0;
      return;
    } else {
      if ((int16_t)(*operand2) < 0) {
        PAL_CopyMem(screen_buf, global_buf_1, 64000);
        curr_scene_id_cache = 0;
        draw_battle_row_sprite();
        PAL_CopyMem((uint8_t *)mgo_frame_offsets, global_buf_1, 64000);
        PAL_CopyMem(global_buf_1, screen_buf, 64000);
      } else {
        read_file_and_close("FBP.MKF", (*operand2));
        if (multi_event_state == 1) {
          PAL_Unpak(global_buf_2, global_buf_1);
        } else {
          curr_scene_id_cache = 0;
          PAL_Unpak(global_buf_2, (uint8_t *)mgo_frame_offsets);
        }
      }
      if ((*operand3) == 0)
        (*operand3) = 2;
      if (multi_event_state == 2) {
        multi_event_param = (*operand3);
        multi_event_param_2 = 0;
      }
      return;
    }
  case 156:
    if ((*operand1) == 0)
      (*operand1) = 1;
    stepSize = (*operand1);
    if (count_alive_enemies() == 1 && enemy_battle_data[eventObjID].hp > 1) {
      for (j = 0; j <= 4; j++) {
        if (stepSize > 0 && enemy_battle_data[j].hp <= 0) {
          stepSize--;
          enemy_battle_data[j] = enemy_battle_data[eventObjID];
          enemy_runtime_data[j] = enemy_runtime_data[eventObjID];
          battle_action_type += enemy_runtime_data[j].cash;
          if (enemy_runtime_data[j].exp > (32767 - battle_max_hp_sum))
            battle_max_hp_sum = 32767;
          else
            battle_max_hp_sum += enemy_runtime_data[j].exp;
        }
      }
      for (j = 0; j <= 4; j++) {
        if (enemy_battle_data[j].hp > 0) {
          team_number = j;
          enemy_battle_data[j].hp = (enemy_battle_data[j].hp + (*operand1)) / ((*operand1) + 1);
        }
      }
      enemy_max_id = team_number + 1;
      for (j = 0; j <= team_number; j++) {
        enemy_battle_data[j].origX = enemy_pos(team_number, j).x;
        enemy_battle_data[j].origY = enemy_pos(team_number, j).y + enemy_runtime_data[j].yPosOffset;
      }
      for (k = 1; k <= 10; k++) {
        for (j = 0; j <= team_number; j++) {
          enemy_battle_data[j].x = (enemy_battle_data[j].x + enemy_battle_data[j].origX) / 2;
          enemy_battle_data[j].y = (enemy_battle_data[j].y + enemy_battle_data[j].origY) / 2;
        }
        draw_battle_scene(1, 0);
      }
      init_enemy_positions();
    } else if ((*operand2) != 0) {
      *scriptEntry = (uint16_t)((*operand2) - 1);
      redraw_hp_mp_flag = 0;
    }
    return;
  case 157:
    if (count_alive_enemies() < enemy_max_id) {
      enemy_cast_anim(eventObjID);
      for (j = 0; j <= team_number; j++) {
        if (enemy_battle_data[j].objectID > 0 && enemy_battle_data[j].hp <= 0) {
          load_enemy_data(j, enemy_battle_data[j].objectID);
          clear_enemy_poison(j);
          enemy_status[j][1] = 1;
          player_hit_flags[j] = (uint16_t)-1;
          enemy_battle_data[j].hp = enemy_runtime_data[j].health / 2;
        }
        if ((*operand1) == 0)
          j = enemy_max_id;
      }
      load_enemy_sprites();
      (*operand1) = 70;
      play_rng_effect(2, 78);
      for (j = 0; j <= 4; j++)
        player_hit_flags[j] = 0;
      play_rng_effect(1, 42);
      enemy_battle_data[eventObjID].direction = enemy_battle_data[eventObjID].flag;
    } else if ((*operand2) != 0) {
      *scriptEntry = (uint16_t)((*operand2) - 1);
      redraw_hp_mp_flag = 0;
    }
    return;
  case 158:
    if ((*operand1) <= 0)
      (*operand1) = enemy_battle_data[eventObjID].objectID;
    if ((*operand2) == 0)
      (*operand2) = 1;
    stepSize = (*operand2);
    if (count_alive_enemies() + (*operand2) <= enemy_max_id) {
      enemy_cast_anim(eventObjID);
      for (j = 0; j <= 4; j++) {
        if (stepSize > 0 && enemy_battle_data[j].hp <= 0) {
          stepSize--;
          load_enemy_data(j, (*operand1));
          clear_enemy_poison(j);
          enemy_status[j][1] = 1;
          player_hit_flags[j] = (uint16_t)-1;
        }
      }
      savedDir = enemy_battle_data[eventObjID].direction;
      load_enemy_sprites();
      enemy_battle_data[eventObjID].direction = savedDir;
      play_sound(212, 1);
      play_rng_effect(1, 78);
      for (j = 0; j <= 4; j++)
        player_hit_flags[j] = 0;
      play_rng_effect(1, 42);
      enemy_battle_data[eventObjID].direction = enemy_battle_data[eventObjID].flag;
    } else if ((*operand3) != 0) {
      *scriptEntry = (uint16_t)((*operand3) - 1);
      redraw_hp_mp_flag = 0;
    }
    return;
  case 159:
    if ((*operand1) > 0) {
      enemy_cast_anim(eventObjID);
      battle_max_hp_sum -= enemy_runtime_data[eventObjID].exp;
      battle_action_type -= enemy_runtime_data[eventObjID].cash;
      savedHP = enemy_battle_data[eventObjID].hp;
      savedUseScript = enemy_battle_data[eventObjID].useScript;
      savedEndScript = enemy_battle_data[eventObjID].battleEndScript;
      load_enemy_data(eventObjID, (*operand1));
      enemy_battle_data[eventObjID].hp = savedHP;
      enemy_battle_data[eventObjID].useScript = savedUseScript;
      enemy_battle_data[eventObjID].battleEndScript = savedEndScript;
      load_enemy_sprites();
      if ((*operand2) == 0)
        (*operand2) = 47;
      play_sound((*operand2), 0);
      play_rng_effect(1, 72);
    }
    return;
  case 160:
    // NOTE: 结局三连播（golden 传 stop=1 不可跳，remake 照搬；结局动画
    // 由 avi.c 解码播出，播完进 release_resources_exit）。
    PAL_PlayAvi(NULL, 4, 1);
    PAL_PlayAvi(NULL, 5, 1);
    PAL_PlayAvi(NULL, 6, 1);
    release_resources_exit();
    return;
  case 161:
    for (j = 1; j <= RPG_team_number; j++) {
      party[j].x = team_abstract_x;
      party[j].y = team_abstract_y - 2;
      trail[j].x = party_abs_x;
      trail[j].y = party_abs_y;
    }
    return;
  case 162:
    rng = VB_rtcRandomNext();
    *scriptEntry = (uint16_t)(*scriptEntry + VB_Int(rng * (*operand1)));
    return;
  case 163:
    if ((*operand3) <= 1)
      (*operand3) ^= 1;
    play_cd((*operand1), (*operand2), (*operand3));
    return;
  case 164:
    if ((*operand3) <= 0)
      (*operand3) = 10;
    scroll_scene_with_fbp((*operand1), (*operand2), (*operand3));
    redraw_flag |= 2;
    curr_scene_id_cache = 0;
    return;
  case 165:
    if ((*operand3) <= 0)
      (*operand3) = 10;
    fade_in_pic((*operand1), (*operand2), (*operand3));
    redraw_flag |= 2;
    curr_scene_id_cache = 0;
    return;
  case 166:
    PAL_PushScreen(global_buf_1);
    return;
  case 167:
    dialog_type = 9;
    dialog_width = 72;
    dialog_height = eventObjID;
    dialog_x = 60;
    if ((*operand1))
      dialog_x = (*operand1);
    return;
  default:
    return;
  }
}

// NOTE: remake 增补入口，golden 无独立 main（进程生命周期由 VB 宿主
// Sub_Main + Form_Unload + End 承担）；数据目录经 $PAL98_DATA 解析。
int main(int argc, char *argv[]) {
  int16_t saveChoice;
  uint8_t row, col;

  (void)argc;
  (void)argv;

  exit_flag = 0;

  // NOTE: PAL_InitCD remake 恒 0（无光驱）；golden 失败 → MsgBox 重试或 End，
  // 该分支不可达。
  if (PAL_InitCD() != 0) {
    fprintf(stderr, "[remake] PAL_InitCD failed (unreachable in remake)\n");
  }

  PAL_SetTimer();

  // NOTE: PAL_InitDSound remake 接 audio.cpp/miniaudio（设备失败返回 1 =
  // golden 无声卡路径），不再有 DirectSound 对象。
  int32_t dsInitResult = PAL_InitDSound(0);
  if (dsInitResult != 0) {
    music_mode = 0;
    has_sfx_raw = 0;
  } else {
    music_mode = 2;
    has_sfx_raw = 1;
    load_sound_data();
  }
  use_cd_flag = (music_mode != 0);
  has_sfx = (has_sfx_raw != 0);

  // NOTE: PAL_InitInput_Win remake 恒 0（kitty 输入直读）；golden 失败 →
  // MsgBox + End，该分支不可达。
  if (PAL_InitInput_Win(0, 0) != 0)
    fprintf(stderr, "[remake] PAL_InitInput failed (unreachable in remake)\n");

  fh_M_MSG = open_file_required("M.MSG");
  fh_RNG_MKF = open_file_required("RNG.MKF");
  fh_MGO_MKF = open_file_required("MGO.MKF");
  fh_F_MKF = open_file_required("F.MKF");
  fh_ABC_MKF = open_file_required("ABC.MKF");

  Load_system_files();

  // NOTE: golden SetMode 失败 → MsgBox + End；remake 降级 stderr 告警继续
  // （无视频时呈现函数空转，支持无头调试）。
  if (PAL_SetMode(0) != 0) {
    fprintf(stderr, "[remake] PAL_SetMode: video not up, running headless\n");
  }

  init_key_definitions();
  PAL_ClearScreen();
  init_done_flag = 1;

  if (pal_skip_avi()) {
    // NOTE: 偏离 golden：AVI 屏原本负责初始化调色板，此处改为先载真调色板、
    // 免动画入黑，由标题 fade_out_palette 完成唯一可见淡入。
    read_palette(0);
    palette_fade_active = 0;
    fade_in(0);
  } else {
    PAL_PlayAvi(NULL, 1, 0);
    PAL_PlayAvi(NULL, 2, 0);
    palette_fade_active = 0;
    fade_in(2);
    read_palette(0);
  }

  RPG_ememy_chase_rate = 1;
  RPG_change_chaserate_times = 0;
  music_mode_init = 3;
  play_all_kinds_music(4, 1);

  do {
    show_fbp_picture(2, 0);
    fade_out_palette(1);
    battle_order_array[0] = 7;
    battle_order_array[1] = 8;
    battle_order_array[100] = (uint16_t)-1;
    battle_order_array[101] = (uint16_t)-1;
    saveChoice = (int16_t)menu_select(&(int16_t){0}, 112, 84, (uint16_t)-1, 2);
    redraw_flag = 16;
    if (saveChoice != 1)
      goto L_newgame;
    saveChoice = check_save_file();
  } while (saveChoice < 0);
  LoadRPG_internal((uint16_t)(saveChoice + 1));
  if (RPG_save_number == 0) {
    goto L_newgame;
  } else {
    redraw_flag |= 2;
    goto L_main_loop;
  }

L_newgame:
  midi_close();
  fade_in(1);
  if (!pal_skip_avi())
    PAL_PlayAvi(NULL, 3, 0);
  scene_to_load = 1;
  redraw_flag |= 13;
  uint16_t *flat = (uint16_t *)&playerExp;
  for (row = 0; row <= 4; row++) {
    for (col = 0; col <= 7; col++) {
      int16_t r1 = 0, r2 = 0;
      if (col > 0) {
        r1 = vb_round_banker_i16_d((VB_rtcRandomNext() * 2.0) + 2.0);
        r2 = vb_round_banker_i16_d(VB_rtcRandomNext() * 20.0);
      }
      float expVal = (float)r2;
      int idx = (col * MAX_PLAYER_ROLES * 4) + (row * 4);
      memcpy(&flat[idx], &expVal, 4);
      flat[idx + 2] = (uint16_t)(playerRoles(row, 6) + r1);
    }
  }

L_main_loop:
  frame_counter = 0;
  battle_viewport_x = (63 - 10) * 32;
  battle_viewport_y = (127 - 12) * 16;
  PAL_Flip();

L_scene:
  for (;;) {
    scene_transition();
    for (;;) {
      int16_t moving, offX, offY;
      int32_t action;

      read_direction_input(&scene_enter_flag, &scene_leave_flag);
      action = key_pressed;
      redraw_flag = 0;
      process_event_objects(-1);
      if (exit_flag != 0) {
        // NOTE: remake 增补：退出请求放行（golden 无此分支）。
        release_resources_exit();
        return 0;
      }
      if (redraw_flag != 0)
        goto L_scene;

      PAL_ClearMenu(global_buf_2, 8192);
      PAL_ClearTree();
      if (scene_enter_flag == 0)
        scene_enter_flag = -scene_leave_flag;
      if (scene_leave_flag == 0)
        scene_leave_flag = scene_enter_flag;

      moving = 0;
      offX = 0;
      offY = 0;
      if (scene_enter_flag != 0) {
        if (load_map_data((team_abstract_x + RPG_viewport_x) + scene_enter_flag * 16,
                          (team_abstract_y + RPG_viewport_y) + scene_leave_flag * 8, 0)) {
          offX = (int16_t)(scene_enter_flag * 16);
          offY = (int16_t)(scene_leave_flag * 8);
          moving = 1;
        }
      }
      PAL_ExTF((uint16_t *)&RPG_team_direction, scene_enter_flag, scene_leave_flag);
      x_off = RPG_viewport_x + team_abstract_x;
      y_off = RPG_viewport_y + team_abstract_y;
      viewport_x_bak = RPG_viewport_x;
      viewport_y_bak = RPG_viewport_y;
      if (moving) {
        RPG_viewport_x = RPG_viewport_x + offX;
        RPG_viewport_y = RPG_viewport_y + offY;
        PAL_Ffxy(&RPG_viewport_x, &RPG_viewport_y, battle_viewport_x, battle_viewport_y);
        if (RPG_viewport_x == viewport_x_bak)
          RPG_viewport_y = viewport_y_bak;
        if (RPG_viewport_y == viewport_y_bak)
          RPG_viewport_x = viewport_x_bak;
        update_party_position();
      } else {
        init_walk_frames();
      }
      party_abs_x = team_abstract_x + RPG_viewport_x;
      party_abs_y = team_abstract_y + RPG_viewport_y;

      get_party_role_id();
      get_sprites_curr_scene();
      render_scene_with_rng();
      update_viewport_scroll();
      render_scene_and_fade(1);
      if (!moving) {
        PAL_Flip();
      }
      PAL_Flip();

      switch (action) {
      case 1:
        process_menu();
        break;
      case 2:
        check_trigger_events();
        break;
      case 12:
        select_magic();
        draw_battle_row_sprite();
        break;
      case 13:
        inventory_use_menu();
        draw_battle_row_sprite();
        break;
      case 14:
        if (yes_no_menu(0, 19) == 1)
          release_resources_exit();
        draw_battle_row_sprite();
        break;
      case 15:
        entry_stub_show_text();
        draw_battle_row_sprite();
        break;
      case 16:
        use_item_menu();
        draw_battle_row_sprite();
        break;
      default:
        break;
      }
      mutex_shaking ^= 1;
      PAL_Flip();
      if (redraw_flag != 0)
        goto L_scene;
    }
  }
  return 0;
}
