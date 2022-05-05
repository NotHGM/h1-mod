#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "dvars.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include <utils/nt.hpp>
#include <utils/hook.hpp>
#include <utils/flags.hpp>

namespace gameplay
{
	namespace
	{
		utils::hook::detour pm_weapon_use_ammo_hook;

		game::dvar_t* jump_slowDownEnable;
		game::dvar_t* jump_enableFallDamage;

		void jump_apply_slowdown_stub(game::mp::playerState_s* ps)
		{
			if (jump_slowDownEnable->current.enabled)
			{
				utils::hook::invoke<void>(0x1401D5360, ps);
			}
		}

		int stuck_in_client_stub(void* entity)
		{
			if (dvars::g_playerEjection->current.enabled)
			{
				return utils::hook::invoke<int>(0x140326CE0, entity); // StuckInClient
			}

			return 0;
		}

		void cm_transformed_capsule_trace_stub(game::trace_t* results, const float* start, const float* end,
			game::Bounds* bounds, game::Bounds* capsule, int contents, const float* origin, const float* angles)
		{
			if (dvars::g_playerCollision->current.enabled)
			{
				utils::hook::invoke<void>(0x1403FF860,
					results, start, end, bounds, capsule, contents, origin, angles); // CM_TransformedCapsuleTrace
			}
		}

		void pm_crashland_stub(game::mp::playerState_s* ps, void* pml)
		{
			if (jump_enableFallDamage->current.enabled)
			{
				utils::hook::invoke<void>(0x1401E2D00, ps, pml);
			}
		}

		void pm_weapon_use_ammo_stub(game::playerState_s* ps, game::Weapon weapon,
			bool is_alternate, int amount, game::PlayerHandIndex hand)
		{
			if (!dvars::player_sustainAmmo->current.enabled)
			{
				pm_weapon_use_ammo_hook.invoke<void>(ps, weapon, is_alternate, amount, hand);
			}
		}

		const auto pm_bouncing_stub_mp = utils::hook::assemble([](utils::hook::assembler& a)
		{
			const auto no_bounce = a.newLabel();
			const auto loc_1401EAF9D = a.newLabel();

			a.push(rax);

			a.mov(rax, qword_ptr(reinterpret_cast<int64_t>(&dvars::pm_bouncing)));
			a.mov(al, byte_ptr(rax, 0x10));
			a.cmp(byte_ptr(rbp, -0x2D), al);

			a.pop(rax);
			a.jz(no_bounce);
			a.jmp(0x1401EB000);

			a.bind(no_bounce);
			a.cmp(dword_ptr(rsp, 0x70), 0);
			a.jnz(loc_1401EAF9D);
			a.jmp(0x1401EAFF1);

			a.bind(loc_1401EAF9D);
			a.jmp(0x1401EAF9D);
		});

		const auto g_speed_stub = utils::hook::assemble([](utils::hook::assembler& a)
		{
			a.mov(rax, qword_ptr(reinterpret_cast<int64_t>(&dvars::g_speed)));
			a.mov(eax, dword_ptr(rax, 0x10));

			// original code
			a.mov(dword_ptr(r14, 0x36), ax);
			a.movzx(eax, word_ptr(r14, 0x3A));

			a.jmp(0x140323DBC);
		});

		const auto client_end_frame_stub = utils::hook::assemble([](utils::hook::assembler& a)
		{
			a.push(rax);

			a.mov(rax, qword_ptr(reinterpret_cast<int64_t>(&dvars::g_gravity)));
			a.mov(eax, dword_ptr(rax, 0x10));
			a.mov(word_ptr(rbx, 0x34), ax);

			a.pop(rax);

			// Game code hook skipped
			a.mov(eax, dword_ptr(rbx, 0x494C));
			a.mov(rdi, rcx);

			a.jmp(0x140322F82);
		});
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			dvars::player_sustainAmmo = dvars::register_bool("player_sustainAmmo", false,
				game::DVAR_FLAG_REPLICATED, "Firing weapon will not decrease clip ammo");
			pm_weapon_use_ammo_hook.create(SELECT_VALUE(0x14042E380, 0x1401F6B90), &pm_weapon_use_ammo_stub);

			if (game::environment::is_sp())
			{
				return;
			}

			utils::hook::call(0x1401E8830, jump_apply_slowdown_stub);
			jump_slowDownEnable = dvars::register_bool("jump_slowDownEnable", true, game::DVAR_FLAG_REPLICATED, "Slow player movement after jumping");

			utils::hook::call(0x1401E490F, pm_crashland_stub);
			jump_enableFallDamage = dvars::register_bool("jump_enableFallDamage", true, game::DVAR_FLAG_REPLICATED, "Enable fall damage");

			dvars::g_playerEjection = dvars::register_bool("g_playerEjection", true, game::DVAR_FLAG_REPLICATED,
				"Flag whether player ejection is on or off");
			utils::hook::call(0x140323333, stuck_in_client_stub);

			utils::hook::nop(0x140323DAD, 15);
			utils::hook::jump(0x140323DAD, g_speed_stub, true);
			dvars::g_speed = dvars::register_int("g_speed", 190, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
				game::DVAR_FLAG_REPLICATED, "changes the speed of the player");

			// Implement player collision dvar
			dvars::g_playerCollision = dvars::register_bool("g_playerCollision", true, game::DVAR_FLAG_REPLICATED,
				"Flag whether player collision is on or off");
			utils::hook::call(0x14049D7CF, cm_transformed_capsule_trace_stub); // SV_ClipMoveToEntity
			utils::hook::call(0x140240BC3, cm_transformed_capsule_trace_stub); // CG_ClipMoveToEntity

			// Implement bouncing dvar
			dvars::pm_bouncing = dvars::register_bool("pm_bouncing", false,
				game::DVAR_FLAG_REPLICATED, "Enable bouncing");
			utils::hook::jump(0x1401EAFE4, pm_bouncing_stub_mp, true);

			dvars::g_gravity = dvars::register_int("g_gravity", 800, std::numeric_limits<short>::min(),
				std::numeric_limits<short>::max(), game::DVAR_FLAG_REPLICATED, "Game gravity in inches per second squared");
			utils::hook::jump(0x140322F72, client_end_frame_stub, true);
			utils::hook::nop(0x140322F68, 1); // Nop skipped opcode
		}
	};
}

REGISTER_COMPONENT(gameplay::component)
