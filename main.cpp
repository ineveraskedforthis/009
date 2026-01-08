#include "data_ids.hpp"
#include <assert.h>
#define GLEW_STATIC
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

#define GLM_FORCE_SWIZZLE

#include "stb_image/stb_image.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"

#include "data.hpp"


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

	int price_update_tick = 0;
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


void hunt(state& game, dcon::character_id cid) {
	auto timer = game.data.character_get_action_timer(cid);
	if (timer > 15) {
		auto weapon = game.data.character_get_weapon_quality(cid);
		auto bonus = int(weapon / 0.2);
		auto food = game.data.character_get_inventory(cid, game.raw_food);
		game.data.character_set_inventory(cid, game.raw_food, food + 1.f + (float)bonus);
		game.data.character_set_action_timer(cid, 0);
		game.data.character_set_action_type(cid, {});
	} else {
		game.data.character_set_action_timer(cid, timer + 1);
	}
	game.data.character_set_hp(cid, game.data.character_get_hp(cid) - 2);
	auto quality = game.data.character_get_weapon_quality(cid);
	game.data.character_set_weapon_quality(cid, quality * 0.99f);
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
		game.data.delete_guest(game.data.character_get_guest(cid));
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
	auto hunger = game.data.character_get_hunger(cid);
	if (food >= 1.f) {
		game.data.character_set_inventory(cid, game.prepared_food, food - 1);
		game.data.character_set_hunger(cid, hunger - 20);

		auto hp = game.data.character_get_hp(cid);
		game.data.character_set_hp(cid, hp + 5);
	}
}
void drink_potion(state& game, dcon::character_id cid) {
	auto potions = game.data.character_get_inventory(cid, game.potion);
	auto hp = game.data.character_get_hp(cid);
	auto hp_max = game.data.character_get_hp_max(cid);

	if (hp * 2 < hp_max && potions >= 1.f) {
		game.data.character_set_inventory(cid, game.potion, potions - 1);
		game.data.character_set_hp(cid, hp + 10);
	}
}


namespace ai {

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

}

namespace update {

void hunter(state& game, dcon::character_id cid) {
	auto x = game.data.character_get_x(cid);
	auto y = game.data.character_get_y(cid);
	auto coins = game.data.character_get_inventory(cid, game.coins);
	auto action = game.data.character_get_action_type(cid);
	auto ai_type = game.data.character_get_ai_type(cid);

	assert(ai_type == game.personality.hunter);

	auto favourite_weapons_shop = game.data.character_get_favourite_shop_weapons(cid);
	auto favourite_weapons_shop_owner = game.data.building_get_owner_from_ownership(favourite_weapons_shop);
	auto weapon_repair_price = game.data.character_get_price_belief_sell(favourite_weapons_shop_owner, game.weapon_service);

	if (!action) {
		auto loot  = game.data.character_get_inventory(cid, game.raw_food);
		auto loot_target = game.data.ai_model_get_stockpile_target(ai_type, game.raw_food);

		if (loot_target > 3) {
			game.data.character_set_action_type(cid, game.ai.shopping);
		} else if (
			ai::triggers::desire_weapon_repair(game, cid, favourite_weapons_shop_owner)
		) {
			game.data.character_set_action_type(cid, game.ai.weapon_repair);
		} else if (
			game.data.character_get_hunger(cid) > 200
			&& game.data.character_get_inventory(cid, game.raw_food) >= 1.f
		) {
			game.data.character_set_action_type(cid, game.ai.prepare_food);
		} else {
			game.data.character_set_action_type(cid, game.ai.working);
		}
	}

	auto guest_in = game.data.character_get_guest_location_from_guest(cid);
	auto timer = game.data.character_get_action_timer(cid);

	if (action == game.ai.weapon_repair) {
		if (
			ai::triggers::desire_weapon_repair(game, cid, favourite_weapons_shop_owner) || timer > 0
		) {
			auto target_x = game.data.building_get_tile_x(favourite_weapons_shop);
			auto target_y = game.data.building_get_tile_y(favourite_weapons_shop);

			auto dx = (float) target_x - x;
			auto dy = (float) target_y - y;
			auto distance = abs(dx) + abs(dy);

			auto timer = game.data.character_get_action_timer(cid);

			if (guest_in == favourite_weapons_shop) {
				repair_weapon(game, cid, favourite_weapons_shop_owner);
			} else if (guest_in) {
				game.data.delete_guest(game.data.character_get_guest(cid));
			} else if (distance < 2){
				game.data.force_create_guest(cid, favourite_weapons_shop);
			} else {
				if (dx != 0) {
					game.data.character_set_x(cid, x + dx / abs(dx));
				}
				if (dy != 0) {
					game.data.character_set_y(cid, y + dy / abs(dy));
				}
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
			game.data.character_set_action_timer(cid, 0);
			game.data.character_set_action_type(cid, {});
		}
		return;
	} else if (action == game.ai.working) {
		hunt(game, cid);
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

	auto human = game.data.create_kind();
	auto rat = game.data.create_kind();

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
		game.data.character_set_hp(hunter, 100);
		game.data.character_set_hp_max(hunter, 100);
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
		game.data.character_set_inventory(weapon_master, game.coins, 10);
		game.data.character_set_ai_type(weapon_master, game.personality.weapon_master);

		game.data.force_create_ownership(weapon_master, shop_weapons);
	}

	for (int i = 0; i < 2; i++) {
		auto alchemist = game.data.create_character();
		game.data.character_set_inventory(alchemist, game.coins, 100);
		game.data.character_set_ai_type(alchemist, game.personality.alchemist);
	}

	for (int i = 0; i < 2; i++) {
		auto herbalist = game.data.create_character();
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
}

void update(state& game) {
	game.data.for_each_character([&](auto cid) {
		auto model = game.data.character_get_ai_type(cid);
		if (model == game.personality.hunter) {
			ai::update::hunter(game, cid);
		} else if (model == game.personality.alchemist) {
			auto potion_price = game.data.character_get_price_belief_sell(cid, game.potion);
			auto potion_material_cost = game.data.character_get_price_belief_buy(cid, game.potion_material);
			if (
				game.data.character_get_inventory(cid, game.potion_material) >= 1.f
				&& potion_price > potion_material_cost * 2.f
			) {
				make_potion(game, cid);
			}

			auto timer = game.data.character_get_action_timer(cid);
			game.data.character_set_action_timer(cid, timer + 1);
		} else if (model == game.personality.herbalist) {
			gather_potion_material(game, cid);

			auto timer = game.data.character_get_action_timer(cid);
			game.data.character_set_action_timer(cid, timer + 1);
		} else if (model == game.personality.innkeeper) {
			if (game.data.character_get_inventory(cid, game.raw_food) >= 1.f) {
				printf("make food\n");
				prepare_food(game, cid);
			}

			auto timer = game.data.character_get_action_timer(cid);
			game.data.character_set_action_timer(cid, timer + 1);
		}
		auto hunger = game.data.character_get_hunger(cid);
		game.data.character_set_hunger(cid, hunger + 1);
	});

	// trade:

	// ai logic would be very simple:
	// sell things you don't desire yourself
	// buy things you desire and miss

	// currently we can buy things only from the favourite shop:

	for (int round = 0; round < 3; round++) {
		game.data.for_each_character([&](auto cid) {
			game.data.for_each_commodity([&](auto commodity) {
				if (commodity == game.coins) {
					return;
				}
				if (commodity == game.weapon_service) {
					return;
				}

				auto shop = game.data.character_get_favourite_shop(cid);
				if (commodity == game.prepared_food) {
					shop = game.data.character_get_favourite_inn(cid);
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
						printf("I am buying\n");
						transaction(game, shop_owner, cid, commodity, 1.f);
						transaction(game, cid, shop_owner, game.coins, price_shop_sell);
					} else if (desired_price_buy >= price_shop_sell && coins >= price_shop_sell) {
						printf("I am ordering\n");
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
						if (!already_indebted) {
							delayed_transaction(game, shop_owner, cid, commodity, 1.f);
							transaction(game, cid, shop_owner, game.coins, price_shop_sell);
						} else {
							printf("But I have already ordered a lot\n");
						}
					}
				}

				if (target < inventory && price_shop_buy > bottom_price) {
					// printf("I do not need this? %d %f %f %f\n", commodity.index(), desired_price_sell, price_shop_buy, in_stock );

					if (price_shop_buy >= desired_price_sell && inventory >= 1.f && coins_shop >= price_shop_buy) {
						printf("I am selling\n");
						transaction(game, cid, shop_owner, commodity, 1.f);
						transaction(game, shop_owner, cid, game.coins, price_shop_buy);
					} else if (price_shop_buy >= desired_price_sell && inventory >= 1.f) {
						printf("I am selling for promise of future payment\n");
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
		if (game.data.character_get_hunger(cid) > 50.f) {
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
		auto message = opengl_get_error_name(glGetError());
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

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GLFW_TRUE);
}

constexpr int triangle_size = 9;
static float input_model[triangle_size] = {
	0.f, 1.f, 0.f,
	-1.f, -1.f, 0.f,
	1.f, -1.f, 0.f
};

base_triangle create_triangle() {
	// setting up vertex buffers
	GLuint vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, triangle_size * sizeof(float), input_model, GL_STATIC_DRAW);

	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float) * 3,  reinterpret_cast<void*>(0));

	base_triangle result {
		.vao = vao,
		.vbo = vbo
	};

	return result;
}

void render_triangle(
	base_triangle object
) {
	glBindVertexArray(object.vao);

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

	// glEnable(GL_DEBUG_OUTPUT);
	// glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

	// illumination settings
	glm::vec3 light_color = glm::vec3(1.f, 1.f, 0.f);
	glm::vec3 ambient = glm::vec3(0.8f, 0.6f, 0.6f);
	glClearColor(ambient.x, ambient.y, ambient.z, 0.f);


	auto triangle = create_triangle();

	// setting up a basic shader
	std::string basic_shader_vertex_path = "./shaders/basic_shader.vert";
	std::string basic_shader_fragment_path = "./shaders/basic_shader.frag";

	std::string vertex_shader_source = read_shader( basic_shader_vertex_path );
	std::string fragment_shader_source = read_shader( basic_shader_fragment_path );

	auto basic_shader = create_program(
		create_shader(GL_VERTEX_SHADER, vertex_shader_source.c_str()),
		create_shader(GL_FRAGMENT_SHADER, fragment_shader_source.c_str())
	);


	int width = 1;
	int height = 1;

	assert_no_errors();

	game::init(world);

	float update_timer = 0.f;

	double last_time = glfwGetTime();
	while (!glfwWindowShouldClose(window))
	{
		// if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
		// 	ImGui_ImplGlfw_Sleep(10);
		// 	continue;
		// }

		// Start the Dear ImGui frame
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();


		const float TEXT_BASE_HEIGHT = ImGui::GetTextLineHeightWithSpacing();

		{
			static float f = 0.0f;
			static int counter = 0;

			ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

			ImGui::Text("Test.");               // Display some text (you can use a format strings too)

			ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
			ImGui::ColorEdit3("clear color", (float*)&ambient); // Edit 3 floats representing a color

			if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
				counter++;
			ImGui::SameLine();
			ImGui::Text("counter = %d", counter);

			ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
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
							ImGui::Text("Character %d", cid.index());

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

		double time = glfwGetTime();
		float dt = (float)(time - last_time);
		last_time = time;

		update_timer += dt;

		if (update_timer > 1.f / 60.f) {
			update_timer = 0.f;
			game::update(world);
		}

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glfwGetFramebufferSize(window, &width, &height);
		glViewport(0, 0, width, height);
		glClearColor(ambient.x, ambient.y, ambient.z, 0.f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);

		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


		glUseProgram(basic_shader);

		render_triangle(triangle);

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