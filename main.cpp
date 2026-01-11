#include "data_ids.hpp"
#define GLM_FORCE_SWIZZLE
#define GLEW_STATIC

#include <array>
#include <assert.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <stdexcept>

#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include <random>

#include "stb_image/stb_image.h"


// Include all GLM core / GLSL features
#include <glm/glm.hpp> // vec2, vec3, mat4, radians
// Include all GLM extensions
#include <glm/ext.hpp> // perspective, translate, rotate
#include "glm/ext/matrix_transform.hpp"

#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#include "data.hpp"

#include "frustum.hpp"

namespace game {

struct skill_ids {
	dcon::skill_id cooking;
};

struct ai_state {
	dcon::activity_id weapon_repair;
	dcon::activity_id shopping;
	dcon::activity_id getting_food;
	dcon::activity_id prepare_food;
	dcon::activity_id working;
};

struct ai_personality {
	dcon::ai_model_id hunter;
	dcon::ai_model_id alchemist;
	dcon::ai_model_id weapon_master;
	dcon::ai_model_id herbalist;
	dcon::ai_model_id innkeeper;
	dcon::ai_model_id shopkeeper;
};

constexpr inline float BASE_FOOD_NUTRITION = 250.f;

constexpr int CHUNK_SIZE = 32;
constexpr int CHUNK_AREA = CHUNK_SIZE * CHUNK_SIZE;

constexpr int WORLD_RADIUS = 16;
constexpr int WORLD_SIZE = WORLD_RADIUS * 2;
constexpr int WORLD_AREA = WORLD_SIZE * WORLD_SIZE;

constexpr int WORLD_SIZE_TILES = CHUNK_SIZE * WORLD_SIZE;
constexpr int WORLD_AREA_TILES = WORLD_SIZE_TILES * WORLD_SIZE_TILES;

struct vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texcoord;
};

struct mesh {
	std::vector<vertex> data;
	GLuint vao;
	GLuint vbo;
};

struct map_state {
	std::array<char, WORLD_AREA_TILES> height {};
	std::array<mesh, WORLD_AREA> meshes {};
};
char get_height(map_state& data, int x, int y) {
	auto c_x = x + WORLD_RADIUS * CHUNK_SIZE;
	auto c_y = y + WORLD_RADIUS * CHUNK_SIZE;
	return data.height[c_x * WORLD_SIZE_TILES + c_y];
}
void set_height(map_state& data, int x, int y, char value) {
	auto c_x = x + WORLD_RADIUS * CHUNK_SIZE;
	auto c_y = y + WORLD_RADIUS * CHUNK_SIZE;
	data.height[c_x * WORLD_SIZE_TILES + c_y] = value;
}

struct kinds {
	dcon::kind_id human;
	dcon::kind_id potion_flower;
	dcon::kind_id meatbug_queen;
	dcon::kind_id meatbug;
	dcon::kind_id tree;
};

struct state {
	dcon::data_container data;
	uint32_t time;


	dcon::commodity_id potion;
	dcon::commodity_id coins;
	dcon::commodity_id potion_material;
	dcon::commodity_id raw_food;
	dcon::commodity_id prepared_food;
	dcon::commodity_id weapon_service;


	dcon::building_model_id inn;
	dcon::building_model_id shop;
	dcon::building_model_id shop_weapon;

	skill_ids skills;
	ai_state ai;
	ai_personality personality;
	kinds special_kinds;

	int price_update_tick = 0;

	map_state map;

	std::default_random_engine rng {};
	std::uniform_real_distribution<float> uniform{0.0, 1.0};
	std::normal_distribution<float> normal {0.f, 1.f};
};

std::string get_name (state& game, dcon::commodity_id commodity) {
	if (game.potion == commodity) {
		return "Potion";
	} else if (game.coins == commodity) {
		return  "Coins";
	} else if (game.potion_material == commodity) {
		return "Potion material";
	} else if (game.raw_food == commodity) {
		return  "Food ingredients";
	} else if (game.prepared_food == commodity) {
		return  "Food";
	} else if (game.weapon_service == commodity) {
		return "Weapon service";
	}
	return "Unknown " + std::to_string(commodity.index());
}

std::string get_name (state& game, dcon::activity_id activity) {
	if (!activity) {
		return "Idle";
	}
	if (game.ai.getting_food == activity) {
		return "Looking for food";
	} else if (game.ai.prepare_food == activity) {
		return  "Preparing food";
	} else if (game.ai.shopping == activity) {
		return "Trading";
	} else if (game.ai.weapon_repair == activity) {
		return  "Repair weapon";
	} else if (game.ai.working == activity) {
		return  "Working";
	}
	return "Unknown " + std::to_string(activity.index());
}

void transaction(state& game, dcon::character_id A, dcon::character_id B, dcon::commodity_id C, float amount) {
	assert(amount >= 0.f);
	auto i_A = game.data.character_get_inventory(A, C);
	auto i_B = game.data.character_get_inventory(B, C);

	assert(i_A >= amount);
	game.data.character_set_inventory(A, C, i_A - amount);
	game.data.character_set_inventory(B, C, i_B + amount);
}
void delayed_transaction(state& game, dcon::character_id A, dcon::character_id B, dcon::commodity_id C, float amount) {
	auto delayed = game.data.get_delayed_transaction_by_transaction_pair(A, B);
	if (delayed) {
		auto mul = 1;
		if (A == game.data.delayed_transaction_get_members(delayed, 1)) {
			mul = -1;
		}
		auto debt = game.data.delayed_transaction_get_balance(delayed, C);
		game.data.delayed_transaction_set_balance(delayed, C, debt + (float)mul * amount);
	} else {
		delayed = game.data.force_create_delayed_transaction(A, B);
		game.data.delayed_transaction_set_balance(delayed, C, amount);
	}
}
enum class move_result {
	completed, failed, in_progress
};
constexpr float speed = 0.1f;
move_result move_to(state& game, dcon::thing_id cid, float target_x, float target_y) {
	auto x = game.data.thing_get_x(cid);
	auto y = game.data.thing_get_y(cid);
	auto dx = target_x - x;
	auto dy = target_y - y;
	auto distance = sqrtf(dx * dx + dy * dy);

	if (distance < speed) {
		game.data.thing_set_x(cid, target_x);
		game.data.thing_set_y(cid, target_y);
		return move_result::completed;
	} else {
		game.data.thing_set_x(cid, x + dx / distance * speed);
		game.data.thing_set_y(cid, y + dy / distance * speed);
		return move_result::in_progress;
	}
	return move_result::failed;
}


move_result move_to(state& game, dcon::thing_id cid, dcon::building_id target) {
	auto target_x = game.data.building_get_tile_x(target);
	auto target_y = game.data.building_get_tile_y(target);

	auto guest_in = game.data.thing_get_guest_location_from_guest(cid);

	if (guest_in == target) {
		return move_result::completed;
	} else if (guest_in) {
		game.data.delete_guest(game.data.thing_get_guest(cid));
		return move_result::in_progress;
	} else {
		auto result = move_to(game, cid, (float)target_x, (float)target_y);
		if (result == move_result::completed) {
			game.data.force_create_guest(cid, target);
			return move_result::completed;
		}
		return result;
	}
}


void exit_the_guested(state& game, dcon::thing_id one_which_exits) {
	auto guest_in = game.data.thing_get_guest_location_from_guest(one_which_exits);
	if (guest_in) {
		auto x = game.data.building_get_tile_x(guest_in);
		auto y = game.data.building_get_tile_y(guest_in);
		game.data.delete_guest(game.data.thing_get_guest(one_which_exits));
		game.data.thing_set_x(one_which_exits, (float)x);
		game.data.thing_set_y(one_which_exits, (float)y);
	}
}


enum class hunt_result {
	moving_to_target, attacking_target, seeking_target, success
};

hunt_result hunt(state& game, dcon::thing_id hunter) {
	exit_the_guested(game, hunter);

	auto selection = game.data.thing_get_hunt_target_as_hunter(hunter);
	auto target = game.data.hunt_target_get_hunted(selection);
	auto x = game.data.thing_get_x(hunter);
	auto y = game.data.thing_get_y(hunter);

	auto kind_of_the_hunter = game.data.thing_get_kind(hunter);

	if (!target){
		auto min_distance_2 = 1000.f * 1000.f;
		game.data.for_each_thing([&](dcon::thing_id candidate){
			auto kind_of_the_hunted = game.data.thing_get_kind(candidate);

			auto tx = game.data.thing_get_x(candidate);
			auto ty = game.data.thing_get_y(candidate);

			auto d = (tx - x) * (tx - x) + (ty - y) * (ty - y);

			if (
				d < min_distance_2
				&& game.data.get_food_hierarchy_by_consumption_pair(
					kind_of_the_hunter,
					kind_of_the_hunted
				)
				&& game.data.thing_get_hp(candidate) > 0
			) {
				min_distance_2 = d;
				target = candidate;
			}
		});

		if (target) {
			game.data.force_create_hunt_target(hunter, target);
		} else {
			return hunt_result::seeking_target;
		}
	}

	auto tx = game.data.thing_get_x(target);
	auto ty = game.data.thing_get_y(target);

	auto d = (tx - x) * (tx - x) + (ty - y) * (ty - y);

	if (d < 1.f) {
		auto one_which_embodies = game.data.thing_get_embodier_from_embodiment(hunter);

		auto damage = 10;

		if (one_which_embodies) {
			auto weapon = game.data.character_get_weapon_quality(one_which_embodies);
			damage *= (1.f + weapon);
			auto quality = game.data.character_get_weapon_quality(one_which_embodies);
			game.data.character_set_weapon_quality(one_which_embodies, quality * 0.95f);
		}

		game.data.thing_set_hp(target, game.data.thing_get_hp(target) - damage);

		if (game.data.thing_get_hp(target) <= 0.f) {
			if (one_which_embodies) {
				auto food = game.data.character_get_inventory(one_which_embodies, game.raw_food);
				game.data.character_set_inventory(one_which_embodies, game.raw_food, food + 1.f);
			} else {
				game.data.thing_set_hp(hunter, game.data.thing_get_hp(hunter) + 5);
				game.data.thing_set_hunger(hunter, game.data.thing_get_hunger(hunter) - BASE_FOOD_NUTRITION);
			}
			game.data.delete_thing(target);
			return hunt_result::success;
		} else {
			return  hunt_result::attacking_target;
		}
	} else {
		move_to(game, hunter, tx, ty);
		return hunt_result::moving_to_target;
	}
}

void repair_weapon(state& game, dcon::character_id cid, dcon::character_id master) {
	auto timer = game.data.character_get_action_timer(cid);
	auto weapon_repair_price = game.data.character_get_price_belief_sell(master, game.weapon_service);
	if (timer == 0) {
		printf("start repair\n");
		game.data.character_set_action_timer(cid, timer + 1);
		transaction(game, cid, master, game.coins, weapon_repair_price);
		game.data.character_set_price_belief_sell(master, game.weapon_service, weapon_repair_price * 1.05f);
	} else if (timer > 4) {
		printf("complete repair\n");
		auto quality = game.data.character_get_weapon_quality(cid);
		game.data.character_set_weapon_quality(cid, quality + 0.3f);
		game.data.character_set_action_timer(cid, 0.f);
		game.data.character_set_action_type(cid, {});
		auto body = game.data.character_get_body_from_embodiment(cid);
		game.data.delete_guest(game.data.thing_get_guest(body));
	} else {
		game.data.character_set_action_timer(cid, timer + 1);
	}
}

void make_potion(state& game, dcon::character_id cid) {
	auto material = game.data.character_get_inventory(cid, game.potion_material);
	assert(material >= 1.f);
	auto timer = game.data.character_get_action_timer(cid);
	if (timer > 6) {
		auto potion = game.data.character_get_inventory(cid, game.potion);
		game.data.character_set_inventory(cid, game.potion_material, material - 1.f);
		game.data.character_set_inventory(cid, game.potion, potion + 1.f);
		game.data.character_set_action_timer(cid, 0);
		game.data.character_set_action_type(cid, {});
	} else {
		game.data.character_set_action_timer(cid, timer + 1);
	}
}

void increase_hp(state& game, dcon::thing_id target, int value) {
	auto hp = game.data.thing_get_hp(target);
	auto hp_max = game.data.thing_get_hp_max(target);
	game.data.thing_set_hp(target, std::min(hp_max, hp + value));
}

void prepare_food(state& game, dcon::character_id cid) {
	auto material = game.data.character_get_inventory(cid, game.raw_food);
	assert(material >= 1.f);
	auto timer = game.data.character_get_action_timer(cid);
	if (timer > 1) {
		auto result = game.data.character_get_inventory(cid, game.prepared_food);
		// auto skill = game.data.character_get_skills(cid, )
		auto skill_bonus = (float)(int)(game.data.character_get_skills(cid, game.skills.cooking) / 0.3);
		game.data.character_set_inventory(cid, game.raw_food, material - 1.f);
		game.data.character_set_inventory(cid, game.prepared_food, result + 1.f + skill_bonus);
		game.data.character_set_action_timer(cid, 0);
		game.data.character_set_action_type(cid, {});
	} else {
		game.data.character_set_action_timer(cid, timer + 1);
	}
}
void gather_potion_material(state& game, dcon::character_id cid) {
	auto timer = game.data.character_get_action_timer(cid);
	if (timer > 3) {
		auto count = game.data.character_get_inventory(cid, game.potion_material);
		game.data.character_set_inventory(cid, game.potion_material, count + 1.f);
		game.data.character_set_action_timer(cid, 0);
		game.data.character_set_action_type(cid, {});
	} else {
		game.data.character_set_action_timer(cid, timer + 1);
	}
}

void eat(state& game, dcon::character_id cid) {
	auto food = game.data.character_get_inventory(cid, game.prepared_food);
	auto body = game.data.character_get_body_from_embodiment(cid);
	auto hunger = game.data.thing_get_hunger(body);
	if (food >= 1.f) {
		game.data.character_set_inventory(cid, game.prepared_food, food - 1);
		game.data.thing_set_hunger(body, hunger - BASE_FOOD_NUTRITION);
		increase_hp(game, body, 10);
	}
}
void drink_potion(state& game, dcon::character_id cid) {
	auto potions = game.data.character_get_inventory(cid, game.potion);
	auto body = game.data.character_get_body_from_embodiment(cid);
	auto hp = game.data.thing_get_hp(body);
	auto hp_max = game.data.thing_get_hp_max(body);
	if (hp * 2 < hp_max && potions >= 1.f) {
		game.data.character_set_inventory(cid, game.potion, potions - 1);
		increase_hp(game, body, 10);
	}
}



namespace ai {

void reset_action(state& game, dcon::character_id cid) {
	game.data.character_set_action_timer(cid, 0);
	game.data.character_set_action_type(cid, {});
}

namespace triggers {

bool desire_weapon_repair(state& game, dcon::character_id cid, dcon::character_id master) {
	if (game.data.character_get_weapon_quality(cid) > 2.f) {
		return false;
	}

	auto coins = game.data.character_get_inventory(cid, game.coins);
	auto weapon_repair_price = game.data.character_get_price_belief_sell(master, game.weapon_service);
	if (weapon_repair_price * 3.f > coins) {
		return false;
	}

	return true;
}

bool desire_buy_food(state& game, dcon::character_id cid) {
	auto ai_type = game.data.character_get_ai_type(cid);
	auto food = game.data.character_get_inventory(cid, game.prepared_food);
	auto food_target = game.data.ai_model_get_stockpile_target(ai_type,  game.prepared_food);
	auto favourite_inn = game.data.character_get_favourite_inn(cid);
	auto favourute_innkeeper = game.data.building_get_owner_from_ownership(favourite_inn);
	auto coins = game.data.character_get_inventory(cid, game.coins);

	auto in_stock = game.data.character_get_inventory(favourute_innkeeper, game.prepared_food);

	if (food_target < food) {
		return false;
	}

	if (in_stock < 1.f) {
		return false;
	}

	auto food_price = game.data.character_get_price_belief_sell(favourute_innkeeper, game.prepared_food);

	if (food_price * 2.f > coins) {
		return false;
	}

	return true;
}

bool hunter_desire_shopping(state& game, dcon::character_id cid) {
	auto ai_type = game.data.character_get_ai_type(cid);

	auto loot  = game.data.character_get_inventory(cid, game.raw_food);
	auto loot_target = game.data.ai_model_get_stockpile_target(ai_type, game.raw_food);

	auto bottom_price = game.data.character_get_price_belief_buy(cid, game.prepared_food) / 5.f;

	auto favourite_shop = game.data.character_get_favourite_shop(cid);
	auto favourute_shopkeeper = game.data.building_get_owner_from_ownership(favourite_shop);
	auto price_shop_buy = game.data.character_get_price_belief_buy(favourute_shopkeeper, game.raw_food);

	return (loot - loot_target > 3 && price_shop_buy > bottom_price);
}

bool alchemist_desire_shopping(state& game, dcon::character_id cid) {
	auto ai_type = game.data.character_get_ai_type(cid);

	auto produced  = game.data.character_get_inventory(cid, game.potion);
	auto produced_target = game.data.ai_model_get_stockpile_target(ai_type, game.potion);

	auto materials = game.data.character_get_inventory(cid, game.potion_material);
	auto materials_target = game.data.ai_model_get_stockpile_target(ai_type, game.potion_material);

	auto bottom_price = game.data.character_get_price_belief_buy(cid, game.prepared_food) / 5.f;

	auto favourite_shop = game.data.character_get_favourite_shop(cid);
	auto favourute_shopkeeper = game.data.building_get_owner_from_ownership(favourite_shop);
	auto price_shop_buy = game.data.character_get_price_belief_buy(favourute_shopkeeper, game.potion);

	return (produced - produced_target > 3 && price_shop_buy > bottom_price) || materials_target > materials;
}

}

namespace update {

void alchemist(state& game, dcon::character_id cid) {
	auto body = game.data.character_get_body_from_embodiment(cid);
	auto x = game.data.thing_get_x(body);
	auto y = game.data.thing_get_y(body);
	auto coins = game.data.character_get_inventory(cid, game.coins);
	auto action = game.data.character_get_action_type(cid);
	auto ai_type = game.data.character_get_ai_type(cid);

	assert(ai_type == game.personality.alchemist);

	auto favourite_shop = game.data.character_get_favourite_shop(cid);
	auto favourite_inn = game.data.character_get_favourite_inn(cid);

	auto favourute_shopkeeper = game.data.building_get_owner_from_ownership(favourite_shop);

	if (!action) {
		if (
			ai::triggers::alchemist_desire_shopping(game, cid)
		) {
			game.data.character_set_action_type(cid, game.ai.shopping);
		} else if (
			ai::triggers::desire_buy_food(game, cid)
		) {
			game.data.character_set_action_type(cid, game.ai.getting_food);
		} else {
			game.data.character_set_action_type(cid, game.ai.working);
		}
		action = game.data.character_get_action_type(cid);
	}

	if (action == game.ai.shopping) {
		if (!ai::triggers::alchemist_desire_shopping(game, cid)) {
			ai::reset_action(game, cid);
		}
		auto move = move_to(game, body, favourite_shop);
		return;
	}
	if (action == game.ai.getting_food) {
		if (!ai::triggers::desire_buy_food(game, cid)) {
			ai::reset_action(game, cid);
		}
		auto move = move_to(game, body, favourite_inn);
		return;
	}
	if (action == game.ai.working) {
		auto potion_price = game.data.character_get_price_belief_buy(favourute_shopkeeper, game.potion);
		auto potion_material_cost = game.data.character_get_price_belief_sell(favourute_shopkeeper, game.potion_material);
		if (
			game.data.character_get_inventory(cid, game.potion_material) >= 1.f
			&& potion_price > potion_material_cost * 2.f
		) {
			make_potion(game, cid);
		} else {
			ai::reset_action(game, cid);
		}
		return;
	}
	game.data.character_set_action_timer(cid, 0);
}

void hunter(state& game, dcon::character_id cid) {
	auto body = game.data.character_get_body_from_embodiment(cid);
	auto x = game.data.thing_get_x(body);
	auto y = game.data.thing_get_y(body);
	auto coins = game.data.character_get_inventory(cid, game.coins);
	auto action = game.data.character_get_action_type(cid);
	auto ai_type = game.data.character_get_ai_type(cid);

	assert(ai_type == game.personality.hunter);

	auto favourite_weapons_shop = game.data.character_get_favourite_shop_weapons(cid);
	auto favourite_weapons_shop_owner = game.data.building_get_owner_from_ownership(favourite_weapons_shop);
	auto weapon_repair_price = game.data.character_get_price_belief_sell(favourite_weapons_shop_owner, game.weapon_service);

	auto favourite_shop = game.data.character_get_favourite_shop(cid);
	auto favourite_inn = game.data.character_get_favourite_inn(cid);



	if (!action) {
		if (
			ai::triggers::hunter_desire_shopping(game, cid)
		) {
			game.data.character_set_action_type(cid, game.ai.shopping);
		} else if (
			ai::triggers::desire_weapon_repair(game, cid, favourite_weapons_shop_owner)
		) {
			game.data.character_set_action_type(cid, game.ai.weapon_repair);
		} else if (
			game.data.thing_get_hunger(body) > 200
			&& game.data.character_get_inventory(cid, game.raw_food) >= 1.f
		) {
			game.data.character_set_action_type(cid, game.ai.prepare_food);
		} else if (
			ai::triggers::desire_buy_food(game, cid)
		) {
			game.data.character_set_action_type(cid, game.ai.getting_food);
		} else {
			game.data.character_set_action_type(cid, game.ai.working);
		}
	}

	auto guest_in = game.data.thing_get_guest_location_from_guest(body);
	auto timer = game.data.character_get_action_timer(cid);

	if (action == game.ai.weapon_repair) {
		if (
			ai::triggers::desire_weapon_repair(game, cid, favourite_weapons_shop_owner) || timer > 0
		) {
			auto move = move_to(game, body, favourite_weapons_shop);
			if (move == move_result::completed) {
				repair_weapon(game, cid, favourite_weapons_shop_owner);
			}
		} else {
			game.data.character_set_action_timer(cid, 0);
			game.data.character_set_action_type(cid, {});
		}
		return;
	} else if (action == game.ai.prepare_food) {
		if (game.data.character_get_inventory(cid, game.raw_food) >= 1.f) {
			prepare_food(game, cid);
		} else {
			ai::reset_action(game, cid);
		}
		return;
	} else if (action == game.ai.working) {
		auto result = hunt(game, body);
		if (result == hunt_result::success) {
			ai::reset_action(game, cid);
		}
		return;
	} else if (action == game.ai.shopping) {
		if (!ai::triggers::hunter_desire_shopping(game, cid)) {
			ai::reset_action(game, cid);
		}
		auto move = move_to(game, body, favourite_shop);
		return;
	} else if (action == game.ai.getting_food) {
		if (!ai::triggers::desire_buy_food(game, cid)) {
			ai::reset_action(game, cid);
		}
		auto move = move_to(game, body, favourite_inn);
		return;
	}
	game.data.character_set_action_timer(cid, 0);
}

}


}

void init(state& game) {
	game.data.character_resize_skills(256);
	game.data.character_resize_price_belief_buy(256);
	game.data.character_resize_price_belief_sell(256);
	game.data.character_resize_inventory(256);
	game.data.ai_model_resize_stockpile_target(256);
	game.data.delayed_transaction_resize_balance(256);

	game.ai.getting_food = game.data.create_activity();
	game.ai.shopping = game.data.create_activity();
	game.ai.weapon_repair = game.data.create_activity();
	game.ai.working = game.data.create_activity();
	game.ai.prepare_food = game.data.create_activity();

	game.coins = game.data.create_commodity();
	game.potion_material = game.data.create_commodity();
	game.potion = game.data.create_commodity();
	game.raw_food = game.data.create_commodity();
	game.prepared_food = game.data.create_commodity();
	game.weapon_service = game.data.create_commodity();

	game.skills.cooking = game.data.create_skill();

	game.inn = game.data.create_building_model();
	game.shop = game.data.create_building_model();
	game.shop_weapon = game.data.create_building_model();

	game.special_kinds.human = game.data.create_kind();
	game.data.kind_set_size(game.special_kinds.human, 1.f);
	game.data.kind_set_speed(game.special_kinds.human, 1.f);

	auto rat = game.data.create_kind();
	game.data.kind_set_size(rat, 0.5f);
	game.data.kind_set_speed(rat, 0.8f);

	game.special_kinds.meatbug = game.data.create_kind();
	game.data.kind_set_size(game.special_kinds.meatbug, 0.1f);
	game.data.kind_set_speed(game.special_kinds.meatbug, 0.2f);

	game.special_kinds.meatbug_queen = game.data.create_kind();
	game.data.kind_set_size(game.special_kinds.meatbug_queen, 2.f);
	game.data.kind_set_speed(game.special_kinds.meatbug_queen, 0.1f);

	game.special_kinds.potion_flower = game.data.create_kind();
	game.data.kind_set_size(game.special_kinds.potion_flower, 0.1f);
	game.data.kind_set_speed(game.special_kinds.potion_flower, 0.f);

	game.special_kinds.tree = game.data.create_kind();
	game.data.kind_set_size(game.special_kinds.tree, 0.2f);
	game.data.kind_set_speed(game.special_kinds.tree, 0.f);

	game.data.force_create_food_hierarchy(game.special_kinds.human, game.special_kinds.meatbug);
	game.data.force_create_food_hierarchy(game.special_kinds.human, rat);
	game.data.force_create_food_hierarchy(rat, game.special_kinds.meatbug);

	{
		game.personality.hunter = game.data.create_ai_model();
		game.data.ai_model_set_stockpile_target(game.personality.hunter, game.potion, 7);
		game.data.ai_model_set_stockpile_target(game.personality.hunter, game.prepared_food, 3);
	}

	{
		game.personality.shopkeeper = game.data.create_ai_model();
		game.data.for_each_commodity([&](auto commodity){
			if (commodity == game.coins) {
				return;
			}
			game.data.ai_model_set_stockpile_target(game.personality.shopkeeper, commodity, 10);
		});
	}

	{
		game.personality.innkeeper = game.data.create_ai_model();
		game.data.ai_model_set_stockpile_target(game.personality.innkeeper, game.raw_food, 10);
		game.data.ai_model_set_stockpile_target(game.personality.innkeeper, game.prepared_food, 5);
		game.data.ai_model_set_stockpile_target(game.personality.innkeeper, game.potion, 1);
	}

	{
		game.personality.alchemist = game.data.create_ai_model();
		game.data.ai_model_set_stockpile_target(game.personality.alchemist, game.prepared_food, 5);
		game.data.ai_model_set_stockpile_target(game.personality.alchemist, game.potion_material, 10);
	}

	{
		game.personality.weapon_master = game.data.create_ai_model();
		game.data.ai_model_set_stockpile_target(game.personality.weapon_master, game.prepared_food, 5);
		game.data.ai_model_set_stockpile_target(game.personality.weapon_master, game.potion, 1);
	}

	{
		game.personality.herbalist = game.data.create_ai_model();
		game.data.ai_model_set_stockpile_target(game.personality.herbalist, game.prepared_food, 5);
		game.data.ai_model_set_stockpile_target(game.personality.herbalist, game.potion, 1);
	}

	{
		auto deliverer_model = game.data.create_ai_model();
		game.data.ai_model_set_stockpile_target(deliverer_model, game.prepared_food, 5);
		game.data.ai_model_set_stockpile_target(deliverer_model, game.potion, 1);
	}


	// {
	// 	auto deliverer = game.data.create_character();
	// 	game.data.character_set_hp(deliverer, 100);
	// 	game.data.character_set_hp_max(deliverer, 100);
	// }
	for (int i = 0; i < 4; i++) {
		auto hunter = game.data.create_character();
		auto hunter_body = game.data.create_thing();
		game.data.thing_set_kind(hunter_body, game.special_kinds.human);
		game.data.force_create_embodiment(hunter, hunter_body);
		game.data.thing_set_hp(hunter_body, 100);
		game.data.thing_set_hp_max(hunter_body, 100);
		game.data.character_set_ai_type(hunter, game.personality.hunter);
		game.data.character_set_weapon_quality(hunter, 1.f);
		game.data.character_set_inventory(hunter, game.coins, 10);
	}

	{
		auto inn = game.data.create_building();
		game.data.building_set_tile_x(inn, 0);
		game.data.building_set_tile_y(inn, 0);
		game.data.building_set_building_model(inn, game.inn);

		auto innkeeper = game.data.create_character();
		auto innkeeper_body = game.data.create_thing();
		game.data.thing_set_kind(innkeeper_body, game.special_kinds.human);
		game.data.force_create_embodiment(innkeeper, innkeeper_body);
		game.data.character_set_inventory(innkeeper, game.coins, 100);
		game.data.character_set_ai_type(innkeeper, game.personality.innkeeper);
		game.data.character_set_skills(innkeeper, game.skills.cooking, 0.3f);

		game.data.force_create_ownership(innkeeper, inn);
	}
	{
		auto shop = game.data.create_building();
		game.data.building_set_tile_x(shop, 3);
		game.data.building_set_tile_y(shop, 3);
		game.data.building_set_building_model(shop, game.shop);
		auto shop_owner = game.data.create_character();
		auto body = game.data.create_thing();
		game.data.thing_set_kind(body, game.special_kinds.human);
		game.data.force_create_embodiment(shop_owner, body);
		game.data.character_set_inventory(shop_owner, game.coins, 100);
		game.data.character_set_ai_type(shop_owner, game.personality.shopkeeper);

		game.data.force_create_ownership(shop_owner, shop);
	}
	{
		auto shop_weapons = game.data.create_building();
		game.data.building_set_tile_x(shop_weapons, 3);
		game.data.building_set_tile_y(shop_weapons, 0);
		game.data.building_set_building_model(shop_weapons, game.shop_weapon);
		auto weapon_master = game.data.create_character();
		auto body = game.data.create_thing();
		game.data.thing_set_kind(body, game.special_kinds.human);
		game.data.force_create_embodiment(weapon_master, body);
		game.data.character_set_inventory(weapon_master, game.coins, 10);
		game.data.character_set_ai_type(weapon_master, game.personality.weapon_master);

		game.data.force_create_ownership(weapon_master, shop_weapons);
	}

	for (int i = 0; i < 2; i++) {
		auto alchemist = game.data.create_character();
		auto body = game.data.create_thing();
		game.data.thing_set_kind(body, game.special_kinds.human);
		game.data.force_create_embodiment(alchemist, body);
		game.data.character_set_inventory(alchemist, game.coins, 100);
		game.data.character_set_ai_type(alchemist, game.personality.alchemist);
	}

	for (int i = 0; i < 2; i++) {
		auto herbalist = game.data.create_character();
		auto body = game.data.create_thing();
		game.data.thing_set_kind(body, game.special_kinds.human);
		game.data.force_create_embodiment(herbalist, body);
		game.data.character_set_ai_type(herbalist, game.personality.herbalist);
	}

	game.data.for_each_character([&](auto cid) {
		game.data.for_each_commodity([&](auto commodity) {
			game.data.character_set_price_belief_buy(cid, commodity, 1.f);
			game.data.character_set_price_belief_sell(cid, commodity, 1.f);
		});

		// select initial favorite shops
		game.data.for_each_building([&](auto candidate_building){
			auto candidate = game.data.building_get_owner_from_ownership(candidate_building);
			dcon::ai_model_id model = game.data.character_get_ai_type(candidate);
			if (model == game.personality.innkeeper) {
				game.data.character_set_favourite_inn(cid, candidate_building);
			}
			if (model == game.personality.weapon_master) {
				game.data.character_set_favourite_shop_weapons(cid, candidate_building);
			}
			if (model == game.personality.shopkeeper) {
				game.data.character_set_favourite_shop(cid, candidate_building);
			}
		});
	});

	for (int i = 0; i < 50; i++) {
		auto queen = game.data.create_thing();
		game.data.thing_set_kind(queen, game.special_kinds.meatbug_queen);
		game.data.thing_set_hp(queen, 300);
		game.data.thing_set_hp_max(queen, 30);
		game.data.thing_set_x(queen, game.uniform(game.rng) * 100.f - 50.f);
		game.data.thing_set_y(queen, game.uniform(game.rng) * 100.f - 50.f);
		game.data.thing_set_direction(queen, game.uniform(game.rng) * glm::pi<float>() * 2);
	}

	// spawn trees

	auto forest_x = 30.f;
	auto forest_y = 30.f;

	for (int i = 0; i < 50; i++) {
		auto thing = game.data.create_thing();
		game.data.thing_set_kind(thing, game.special_kinds.tree);
		game.data.thing_set_hp(thing, 30);
		game.data.thing_set_hp_max(thing, 30);
		game.data.thing_set_x(thing, game.normal(game.rng) * 10.f  + forest_x);
		game.data.thing_set_y(thing, game.normal(game.rng) * 10.f  + forest_y);
		game.data.thing_set_direction(thing, game.uniform(game.rng) * glm::pi<float>() * 2);
	}
}

void update(state& game) {
	game.data.for_each_character([&](auto cid) {
		auto model = game.data.character_get_ai_type(cid);
		if (model == game.personality.hunter) {
			ai::update::hunter(game, cid);
		} else if (model == game.personality.alchemist) {
			ai::update::alchemist(game, cid);
		} else if (model == game.personality.herbalist) {
			gather_potion_material(game, cid);

			auto timer = game.data.character_get_action_timer(cid);
			game.data.character_set_action_timer(cid, timer + 1);
		} else if (model == game.personality.innkeeper) {
			auto material_cost = game.data.character_get_price_belief_buy(cid, game.raw_food);
			auto production_cost = game.data.character_get_price_belief_sell(cid, game.prepared_food);
			if (
				game.data.character_get_inventory(cid, game.raw_food) >= 1.f
				&& production_cost > material_cost
			) {
				printf("make food\n");
				prepare_food(game, cid);
			}

			auto timer = game.data.character_get_action_timer(cid);
			game.data.character_set_action_timer(cid, timer + 1);
		}
	});

	game.data.for_each_thing([&](auto id) {
		auto hunger = game.data.thing_get_hunger(id);
		game.data.thing_set_hunger(id, hunger + 1);
	});

	// trade:

	// ai logic would be very simple:
	// sell things you don't desire yourself
	// buy things you desire and miss

	// currently we can buy things only from the favourite shop:

	for (int round = 0; round < 3; round++) {
		game.data.for_each_character([&](auto cid) {
			auto body =  game.data.character_get_body_from_embodiment(cid);
			game.data.for_each_commodity([&](auto commodity) {
				if (commodity == game.coins) {
					return;
				}
				if (commodity == game.weapon_service) {
					return;
				}

				auto shop = game.data.character_get_favourite_shop(cid);
				auto action = game.ai.shopping;
				if (commodity == game.prepared_food) {
					shop = game.data.character_get_favourite_inn(cid);
					action = game.ai.getting_food;
				}

				if (
					game.data.character_get_ai_type(cid) == game.personality.hunter
					|| game.data.character_get_ai_type(cid) == game.personality.alchemist
				) {
					auto guest_in = game.data.thing_get_guest_location_from_guest(body);
					if (game.data.character_get_action_type(cid) != action) {
						return;
					}

					if (guest_in != shop) {
						return;
					}
				}

				auto shop_owner = game.data.building_get_owner_from_ownership(shop);
				if (cid == shop_owner) {
					return;
				}
				// auto desire = game.data.character_get_hunger(cid, commodity);
				auto ai = game.data.character_get_ai_type(cid);
				auto target = game.data.ai_model_get_stockpile_target(ai, commodity);
				auto inventory = game.data.character_get_inventory(cid, commodity);
				auto in_stock = game.data.character_get_inventory(shop_owner, commodity);
				auto coins = game.data.character_get_inventory(cid, game.coins);
				auto desired_price_buy = game.data.character_get_price_belief_buy(cid, commodity);
				auto desired_price_sell = game.data.character_get_price_belief_sell(cid, commodity);
				auto coins_shop = game.data.character_get_inventory(shop_owner, game.coins);
				auto price_shop_sell = game.data.character_get_price_belief_sell(shop_owner, commodity);
				auto price_shop_buy = game.data.character_get_price_belief_buy(shop_owner, commodity);

				auto bottom_price = game.data.character_get_price_belief_buy(cid, game.prepared_food) / 5.f;


				if (target > inventory) {
					// printf("I need this? %d %f %f %f\n", commodity.index(), desired_price_buy, price_shop_sell, in_stock );
					if (desired_price_buy >= price_shop_sell && in_stock >= 1.f && coins >= price_shop_sell) {
						printf("I am buying %s\n", game::get_name(game, commodity).c_str());
						transaction(game, shop_owner, cid, commodity, 1.f);
						transaction(game, cid, shop_owner, game.coins, price_shop_sell);
					} else if (desired_price_buy >= price_shop_sell && coins >= price_shop_sell) {
						printf("I am ordering %s\n", game::get_name(game, commodity).c_str());
						auto delayed = game.data.get_delayed_transaction_by_transaction_pair(cid, shop_owner);
						bool already_indebted = false;
						if (delayed) {
							auto A = game.data.delayed_transaction_get_members(delayed, 0);
							auto B = game.data.delayed_transaction_get_members(delayed, 1);
							auto debt = game.data.delayed_transaction_get_balance(delayed, commodity);
							auto mult = 1.f;
							if (A != shop_owner) {
								mult = -1.f;
							}
							if (debt * mult > 3) {
								already_indebted = true;
							}
						}
						delayed_transaction(game, shop_owner, cid, commodity, 1.f);
						transaction(game, cid, shop_owner, game.coins, price_shop_sell);
						// if (!already_indebted) {
						// } else {
						// 	printf("But I have already ordered a lot\n");
						// }
					} else if (desired_price_buy >= price_shop_sell && in_stock >= 1.f) {
						printf("I am buying %s with a loan\n", game::get_name(game, commodity).c_str());
						transaction(game, shop_owner, cid, commodity, 1.f);
						delayed_transaction(game, cid, shop_owner, game.coins, price_shop_sell);
					}
				}

				if (target < inventory && price_shop_buy > bottom_price) {
					// printf("I do not need this? %d %f %f %f\n", commodity.index(), desired_price_sell, price_shop_buy, in_stock );

					if (price_shop_buy >= desired_price_sell && inventory >= 1.f && coins_shop >= price_shop_buy) {
						printf("I am selling %s\n", game::get_name(game, commodity).c_str());
						transaction(game, cid, shop_owner, commodity, 1.f);
						transaction(game, shop_owner, cid, game.coins, price_shop_buy);
					} else if (price_shop_buy >= desired_price_sell && inventory >= 1.f) {
						printf("I am selling %s for promise of future payment\n", game::get_name(game, commodity).c_str());
						transaction(game, cid, shop_owner, commodity, 1.f);
						delayed_transaction(game, shop_owner, cid, game.coins, price_shop_buy);
					}
				}


				// convergence of beliefs during interaction:

				auto alpha = 0.01f;
				{
					auto shift = price_shop_sell - desired_price_buy;
					game.data.character_set_price_belief_buy(cid, commodity, desired_price_buy + shift * alpha);
				}
				{
					auto shift = price_shop_buy - desired_price_sell;
					game.data.character_set_price_belief_sell(cid, commodity, desired_price_sell + shift * alpha);
				}
			});
		});
	}

	// fulfill promises:
	game.data.for_each_delayed_transaction([&](auto delayed) {
		auto A = game.data.delayed_transaction_get_members(delayed, 0);
		auto B = game.data.delayed_transaction_get_members(delayed, 1);

		game.data.for_each_commodity([&](auto commodity) {
			auto debt = game.data.delayed_transaction_get_balance(delayed, commodity);
			if (debt > 0) {
				auto inv = game.data.character_get_inventory(A, commodity);
				if (inv >= 1) {
					transaction(game, A, B, commodity, 1.f);
					delayed_transaction(game, A, B, commodity, -1.f);
				}
			} else if (debt < 0) {
				auto inv = game.data.character_get_inventory(B, commodity);
				if (inv >= 1) {
					transaction(game, B, A, commodity, 1.f);
					delayed_transaction(game, B, A, commodity, -1.f);
				}
			}
		});
	});

	game.data.for_each_character([&](auto cid) {
		auto body = game.data.character_get_body_from_embodiment(cid);
		if (game.data.thing_get_hunger(body) > 250.f) {
			eat(game, cid);
		}
		drink_potion(game, cid);
	});

	// event: if commodity is not selling well: reduce sell price:
	game.price_update_tick++;
	if (game.price_update_tick > 4) {
		game.price_update_tick = 0;
	}
	if (game.price_update_tick  == 0) {
		game.data.for_each_character([&](auto cid) {
			if (game.data.character_get_ai_type(cid) == game.personality.weapon_master) {
				auto cost = game.data.character_get_price_belief_sell(cid, game.weapon_service);
				game.data.character_set_price_belief_sell(cid, game.weapon_service, cost * 0.99f);
			}
			game.data.for_each_commodity([&](auto commodity) {
				if (commodity == game.coins) {
					return;
				}
				if (commodity == game.weapon_service) {
					return;
				}

				auto inventory = game.data.character_get_inventory(cid, commodity);
				auto ai = game.data.character_get_ai_type(cid);
				auto target = game.data.ai_model_get_stockpile_target(ai, commodity);

				auto spoilage = (float)(int)(inventory / 20);

				if (inventory > target * 2) {
					auto price_decay_sell = exp(-inventory / target * 0.05f);
					auto price_decay_buy =exp(-inventory / target * 0.1f);

					auto price_shop_sell = game.data.character_get_price_belief_sell(cid, commodity);
					game.data.character_set_price_belief_sell(cid, commodity, std::max(0.00001f,  price_shop_sell * price_decay_sell));

					auto price_shop_buy = game.data.character_get_price_belief_buy(cid, commodity);
					game.data.character_set_price_belief_buy(cid, commodity, std::max(0.00001f, price_shop_buy * price_decay_buy));
				}

				// if something is spoiling, we want to get rid of it
				if (spoilage > 0) {
					auto price_decay_sell = exp(-spoilage * 0.05f);
					auto price_decay_buy =exp(-spoilage * 0.1f);
					// spoilage
					game.data.character_set_inventory(cid, commodity, inventory - (float)(int)(inventory / 20));

					auto price_shop_sell = game.data.character_get_price_belief_sell(cid, commodity);
					game.data.character_set_price_belief_sell(cid, commodity, 0.00001f + price_shop_sell * price_decay_sell);

					auto price_shop_buy = game.data.character_get_price_belief_buy(cid, commodity);
					game.data.character_set_price_belief_buy(cid, commodity, 0.00001f + price_shop_buy * price_decay_buy);
				}

				auto coins = game.data.character_get_inventory(cid, game.coins);

				if (inventory < target) {
					auto lack = (float)(target - inventory) / (float)target;
					auto mult_buy = exp(lack  * 0.05f);

					auto price_shop_buy = game.data.character_get_price_belief_buy(cid, commodity);
					game.data.character_set_price_belief_buy(cid, commodity, std::min(coins + 10.f,  price_shop_buy * mult_buy));
					auto price_shop_sell = game.data.character_get_price_belief_sell(cid, commodity);

					if (game.data.character_get_ai_type(cid) == game.personality.shopkeeper) {
						game.data.character_set_price_belief_sell(cid, commodity,  price_shop_buy * mult_buy);
					} else {
						game.data.character_set_price_belief_sell(cid, commodity, std::min(coins + 10.f,  price_shop_sell * mult_buy));
					}
				}
			});
		});
	}


	game.data.execute_parallel_over_thing([&](auto critter){
		auto kind = game.data.thing_get_kind(critter);
		auto speed = game.data.kind_get_speed(kind);

		auto soul = game.data.thing_get_embodier_from_embodiment(critter);
		auto following = game.data.thing_get_follow_target_as_follower(critter);
		auto target = game.data.follow_target_get_followed(following);
		auto x = game.data.thing_get_x(critter);
		auto y = game.data.thing_get_y(critter);

		auto cx = ve::select(target == dcon::thing_id{}, x, game.data.thing_get_x(target));
		auto cy = ve::select(target == dcon::thing_id{}, y, game.data.thing_get_y(target));

		auto alpha = game.data.thing_get_direction(critter);
		auto dx = ve::apply([&](float alpha_v){return sin(alpha_v);}, alpha);
		auto dy = ve::apply([&](float alpha_v){return -cos(alpha_v);}, alpha);

		auto fdx = cx - x;
		auto fdy = cy - y;

		auto fn = ve::sqrt(fdx * fdx + fdy * fdy);

		fdx = ve::select(fn > speed, fdx / fn, fdx) * 0.05f;
		fdy = ve::select(fn > speed, fdy / fn, fdy) * 0.05f;

		dx = ve::select(soul == dcon::character_id{}, dx * 0.05f, 0.f);
		dy = ve::select(soul == dcon::character_id{}, dy * 0.05f, 0.f);
		game.data.thing_set_x(critter, x + (dx + fdx) * speed);
		game.data.thing_set_y(critter, y + (dy + fdy) * speed);
	});

	std::vector<dcon::thing_id> will_give_birth {};

	game.data.for_each_thing([&](auto critter){
		auto soul = game.data.thing_get_embodier_from_embodiment(critter);
		if (!soul) {
			auto alpha = game.data.thing_get_direction(critter);
			game.data.thing_set_direction(critter, alpha + 0.1f * game.uniform(game.rng) - 0.05f);
		}

		auto kind = game.data.thing_get_kind(critter);
		if (kind == game.special_kinds.meatbug_queen) {
			if (game.uniform(game.rng) < 0.01f) {
				will_give_birth.push_back(critter);
			}
		}
	});

	for (auto& mother : will_give_birth) {
		auto child = game.data.create_thing();
		game.data.thing_set_kind(child, game.special_kinds.meatbug);
		game.data.thing_set_hp(child, 30);
		game.data.thing_set_hp_max(child, 30);
		game.data.thing_set_x(child, game.data.thing_get_x(mother));
		game.data.thing_set_y(child, game.data.thing_get_y(mother));
		game.data.force_create_follow_target(child, mother);
	}
}

}


void APIENTRY glDebugOutput(
	GLenum source,
	GLenum type,
	unsigned int id,
	GLenum severity,
	GLsizei length,
	const char *message,
	const void *userParam
) {
	// ignore non-significant error/warning codes
	if(id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

	std::cout << "---------------" << std::endl;
	std::cout << "Debug message (" << id << "): " <<  message << std::endl;

	switch (source)
	{
		case GL_DEBUG_SOURCE_API:             std::cout << "Source: API"; break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   std::cout << "Source: Window System"; break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER: std::cout << "Source: Shader Compiler"; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:     std::cout << "Source: Third Party"; break;
		case GL_DEBUG_SOURCE_APPLICATION:     std::cout << "Source: Application"; break;
		case GL_DEBUG_SOURCE_OTHER:           std::cout << "Source: Other"; break;
	} std::cout << std::endl;

	switch (type)
	{
		case GL_DEBUG_TYPE_ERROR:               std::cout << "Type: Error"; break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: std::cout << "Type: Deprecated Behaviour"; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  std::cout << "Type: Undefined Behaviour"; break;
		case GL_DEBUG_TYPE_PORTABILITY:         std::cout << "Type: Portability"; break;
		case GL_DEBUG_TYPE_PERFORMANCE:         std::cout << "Type: Performance"; break;
		case GL_DEBUG_TYPE_MARKER:              std::cout << "Type: Marker"; break;
		case GL_DEBUG_TYPE_PUSH_GROUP:          std::cout << "Type: Push Group"; break;
		case GL_DEBUG_TYPE_POP_GROUP:           std::cout << "Type: Pop Group"; break;
		case GL_DEBUG_TYPE_OTHER:               std::cout << "Type: Other"; break;
	} std::cout << std::endl;

	switch (severity)
	{
		case GL_DEBUG_SEVERITY_HIGH:         std::cout << "Severity: high"; break;
		case GL_DEBUG_SEVERITY_MEDIUM:       std::cout << "Severity: medium"; break;
		case GL_DEBUG_SEVERITY_LOW:          std::cout << "Severity: low"; break;
		case GL_DEBUG_SEVERITY_NOTIFICATION: std::cout << "Severity: notification"; break;
	} std::cout << std::endl;
	std::cout << std::endl;
}

struct base_triangle {
	GLuint vao;
	GLuint vbo;
};

std::string_view opengl_get_error_name(GLenum t) {
	switch(t) {
		case GL_INVALID_ENUM:
			return "GL_INVALID_ENUM";
		case GL_INVALID_VALUE:
			return "GL_INVALID_VALUE";
		case GL_INVALID_OPERATION:
			return "GL_INVALID_OPERATION";
		case GL_INVALID_FRAMEBUFFER_OPERATION:
			return "GL_INVALID_FRAMEBUFFER_OPERATION";
		case GL_OUT_OF_MEMORY:
			return "GL_OUT_OF_MEMORY";
		case GL_STACK_UNDERFLOW:
			return "GL_STACK_UNDERFLOW";
		case GL_STACK_OVERFLOW:
			return "GL_STACK_OVERFLOW";
		case GL_NO_ERROR:
			return "GL_NO_ERROR";
		default:
			return "Unknown";
	}
}
std::string to_string(std::string_view str) {
	return std::string(str.begin(), str.end());
}
void opengl_error_print(std::string message) {
	std::string full_message = message;
	full_message += "\n";
	full_message += opengl_get_error_name(glGetError());
	printf("%s\n", ("OpenGL error:" + full_message).c_str());
}
void assert_no_errors() {
	auto error = glGetError();
	if (error != GL_NO_ERROR) {
		auto message = opengl_get_error_name(error);
		printf("%s\n", (to_string(message)).c_str());
		assert(false);
	}
}

const std::string read_shader(const std::string path) {
	std::string shader_source;
	std::ifstream shader_file;

	shader_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);

	try {
		shader_file.open(path);
		std::stringstream shader_source_stream;
		shader_source_stream << shader_file.rdbuf();
		shader_file.close();
		shader_source = shader_source_stream.str();
	} catch (std::ifstream::failure& e) {
		throw std::runtime_error(e);
	}

	return shader_source;
}

GLuint create_shader(GLenum type, const char *source) {
	GLuint result = glCreateShader(type);
	glShaderSource(result, 1, &source, nullptr);
	glCompileShader(result);
	GLint status;
	glGetShaderiv(result, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE) {
		GLint info_log_length;
		glGetShaderiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
		std::string info_log(info_log_length, '\0');
		glGetShaderInfoLog(result, info_log.size(), nullptr, info_log.data());
		throw std::runtime_error("Shader compilation failed: " + info_log);
	}
	return result;
}

template <typename ... Shaders>
GLuint create_program(Shaders ... shaders)
{
	GLuint result = glCreateProgram();
	(glAttachShader(result, shaders), ...);
	glLinkProgram(result);

	GLint status;
	glGetProgramiv(result, GL_LINK_STATUS, &status);
	if (status != GL_TRUE)
	{
		GLint info_log_length;
		glGetProgramiv(result, GL_INFO_LOG_LENGTH, &info_log_length);
		std::string info_log(info_log_length, '\0');
		glGetProgramInfoLog(result, info_log.size(), nullptr, info_log.data());
		throw std::runtime_error("Program linkage failed: " + info_log);
	}

	return result;
}


void glew_fail(std::string_view message, GLenum error) {
	throw std::runtime_error(to_string(message) + reinterpret_cast<const char *>(glewGetErrorString(error)));
}

void error_callback(int error, const char* description)
{
	fprintf(stderr, "Error: %s\n", description);
}

static int current_move_x = 0;
static int current_move_y = 0;

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	}

	if (key == GLFW_KEY_UP) {
		if (action == GLFW_PRESS) {
			current_move_y += 1;
		} else if (action == GLFW_RELEASE) {
			current_move_y -= 1;
		}
	}

	if (key == GLFW_KEY_DOWN) {
		if (action == GLFW_PRESS) {
			current_move_y -= 1;
		} else if (action == GLFW_RELEASE) {
			current_move_y += 1;
		}
	}

	if (key == GLFW_KEY_LEFT) {
		if (action == GLFW_PRESS) {
			current_move_x -= 1;
		} else if (action == GLFW_RELEASE) {
			current_move_x += 1;
		}
	}

	if (key == GLFW_KEY_RIGHT) {
		if (action == GLFW_PRESS) {
			current_move_x += 1;
		} else if (action == GLFW_RELEASE) {
			current_move_x -= 1;
		}
	}
}

constexpr int triangle_size = 9;
static std::vector<game::vertex> triangle_mesh = {
	{{0.2f, 0.f, 0.5f}, {0.f, 0.f, 1.f}, {}},
	{{0.f, 0.5f, 0.5f}, {0.f, 0.f, 1.f}, {}},
	{{-0.2f, 0.f, 0.5f}, {0.f, 0.f, 1.f}, {}}
};

base_triangle create_triangle() {

	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, triangle_mesh.size() * sizeof(game::vertex), triangle_mesh.data(), GL_STATIC_DRAW);

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(game::vertex),  reinterpret_cast<void*>(0));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(game::vertex),  reinterpret_cast<void*>(sizeof(float) * 3));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(game::vertex),  reinterpret_cast<void*>(sizeof(float) * 3 + sizeof(float) * 3));

	return {vao, vbo};
}

static std::vector<game::vertex> tree_mesh;

base_triangle create_tree() {
	tree_mesh.clear();
	int quality = 4;
	float angle_step = 2.f * glm::pi<float>() / (float)quality;

	for (int i = 0; i < quality; i++) {
		float angle = (float)(i) * angle_step;
		float next_angle = (float)(i + 1) * angle_step;
		glm::vec3 left_bottom = {cos(angle), sin(angle), 0.f};
		glm::vec3 right_bottom = {cos(next_angle), sin(next_angle),  0.f};
		glm::vec3 top = {0.f, 0.f, 20.f};

		auto top_to_left = left_bottom - top;
		auto top_to_right = right_bottom - top;
		auto normal = glm::cross(top_to_left, top_to_right);

		tree_mesh.push_back({{left_bottom.x, left_bottom.y, left_bottom.z}, normal, {}});
		tree_mesh.push_back({{right_bottom.x, right_bottom.y, right_bottom.z}, normal, {}});
		tree_mesh.push_back({{top.x, top.y, top.z}, normal, {}});
	}

	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, tree_mesh.size() * sizeof(game::vertex), tree_mesh.data(), GL_STATIC_DRAW);

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(game::vertex),  reinterpret_cast<void*>(0));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(game::vertex),  reinterpret_cast<void*>(sizeof(float) * 3));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(game::vertex),  reinterpret_cast<void*>(sizeof(float) * 3 + sizeof(float) * 3));

	return {vao, vbo};
}

void generate_mesh_from_heightmap(game::map_state& data, int chunk_x, int chunk_y) {

	auto chunk_index = (chunk_x + game::WORLD_RADIUS) * game::WORLD_SIZE + (chunk_y + game::WORLD_RADIUS);


	auto& mesh = data.meshes[chunk_index].data;

	glm::vec3 up = {0.f, 0.f, 1.f};
	glm::vec3 n_left = {-1.f, 0.f, 0.f};
	glm::vec3 n_forward = {0.f, -1.f, 0.f};

	for (int i = 0; i < game::CHUNK_AREA; i++) {
		int ix = i / game::CHUNK_SIZE;
		int iy = i - ix * game::CHUNK_SIZE;

		float x = (float)ix + chunk_x * game::CHUNK_SIZE;
		float y = (float)iy + chunk_y * game::CHUNK_SIZE;
		float z = game::get_height(data, x, y);

		mesh.push_back({{x, y, z}, up, {}});
		mesh.push_back({{x + 1.f, y, z}, up, {}});
		mesh.push_back({{x, y + 1.f, z}, up, {}});

		mesh.push_back({{x, y + 1.f, z}, up, {}});
		mesh.push_back({{x + 1.f, y, z}, up, {}});
		mesh.push_back({{x + 1.f, y + 1.f, z}, up, {}});

		auto here = (float)game::get_height(data, x, y);
		if (x > -game::WORLD_RADIUS * game::CHUNK_SIZE) {
			auto left = (float) game::get_height(data, x - 1, y);
			if (left < here) {
				float z_n = (float)(left);

				mesh.push_back({{x, y, z_n}, n_left, {}});
				mesh.push_back({{x, y, z}, n_left, {}});
				mesh.push_back({{x, y + 1.f, z}, n_left, {}});

				mesh.push_back({{x, y, z_n}, n_left, {}});
				mesh.push_back({{x, y + 1.f, z}, n_left, {}});
				mesh.push_back({{x, y + 1.f, z_n}, n_left, {}});
			}
		}

		if (x + 1 < game::WORLD_RADIUS * game::CHUNK_SIZE) {
			auto there = (float)game::get_height(data, x + 1, y);
			if (there < here) {
				float z_n = (float)(there);

				mesh.push_back({{x + 1.f, y, z}, -n_left, {}});
				mesh.push_back({{x + 1.f, y, z_n}, -n_left, {}});
				mesh.push_back({{x + 1.f, y + 1.f, z}, -n_left, {}});

				mesh.push_back({{x + 1.f, y + 1.f, z}, -n_left, {}});
				mesh.push_back({{x + 1.f, y, z_n}, -n_left, {}});
				mesh.push_back({{x + 1.f, y + 1.f, z_n}, -n_left, {}});
			}
		}

		if (y > -game::WORLD_RADIUS * game::CHUNK_SIZE) {
			auto there = (float)game::get_height(data, x, y - 1);
			if (there < here) {
				float z_n = (float)(there);


				mesh.push_back({{x, y, z}, n_forward, {}});
				mesh.push_back({{x, y, z_n}, n_forward, {}});
				mesh.push_back({{x + 1.f, y, z}, n_forward, {}});

				mesh.push_back({{x + 1.f, y, z}, n_forward, {}});
				mesh.push_back({{x, y, z_n}, n_forward, {}});
				mesh.push_back({{x + 1.f, y, z_n}, n_forward, {}});
			}
		}

		if (y + 1 < game::WORLD_RADIUS * game::CHUNK_SIZE) {
			auto there = (float)game::get_height(data, x, y + 1);
			if (there < here) {
				float z_n = (float)(there);

				mesh.push_back({{x, y + 1.f, z_n}, -n_forward, {}});
				mesh.push_back({{x, y + 1.f, z}, -n_forward, {}});
				mesh.push_back({{x + 1.f, y + 1.f, z}, -n_forward, {}});

				mesh.push_back({{x, y + 1.f, z_n}, -n_forward, {}});
				mesh.push_back({{x + 1.f, y + 1.f, z}, -n_forward, {}});
				mesh.push_back({{x + 1.f, y + 1.f, z_n}, -n_forward, {}});
			}
		}
	}

	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(game::vertex), mesh.data(), GL_STATIC_DRAW);

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(game::vertex),  reinterpret_cast<void*>(0));

	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(game::vertex),  reinterpret_cast<void*>(sizeof(float) * 3));

	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(game::vertex),  reinterpret_cast<void*>(sizeof(float) * 3 + sizeof(float) * 3));

	data.meshes[chunk_index].vao = vao;
	data.meshes[chunk_index].vbo = vbo;
}


namespace geometry {

namespace screen_relative {
struct point {
	glm::vec2 data;
};
}

namespace world{
struct point {
	glm::vec2 data;
};
}

namespace screen_opengl {
struct point {
	glm::vec2 data;
};

point convert(const geometry::world::point in, const geometry::world::point camera, const float aspect_ratio, const float zoom) {
	auto result = (in.data - camera.data) * glm::vec2 { aspect_ratio, 1.f } * zoom;
	return {
		result
	};
}
}

}

struct shader_2d_data {
	GLuint shift;
	GLuint zoom;
	GLuint aspect_ratio;
};

void render_triangle(
	base_triangle& object,
	shader_2d_data& location,
	geometry::screen_opengl::point& position
) {
	glBindVertexArray(object.vao);
	glUniform2f(location.shift, position.data.x, position.data.y);
	glDrawArrays(
		GL_TRIANGLES,
		0,
		3
	);
}

game::state world {};

int main(void)
{
	glfwSetErrorCallback(error_callback);
	if (!glfwInit())
		return -1;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);

	GLFWwindow* window;
	float main_scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
	window = glfwCreateWindow(1280 * main_scale, 960 * main_scale, "009", NULL, NULL);
	if (!window)
	{
		glfwTerminate();
		return -1;
	}
	glfwSetKeyCallback(window, key_callback);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	// ImGui::StyleColorsDark();
	ImGui::StyleColorsLight();

	geometry::world::point camera { {0.f, 0.f} };
	float zoom = 0.1f;

	ImGuiStyle& style = ImGui::GetStyle();
	style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
	style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)


	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330 core");

	GLenum err = glewInit();
	if (GLEW_OK != err) {
		fprintf(stderr, "Error: %s\n", glewGetErrorString(err));
	}
	fprintf(stdout, "Status: Using GLEW %s\n", glewGetString(GLEW_VERSION));

	// GLEW validation
	if (auto result = glewInit(); result != GLEW_NO_ERROR)
		glew_fail("glewInit: ", result);
	if (!GLEW_VERSION_3_3)
		throw std::runtime_error("OpenGL 3.3 is not supported");

	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

	// illumination settings
	glm::vec3 light_color = glm::vec3(1.f, 0.8f, 0.3f);
	glm::vec3 ambient = glm::vec3(0.2f, 0.6f, 0.8f);
	glClearColor(ambient.x, ambient.y, ambient.z, 0.f);


	auto triangle = create_triangle();
	auto tree = create_tree();


	// setting up a basic shader
	std::string shader_2d_vertex_path = "./shaders/basic_shader_flat.vert";
	std::string shader_2d_fragment_path = "./shaders/basic_shader_flat.frag";
	std::string shader_2d_vertex_source = read_shader( shader_2d_vertex_path );
	std::string shader_2d_fragment_source = read_shader( shader_2d_fragment_path );
	auto shader_2d = create_program(
		create_shader(GL_VERTEX_SHADER, shader_2d_vertex_source.c_str()),
		create_shader(GL_FRAGMENT_SHADER, shader_2d_fragment_source.c_str())
	);
	shader_2d_data shader_2d_loc {};
	shader_2d_loc.shift = glGetUniformLocation(shader_2d, "shift");
	shader_2d_loc.zoom = glGetUniformLocation(shader_2d, "zoom");
	shader_2d_loc.aspect_ratio = glGetUniformLocation(shader_2d, "aspect_ratio");


	// setting up a basic shader
	std::string basic_shader_vertex_path = "./shaders/basic_shader_meshes.vert";
	std::string basic_shader_fragment_path = "./shaders/basic_shader_meshes.frag";

	std::string vertex_shader_source = read_shader( basic_shader_vertex_path );
	std::string fragment_shader_source = read_shader( basic_shader_fragment_path );

	auto basic_shader = create_program(
		create_shader(GL_VERTEX_SHADER, vertex_shader_source.c_str()),
		create_shader(GL_FRAGMENT_SHADER, fragment_shader_source.c_str())
	);

	GLuint model_location = glGetUniformLocation(basic_shader, "model");
	GLuint view_location = glGetUniformLocation(basic_shader, "view");
	GLuint projection_location = glGetUniformLocation(basic_shader, "projection");
	GLuint albedo_location = glGetUniformLocation(basic_shader, "albedo");
	GLuint color_location = glGetUniformLocation(basic_shader, "color");
	GLuint use_texture_location = glGetUniformLocation(basic_shader, "use_texture");
	GLuint light_direction_location = glGetUniformLocation(basic_shader, "light_direction");
	GLuint camera_position_location = glGetUniformLocation(basic_shader, "camera_position");
	GLuint light_color_location = glGetUniformLocation(basic_shader, "light_color");
	GLuint ambient_location = glGetUniformLocation(basic_shader, "ambient");
	GLuint bones_location = glGetUniformLocation(basic_shader, "bones");

	GLuint shadow_layers_location = glGetUniformLocation(basic_shader, "shadow_layers");
	GLuint shadow_map_location = glGetUniformLocation(basic_shader, "shadow_map");
	GLuint render_shadow_transform_location = glGetUniformLocation(basic_shader, "shadow_transform");


	std::string shadow_vertex_path = "./shaders/shadow.vert";
	std::string shadow_fragment_path = "./shaders/shadow.frag";
	std::string shadow_vertex_shader_source = read_shader( shadow_vertex_path );
	std::string shadow_fragment_shader_source = read_shader( shadow_fragment_path );
	auto shadow_vertex_shader = create_shader(GL_VERTEX_SHADER, shadow_vertex_shader_source.c_str());
	auto shadow_fragment_shader = create_shader(GL_FRAGMENT_SHADER, shadow_fragment_shader_source.c_str());
	auto shadow_program = create_program(shadow_vertex_shader, shadow_fragment_shader);
	GLuint shadow_model_location = glGetUniformLocation(shadow_program, "model");
	GLuint shadow_transform_location = glGetUniformLocation(shadow_program, "transform");

	GLsizei shadow_map_resolution = 2048;
	const GLsizei shadow_layers = 1;

	GLuint shadow_map;
	glGenTextures(1, &shadow_map);
	glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_map);
	glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage3D(
		GL_TEXTURE_2D_ARRAY,
		0,
		GL_RG32F,
		shadow_map_resolution, shadow_map_resolution, shadow_layers,
		0,
		GL_RGBA, GL_FLOAT, nullptr
	);

	GLuint shadow_fbo [shadow_layers];
	GLuint shadow_renderbuffers [shadow_layers];

	for (GLsizei i = 0; i < shadow_layers; i ++) {
		glGenFramebuffers(1, &shadow_fbo[i]);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadow_fbo[i]);
		glFramebufferTextureLayer(
		GL_DRAW_FRAMEBUFFER,
		GL_COLOR_ATTACHMENT0,
		shadow_map, 0, i
		);

		if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		throw std::runtime_error("Incomplete framebuffer!");

		glGenRenderbuffers(1, &shadow_renderbuffers[i]);
		glBindRenderbuffer(GL_RENDERBUFFER, shadow_renderbuffers[i]);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, shadow_map_resolution, shadow_map_resolution);
		glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, shadow_renderbuffers[i]);
	}

	float albedo_world[] = {0.4f, 0.5f, 0.8f};
	float albedo_character[] = {0.9f, 0.5f, 0.6f};
	float albedo_critter[] = {0.5f, 0.1f, 0.1f};


	std::default_random_engine rng;
	std::uniform_real_distribution<float> uniform{0.0, 1.0};
	std::normal_distribution<float> normal_d{0.f, 0.1f};
	std::normal_distribution<float> size_d{1.f, 0.3f};


	glm::vec3 camera_position{0.f, 0.f, 15.f};

	int width = 1;
	int height = 1;

	assert_no_errors();

	game::init(world);


	world.data.kind_set_vao(world.special_kinds.human, triangle.vao);
	world.data.kind_set_vao(world.special_kinds.meatbug, triangle.vao);
	world.data.kind_set_vao(world.special_kinds.meatbug_queen, triangle.vao);
	world.data.kind_set_vao(world.special_kinds.tree, tree.vao);

	world.data.kind_set_triangles_count(world.special_kinds.human, triangle_mesh.size());
	world.data.kind_set_triangles_count(world.special_kinds.meatbug, triangle_mesh.size());
	world.data.kind_set_triangles_count(world.special_kinds.meatbug_queen, triangle_mesh.size());
	world.data.kind_set_triangles_count(world.special_kinds.tree, tree_mesh.size());

	for (int i = 0; i < game::WORLD_AREA_TILES; i++) {
		world.map.height[i] = 0;
	}
	world.data.for_each_building([&](auto data) {
		auto x = world.data.building_get_tile_x(data);
		auto y = world.data.building_get_tile_y(data);

		game::set_height(world.map, x, y, 1);
	});

	for (int i = 0; i < game::WORLD_AREA; i++) {
		auto x = i / game::WORLD_SIZE;
		auto y = i - x * game::WORLD_SIZE;
		x -= game::WORLD_RADIUS;
		y -= game::WORLD_RADIUS;
		generate_mesh_from_heightmap(world.map, x, y);
	}


	float update_timer = 0.f;


	glm::vec3 light_direction {0.5f, 0.5f, 0.5f};

	glm::vec2 camera_speed = {};
	int tick = 0;
	float data[512] {};

	double last_time = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		tick++;

		double time = glfwGetTime();
		float dt = (float)(time - last_time);
		last_time = time;

		update_timer += dt;

		if (update_timer > 1.f / 60.f) {
			update_timer = 0.f;
			game::update(world);
		}

		camera_speed *= exp(-dt * 10.f);
		camera_speed += glm::vec2(float(current_move_x), float(current_move_y)) * dt;

		camera_position.xy += camera_speed;

		// if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
		// 	ImGui_ImplGlfw_Sleep(10);
		// 	continue;
		// }

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();


		const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();

		glm::vec3 light_direction {cosf(time / 100.f), sinf(time / 100.f), 2.5f};
		glm::vec3 light_z = glm::normalize(light_direction);
		glm::vec3 light_x = glm::normalize(glm::cross(light_z, {0.f, 0.f, 1.f}));
		glm::vec3 light_y = glm::cross(light_x, light_z);

		{
			static float f = 0.0f;
			static int counter = 0;

			ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

			ImGui::Text("Test.");               // Display some text (you can use a format strings too)

			ImGui::SliderFloat3("float", (float*)&light_direction, -1.0f, 1.0f);
			light_direction = glm::normalize(light_direction);

			ImGui::ColorEdit3("clear color", (float*)&ambient); // Edit 3 floats representing a color

			if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
				counter++;
			ImGui::SameLine();
			ImGui::Text("counter = %d", counter);

			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			ImGui::End();
		}

		{
			float total_debt = 0.f;
			world.data.for_each_delayed_transaction([&](auto transaction){
				total_debt += abs(world.data.delayed_transaction_get_balance(transaction, world.coins));
			});
			ImGui::Begin("Stats");
			ImGui::Text("Total debt: %f", total_debt);
			ImGui::End();
		}

		{
			static float f = 0.0f;
			static int counter = 0;

			ImGui::Begin("Characters");

			ImGui::Text("Inventory");
			static ImGuiTableFlags flags =
				ImGuiTableFlags_ScrollY
				| ImGuiTableFlags_RowBg
				| ImGuiTableFlags_BordersOuter
				| ImGuiTableFlags_BordersV
				| ImGuiTableFlags_Resizable
				| ImGuiTableFlags_Reorderable
				| ImGuiTableFlags_Hideable;

			ImVec2 outer_size = ImVec2(0.0f, TEXT_BASE_HEIGHT * 20);
			if (ImGui::BeginTable("table_scrolly", world.data.commodity_size() + 1, flags, outer_size)) {
				ImGui::TableSetupScrollFreeze(0, 1); // Make top row always visible

				ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_None);

				world.data.for_each_commodity([&](auto commodity) {
					ImGui::TableSetupColumn(
						game::get_name(world, commodity).c_str(),
						ImGuiTableColumnFlags_None
					);
				});

				ImGui::TableHeadersRow();

				// Demonstrate using clipper for large vertical lists
				ImGuiListClipper clipper;
				clipper.Begin(1000);
				while (clipper.Step()) {
					world.data.for_each_character([&](dcon::character_id cid){
						if (cid.index() >= clipper.DisplayStart && cid.index() < clipper.DisplayEnd) {
							ImGui::TableNextRow();

							ImGui::TableSetColumnIndex(0);
							ImGui::Text("%s %d", get_name(world, world.data.character_get_action_type(cid)).c_str(), cid.index());

							int column = 1;
							world.data.for_each_commodity([&](auto commodity) {
								ImGui::TableSetColumnIndex(column);
								ImGui::Text("%f", world.data.character_get_inventory(cid, commodity));
								column++;
							});
						}
					});
				}
				ImGui::EndTable();
			}

			ImGui::End();
		}


		ImGui::Render();



		float near_plane = 0.1f;
		float far_plane = 20.f;
		glm::mat4 view(1.f);
		view = glm::rotate(view, -glm::pi<float>() / 9.f * 0.9f, {1.f, 0.f, 0.f});
		view = glm::translate(view, -camera_position);
		// drawing shadow maps
		std::vector<glm::mat4> shadow_projections;
		glm::mat4 projection_full_range = glm::perspective(
			glm::pi<float>() / 3.f, (1.f * width) / height, near_plane, far_plane
		);

		std::vector<glm::vec3> visible_world;

		for (int j = -1; j < 3; j+=2) {
			for (int k = -1; k < 3; k+=2) {
				for (int l = -1; l < 3; l+=2) {
					visible_world.push_back({
						camera_position.x + j * camera_position.z,
						camera_position.y + k * camera_position.z,
						l * 20.f * 2
					});
				}
			}
		}

		for (GLsizei i = 0; i < shadow_layers; i++) {
			float ratio = far_plane / near_plane;
			float current_layer_ratio = (float) i / shadow_layers;
			float frustum_split_near = near_plane * pow(ratio, current_layer_ratio);
			float next_layer_ratio = (float) (i + 1) / shadow_layers;
			float power = pow(ratio, next_layer_ratio);
			float frustum_split_far = near_plane * power;

			glm::mat4 projection_shadow_range = glm::perspective(
				glm::pi<float>() / 3.f, (float)(width) / (float)height, frustum_split_near, frustum_split_far
			);

			auto visible_world = frustum(projection_shadow_range * view).vertices;


			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, shadow_fbo[i]);

			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);

			glEnable(GL_CULL_FACE);
			glCullFace(GL_BACK);

			glDisable(GL_CULL_FACE);

			glDisable(GL_BLEND);

			glClearColor(1.0f, 1.0f, 0.0f, 0.0f);
			glClearDepth(1.0f);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			glViewport(0, 0, shadow_map_resolution, shadow_map_resolution);

			// projection of corners on X
			float min_x = std::numeric_limits<float>::max();
			float max_x = -std::numeric_limits<float>::max();
			for (auto& corner : visible_world) {
				max_x = std::max(max_x, glm::dot(corner, light_x));
				min_x = std::min(min_x, glm::dot(corner, light_x));
			}

			// projection of corners on Y
			float min_y = std::numeric_limits<float>::max();
			float max_y = -std::numeric_limits<float>::max();
			for (auto& corner : visible_world) {
				max_y = std::max(max_y, glm::dot(corner, light_y));
				min_y = std::min(min_y, glm::dot(corner, light_y));
			}

			// projection of corners on Z
			float min_z = std::numeric_limits<float>::max();
			float max_z = -std::numeric_limits<float>::max();
			for (auto& corner : visible_world) {
				max_z = std::max(max_z, glm::dot(corner, light_z));
				min_z = std::min(min_z, glm::dot(corner, light_z));
			}

			glm::vec3 true_center {0.f};
			for (auto& corner : visible_world) {
				true_center += corner;
			}
			true_center /= (float)(visible_world.size());

/*
			auto corner = ground.model.meshes[0].max + glm::vec3(0.0, 10.0, 0.0);
			max_z = std::max(max_z, glm::dot(corner, light_z));
			min_z = std::min(min_z, glm::dot(corner, light_z));

			corner = ground.model.meshes[0].min;
			max_z = std::max(max_z, glm::dot(corner, light_z));
			min_z = std::min(min_z, glm::dot(corner, light_z));
*/

			glm::vec3 max = {max_x, max_y, max_z};
			glm::vec3 min = {min_x, min_y, min_z};

			auto center = (max + min) * 0.5f;

			glm::vec3 to_min {};//min - center;
			glm::vec3 to_max {};//max - center;



			auto center_world = glm::mat3(light_x, light_y, light_z) * center;

			auto ortho = glm::ortho(
				min_x, max_x,
				min_y, max_y,
				-max_z, -min_z
			);
			auto basis_change = glm::mat4(glm::transpose(glm::mat3(light_x, light_y, light_z)));

			glm::mat4 light_projection = ortho * basis_change;
			auto new_basis_center = basis_change * glm::vec4{true_center, 1.f};
			auto image_of_center = light_projection * glm::vec4{true_center, 1.f};
			auto projection_of_center = glm::dot(light_z, true_center);

			shadow_projections.push_back(light_projection);

			// projection_full_range = light_projection;

			glUseProgram(shadow_program);
			glm::mat4 model (1.f);
			glUniformMatrix4fv(shadow_model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
			glUniformMatrix4fv(shadow_transform_location, 1, GL_FALSE, reinterpret_cast<float *>(&light_projection));

			for (auto & ch : world.map.meshes) {
				glBindVertexArray(ch.vao);
				glDrawArrays(
					GL_TRIANGLES,
					0,
					ch.data.size()
				);
			}
			world.data.for_each_thing([&] (dcon::thing_id cid) {
				auto soul = world.data.thing_get_embodier_from_embodiment(cid);
				auto guest_in = world.data.thing_get_guest_location_from_guest(cid);
				if(guest_in) {
					return;
				}
				glm::mat4 model (1.f);
				model = glm::translate(model, {world.data.thing_get_x(cid), world.data.thing_get_y(cid), 0.f});

				auto kind = world.data.thing_get_kind(cid);
				auto size = world.data.kind_get_size(kind);
				model = glm::scale(model, {size, size, 1.f});
				auto rotation = world.data.thing_get_direction(cid);
				model = glm::rotate(model, rotation, glm::vec3{0.f, 0.f, 1.f});
				glUniformMatrix4fv(shadow_model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
				glBindVertexArray(world.data.kind_get_vao(kind));
				glDrawArrays(
					GL_TRIANGLES,
					0,
					world.data.kind_get_triangles_count(kind)
				);
			});
		}

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glfwGetFramebufferSize(window, &width, &height);

		width = std::max(width, 10);
		height = std::max(height, 10);

		glViewport(0, 0, width, height);
		float aspect_ratio = (float) width / (float) height;
		glClearColor(ambient.x, ambient.y, ambient.z, 0.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);

		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);

		// glDisable(GL_CULL_FACE);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glUseProgram(basic_shader);

		glActiveTexture(GL_TEXTURE10);
		glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_map);
		glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

		glm::mat4 model (1.f);
		glUniformMatrix4fv(model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
		glUniformMatrix4fv(view_location, 1, GL_FALSE, reinterpret_cast<float *>(&view));
		glUniformMatrix4fv(projection_location, 1, GL_FALSE, reinterpret_cast<float *>(&projection_full_range));

		glUniform3fv(light_direction_location, 1, reinterpret_cast<float *>(&light_direction));
		glUniform3fv(light_color_location, 1,  reinterpret_cast<float *>(&light_color));
		glUniform3fv(ambient_location, 1,  reinterpret_cast<float *>(&ambient));

		glUniform3fv(albedo_location, 1, albedo_world);

		glUniform1i(shadow_map_location, 10);
		glUniform1i(shadow_layers_location, shadow_layers);
		glUniformMatrix4fv(render_shadow_transform_location, shadow_layers, GL_FALSE, reinterpret_cast<float *>(shadow_projections.data()));

		for (auto & ch : world.map.meshes) {
			glBindVertexArray(ch.vao);
			glDrawArrays(
				GL_TRIANGLES,
				0,
				ch.data.size()
			);
		}

		world.data.for_each_thing([&] (dcon::thing_id cid) {
			auto soul = world.data.thing_get_embodier_from_embodiment(cid);
			auto guest_in = world.data.thing_get_guest_location_from_guest(cid);
			if(guest_in) {
				return;
			}
			glm::mat4 model (1.f);
			model = glm::translate(model, {world.data.thing_get_x(cid), world.data.thing_get_y(cid), 0.f});
			if (!soul) {
				glUniform3fv(albedo_location, 1, albedo_critter);
			} else {
				glUniform3fv(albedo_location, 1, albedo_character);
			}

			auto kind = world.data.thing_get_kind(cid);
			auto size = world.data.kind_get_size(kind);
			model = glm::scale(model, {size, size, 1.f});
			auto rotation = world.data.thing_get_direction(cid);
			model = glm::rotate(model, rotation, glm::vec3{0.f, 0.f, 1.f});
			glUniformMatrix4fv(model_location, 1, GL_FALSE, reinterpret_cast<float *>(&model));
			glBindVertexArray(world.data.kind_get_vao(kind));
			glDrawArrays(
				GL_TRIANGLES,
				0,
				world.data.kind_get_triangles_count(kind)
			);
		});


		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
		glfwPollEvents();
		assert_no_errors();
	}

	// Cleanup
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(window);

	glfwTerminate();
	return 0;
}