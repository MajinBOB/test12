// Click Buffer - forgives clicks made up to N physics ticks too early.
//
// How it works:
//  * When you press jump and the click can't do anything yet (cube in the air,
//    ball in the air, not touching an orb...), we open a "window" of N ticks.
//  * Every physics tick (TPS, not FPS) we check whether the player can now act
//    (landed, or touching an orb/ring). If so, we inject the press on your behalf.
//  * If the window runs out first, the click is simply dropped like vanilla.
//
// Modes where a click is always valid (ship, UFO, wave, swing) are left alone.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

namespace {
	constexpr int JUMP_BUTTON = 1; // PlayerButton::Jump

	struct Pending {
		bool active = false;   // click is waiting for a valid moment
		bool injected = false; // we pressed on the player's behalf
		bool realHeld = false; // physical button is currently down
		int64_t pressTick = 0;
		int64_t injectTick = 0;
	};

	struct BufferState {
		int64_t tick = 0;
		bool injecting = false; // guards against re-entering our own hook
		Pending p[2];           // [0] = player 1, [1] = player 2
	};

	BufferState g_state;
	int64_t g_frames = 3;
	bool g_enabled = true;
}

$on_mod(Loaded) {
	g_frames = Mod::get()->getSettingValue<int64_t>("buffer-frames");
	g_enabled = Mod::get()->getSettingValue<bool>("enabled");

	listenForSettingChanges<int64_t>("buffer-frames", [](int64_t v) { g_frames = v; });
	listenForSettingChanges<bool>("enabled", [](bool v) {
		g_enabled = v;
		g_state = BufferState{};
	});
}

class $modify(ClickBufferLayer, GJBaseGameLayer) {
	// Would a click do something for this player right now?
	static bool canAct(PlayerObject* pl) {
		if (!pl) return false;
		// These modes accept a click at any time, nothing to buffer.
		if (pl->m_isShip || pl->m_isBird || pl->m_isDart || pl->m_isSwing) return true;
		bool touchingRing = pl->m_touchingRings && pl->m_touchingRings->count() > 0;
		return pl->m_isOnGround || touchingRing;
	}

	bool canActFor(bool isPlayer1) {
		bool ok = canAct(isPlayer1 ? m_player1 : m_player2);
		// In non-2-player dual mode one click controls both icons.
		if (!ok && m_gameState.m_isDualMode && !m_levelSettings->m_twoPlayerMode) {
			ok = canAct(isPlayer1 ? m_player2 : m_player1);
		}
		return ok;
	}

	void handleButton(bool down, int button, bool isPlayer1) {
		bool relevant = g_enabled
			&& !g_state.injecting
			&& button == JUMP_BUTTON
			&& typeinfo_cast<PlayLayer*>(this);

		if (relevant) {
			auto& p = g_state.p[isPlayer1 ? 0 : 1];
			if (down) {
				p.realHeld = true;
				p.injected = false;
				p.pressTick = g_state.tick;
				// Click can't do anything yet? Open the buffer window.
				p.active = !this->canActFor(isPlayer1);
			} else {
				// Real release always goes through. A pending window stays open
				// so that a quick early tap still counts.
				p.realHeld = false;
				p.injected = false;
			}
		}

		GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}

	void tickBuffer(bool isPlayer1) {
		auto& p = g_state.p[isPlayer1 ? 0 : 1];

		if (p.active) {
			if (g_state.tick - p.pressTick > g_frames) {
				p.active = false; // too early, click is lost like vanilla
			} else if (this->canActFor(isPlayer1)) {
				g_state.injecting = true;
				if (p.realHeld) {
					GJBaseGameLayer::handleButton(false, JUMP_BUTTON, isPlayer1);
				}
				GJBaseGameLayer::handleButton(true, JUMP_BUTTON, isPlayer1);
				g_state.injecting = false;

				p.active = false;
				p.injected = true;
				p.injectTick = g_state.tick;
			}
		} else if (p.injected && !p.realHeld && g_state.tick > p.injectTick) {
			// Player already let go, so release our injected press.
			g_state.injecting = true;
			GJBaseGameLayer::handleButton(false, JUMP_BUTTON, isPlayer1);
			g_state.injecting = false;
			p.injected = false;
		}
	}

	// Runs once per physics tick, so everything here is TPS-based.
	void processCommands(float dt) {
		if (g_enabled && typeinfo_cast<PlayLayer*>(this)) {
			g_state.tick++;
			this->tickBuffer(true);
			this->tickBuffer(false);
		}
		GJBaseGameLayer::processCommands(dt);
	}
};

class $modify(ClickBufferPlayLayer, PlayLayer) {
	void resetLevel() {
		g_state = BufferState{};
		PlayLayer::resetLevel();
	}
};
