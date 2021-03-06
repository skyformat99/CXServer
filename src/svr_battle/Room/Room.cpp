#include "stdafx.h"
#include "Room.h"
#include "PlayerRoomData.h"
#include "Service/ServiceMgr.h"
#include "Buffer/NetPack.h"
#include "Player/Player.h"

static const uint Room_Player_Min = 1;
static const uint Room_Player_Max = 20;

uint _Service_Room_GameLoop(void* p) {
    CRoom* self = (CRoom*)p;
    self->Update(0.02f);
    return 20;
}

std::map<uint32, CRoom*> CRoom::RoomList;

CRoom::CRoom(uint8 teamCnt /* = 2 */) : m_kTeamCntMax(teamCnt)
{
    RoomList[m_unique_id] = this;
}
void CRoom::DestroyRoom()
{
    RoomList.erase(m_unique_id);

    // 战斗结束，重置仍在的玩家
    for (auto& it : m_waitLst) {
        for (auto& player : it.second) {
            player->m_room.m_roomId = 0;
        }
    }
    for (auto& it : m_players) {
        it.second->m_room.m_roomId = 0;
    }
    delete this;
}
bool CRoom::JoinRoom(Player& player)
{
    // 广播，有人加入游戏
    for (auto& it : m_players) {
        it.second->CallRpc(rpc_client_sync_player_join_info, [&](NetPack& buf){
            buf.WriteUInt32(player.m_pid);
            buf.WriteString(player.m_name);
            buf.WriteUInt32(player.m_index);
            buf.WriteUInt8(player.m_room.m_teamId);
            //buf.WriteUInt32(player.m_room.m_netId);
            //buf.WriteFloat(player.m_room.m_posX);
            //buf.WriteFloat(player.m_room.m_posY);
        });
    }
    m_players[player.m_index] = &player;
    player.m_room.m_roomId = GetUniqueId();
    return true;
}
bool CRoom::ExitRoom(Player& player)
{
    CancelWait(player);

    m_players.erase(player.m_index);
    player.m_room.m_roomId = 0;

    // 广播，有人退出
    for (auto& it : m_players) {
        it.second->CallRpc(rpc_client_notify_player_exit_room, [&](NetPack& buf){
            buf.WriteUInt32(player.m_pid);
            buf.WriteUInt32(player.m_index);
            //buf.WriteUInt32(player.m_room.m_netId);
        });
    }
    if (m_players.empty() && m_waitLst.empty()) DestroyRoom();
    return true;
}
void CRoom::CancelWait(Player& player)
{
    for (auto& it : m_waitLst) {
        for (auto itt = it.second.begin(); itt != it.second.end(); ++itt) {
            if ((*itt)->m_index == player.m_index) {
                it.second.erase(itt);
                return;
            }
        }
    }
}
Player* CRoom::FindBattlePlayer(uint idx)
{
    auto it = m_players.find(idx);
    if (it == m_players.end()) return nullptr;
    return it->second;
}
void CRoom::ForEachTeammate(uint8 teamId, std::function<void(Player&)>& func)
{
    for (auto& it : m_players) {
        Player* ptr = it.second;
        if (teamId == ptr->m_room.m_teamId)
        {
            func(*ptr);
        }
    }
}
void CRoom::ForEachPlayer(std::function<void(Player&)>& func)
{
    for (auto& it : m_players) func(*it.second);
}

bool CRoom::TryToJoinWaitLst(const std::vector<Player*>& lst)
{
    //房间空位不够
    if (m_players.size() + lst.size() > Room_Player_Max) return false;

    //超过单战队人数限制
    const uint OneTeamPlayerMax = Room_Player_Max / m_kTeamCntMax;
    if (lst.size() > OneTeamPlayerMax) return false;

    std::map<uint8, uint> teamInfos;
    for (auto& it : m_players) {
        teamInfos[it.second->m_room.m_teamId] += 1;
    }
    for (auto& it : m_waitLst) {
        teamInfos[it.first] += it.second.size();
    }
    //新建战队否
    if (teamInfos.size() < m_kTeamCntMax) {
        uint8 newTeamId = teamInfos.empty() ? 1 : teamInfos.rbegin()->first + 1;
        teamInfos[newTeamId] = lst.size();
        for (auto& it : lst) it->m_room.m_roomId = m_unique_id;
        m_waitLst[newTeamId] = std::move(lst);
        _FlushWaitLst(teamInfos);
        return true;
    }
    //优先加人数最少的战队
    uint min = 100; uint8 minId = 0;
    for (auto& it : teamInfos) {
        if (it.second < min) {
            min = it.second;
            minId = it.first;
        }
    }
    if (teamInfos[minId] + lst.size() <= OneTeamPlayerMax) {
        teamInfos[minId] += lst.size();
        for (auto& it : lst) {
            it->m_room.m_roomId = m_unique_id;
            m_waitLst[minId].push_back(it);
        }
        _FlushWaitLst(teamInfos);
        return true;
    }
    return false;
}
void CRoom::_FlushWaitLst(const std::map<uint8, uint>& teamInfos)
{
    //if (teamInfos.size() < 2) return;//TODO:zhoumf:临时放开

    uint min = 100, max = 0, total = 0;
    for (auto& it : teamInfos) {
        if (it.second < min) min = it.second;
        if (it.second > max) max = it.second;
        total += it.second;
    }
    if (total >= Room_Player_Min && max - min <= 1) {
        //设置玩家的m_roomId，登录时，若自己m_roomId有效，开启“进入战斗场景流程”
        //已登录的，直接开启“进入战斗场景流程”
        /* 废弃：进入战斗场景流程：
            1、通知Client关闭等待界面，载入战斗场景
            2、完毕后上报svr_battle，此时尚不能操作
            3、svr_battle收到载入完毕消息，真正将玩家加入房间，进场景，再回复client
            4、client操作解锁
        */
        for (auto& it : m_waitLst) {
            for (auto& player : it.second) {
                player->m_room.m_teamId = it.first;
                player->m_room.m_canJoinRoom = true;
                if (player->m_isLogin) { player->m_room.NotifyClientJoinRoom(); }
            }
        }
        m_waitLst.clear();
    }
}

void CRoom::Update(float dt)
{

}