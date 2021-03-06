#include "stdafx.h"
#include "luanode.h"
#include "luanode_timer.h"
#include "blogger.h"

#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

using namespace LuaNode;

const char* Timer::className = "Timer";
const Timer::RegType Timer::methods[] = {
	{"start", &Timer::Start},
	{"stop", &Timer::Stop},
	{"again", &Timer::Again},
	{0}
};

const Timer::RegType Timer::setters[] = {
	{0}
};

const Timer::RegType Timer::getters[] = {
	{0}
};

// Take care: if the code is running on a coroutine, L is not the main state
Timer::Timer(lua_State* L) : 
	m_repeats(false),
	m_after(0)
{
	LogDebug("Constructing Timer (%p)", this);
}

Timer::~Timer(void)
{
	LogDebug("Destructing Timer (%p)", this);
}

//////////////////////////////////////////////////////////////////////////
/// 
int Timer::Start (lua_State* L)
{
	m_after = luaL_checkinteger(L, 2);
	int repeat = luaL_checkinteger(L, 3);

	LogDebug("Timer::Start (%p) (after: %d ms, repeat: %d ms)", this, m_after, repeat);

	if(repeat != 0) {
		m_repeats = true;
	}

	if(m_timer != NULL) {
		// TODO: fix me
		luaL_error(L, "double Start not implemented yet in timers");
	}
	m_timer = boost::make_shared<boost::asio::deadline_timer>( boost::ref(GetIoService()), boost::posix_time::milliseconds(m_after) );

	lua_pushvalue(L, 1);
	int reference = luaL_ref(L, LUA_REGISTRYINDEX);
	m_timer->async_wait( boost::bind(&Timer::OnTimeout, this, reference, boost::asio::placeholders::error) );
	return 0;
}

//////////////////////////////////////////////////////////////////////////
/// 
int Timer::Stop (lua_State* L)
{
	LogDebug("Timer::Stop (%p)", this);
	if(m_timer != NULL) {
		m_timer->cancel();
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
/// 
int Timer::Again (lua_State* L)
{
	LogDebug("Timer::Again (%p)", this);

	// if no timer, start it (non repeating)
	if(!m_timer) {
		lua_pushinteger(L, 0);
		return Start(L);
	}
	else {
		// if started but non repeating, stop it and start it again
		if(!m_repeats) {
			m_timer->cancel();
		}

		if(lua_isnumber(L, 2)) {
			m_after = lua_tointeger(L, 2);
		}

		lua_pushvalue(L, 1);
		int reference = luaL_ref(L, LUA_REGISTRYINDEX);
		m_timer->expires_from_now( boost::posix_time::milliseconds(m_after) );
		m_timer->async_wait( boost::bind(&Timer::OnTimeout, this, reference, boost::asio::placeholders::error) );
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
/// 
void Timer::OnTimeout (int reference, const boost::system::error_code& error)
{
	lua_State* L = LuaNode::GetLuaVM();
	lua_rawgeti(L, LUA_REGISTRYINDEX, reference);
	luaL_unref(L, LUA_REGISTRYINDEX, reference);

	if(!error || error != boost::asio::error::operation_aborted) {
		// 'm_repeats' might change during the callback
		
		lua_getfield(L, 1, "callback");
		if(lua_type(L, 2) == LUA_TFUNCTION) {
			LogDebug("Timer::OnTimeout (%p) - Timer fired", this);
			LuaNode::GetLuaVM().call(0, LUA_MULTRET);

			if(m_repeats) {
				// since a new timer could be setup in the callback, grab a new reference and pass that
				// so I can always clean up the old one
				// maybe optimize this some time
				int newReference = luaL_ref(L, LUA_REGISTRYINDEX);
				m_timer->expires_from_now( boost::posix_time::milliseconds(m_after) );
				m_timer->async_wait( boost::bind(&Timer::OnTimeout, this, newReference, boost::asio::placeholders::error) );
			}
		}
		else {
			LogDebug("Timer::OnTimeout (%p) - Timer fired without a callback. Cancelling it.", this);
			m_timer->cancel();
		}
	}
	else {
		if(error.value() == boost::asio::error::operation_aborted) {
			LogDebug("Timer::OnTimeout (%p) - Timer was cancelled", this);
		}
		else {
			LogError("Timer::OnTimeout (%p) - error %s", this, error.message().c_str());
		}
	}
	lua_settop(L, 0);
}
