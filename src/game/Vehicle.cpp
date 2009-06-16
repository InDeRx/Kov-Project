/*
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Vehicle.h"
#include "Unit.h"
#include "Util.h"
#include "WorldPacket.h"
#include "InstanceData.h"

Vehicle::Vehicle() : Creature(), m_vehicleId(0), m_vehicleInfo(NULL), m_spawnduration(0),
                     despawn(false), m_creation_time(0), m_VehicleData(NULL)
{
    m_isVehicle = true;
    m_updateFlag = (UPDATEFLAG_LIVING | UPDATEFLAG_HAS_POSITION | UPDATEFLAG_VEHICLE);
}

Vehicle::~Vehicle()
{
    if(m_uint32Values)                                      // only for fully created Object
        ObjectAccessor::Instance().RemoveObject(this);
}

void Vehicle::AddToWorld()
{
    ///- Register the vehicle for guid lookup
    if(!IsInWorld()) ObjectAccessor::Instance().AddObject(this);
    Unit::AddToWorld();
}

void Vehicle::RemoveFromWorld()
{
    ///- Remove the vehicle from the accessor
    if(IsInWorld()) ObjectAccessor::Instance().RemoveObject(this);
    ///- Don't call the function for Creature, normal mobs + totems go in a different storage
    Unit::RemoveFromWorld();
}

void Vehicle::setDeathState(DeathState s)                       // overwrite virtual Creature::setDeathState and Unit::setDeathState
{
    Creature::setDeathState(s);
    if(s == JUST_DIED)
    {
        if(GetVehicleFlags() & VF_DESPAWN_NPC)
            Dismiss();
        else
            RemoveAllPassengers();
    }
}

void Vehicle::Update(uint32 diff)
{
    Creature::Update(diff);
    if(despawn)
    {
        m_spawnduration -= diff;
        if(m_spawnduration < 0)
            Dismiss();
        despawn = false;
    }
}

bool Vehicle::Create(uint32 guidlow, Map *map, uint32 phaseMask, uint32 Entry, uint32 vehicleId, uint32 team, const CreatureData *data)
{
    SetMapId(map->GetId());
    SetInstanceId(map->GetInstanceId());
    SetPhaseMask(phaseMask,false);

    CreatureInfo const *cinfo = objmgr.GetCreatureTemplate(Entry);
    if(!cinfo)
    {
        sLog.outErrorDb("Creature entry %u does not exist.", Entry);
        return false;
    }

    Object::_Create(guidlow, Entry, HIGHGUID_VEHICLE);

    if(!UpdateEntry(Entry, team, data))
        return false;

    if(!vehicleId)
    {
        CreatureDataAddon const *cainfo = GetCreatureAddon();
        if(!cainfo)
            return false;
        vehicleId = cainfo->vehicle_id;
    }
    if(!SetVehicleId(vehicleId))
        return false;

    LoadCreaturesAddon();

    m_regenHealth = false;
    m_creation_time = getMSTime();

    SetFloatValue(UNIT_FIELD_HOVERHEIGHT, 1.0f);
    RemoveUnitMovementFlag(MOVEMENTFLAG_WALK_MODE);

    //Notify the map's instance data.
    //Only works if you create the object in it, not if it is moves to that map.
    //Normally non-players do not teleport to other maps.
    if(map->IsDungeon() && ((InstanceMap*)map)->GetInstanceData())
    {
        ((InstanceMap*)map)->GetInstanceData()->OnCreatureCreate(this);
    }

    return true;
}

bool Vehicle::SetVehicleId(uint32 vehicleid)
{
    VehicleEntry const *vehicleInfo = sVehicleStore.LookupEntry(vehicleid);
    if(!vehicleInfo)
        return false;

    m_vehicleId = vehicleid;
    m_vehicleInfo = vehicleInfo;

    // can be NULL
    VehicleDataStructure const *VDStructure = objmgr.GetVehicleData(vehicleid);
    if(VDStructure)
        m_VehicleData = VDStructure;

    InitSeats();
    return true;
}

void Vehicle::InitSeats()
{
    m_Seats.clear();

    for(uint32 i = 0; i < MAX_SEAT; ++i)
    {
        uint32 seatId = m_vehicleInfo->m_seatID[i];
        if(seatId)
        {
            if(VehicleSeatEntry const *veSeat = sVehicleSeatStore.LookupEntry(seatId))
            {
                VehicleSeat newseat;
                //newseat.seatInfo = veSeat;
                newseat.passenger = NULL;
                newseat.flags = SEAT_FREE;
                newseat.vs_flags = objmgr.GetSeatFlags(seatId);
                m_Seats[i] = newseat;
            }
        }
    }
    // NOTE : there can be vehicles without seats (eg. 180) - probably some TEST vehicles
}
void Vehicle::ChangeSeatFlag(uint8 seat, uint8 flag)
{
    SeatMap::iterator i_seat = m_Seats.find(seat);
    // this should never happen
    if(i_seat == m_Seats.end())
        return;

    if(i_seat->second.flags != flag)
    {
        i_seat->second.flags = flag;
        EmptySeatsCountChanged();
    }
}
bool Vehicle::FindFreeSeat(int8 *seatid, bool force)
{
    SeatMap::const_iterator i_seat = m_Seats.find(*seatid);
    if(i_seat == m_Seats.end())
        return GetFirstEmptySeat(seatid, force);
    if((i_seat->second.flags & (SEAT_FULL | SEAT_VEHICLE_FULL)) || (!force && (i_seat->second.vs_flags & SF_UNACCESSIBLE)))
        return GetNextEmptySeat(seatid, true, force);

    return true;
}

bool Vehicle::GetFirstEmptySeat(int8 *seatId, bool force)
{
    for(SeatMap::iterator itr = m_Seats.begin(); itr != m_Seats.end(); ++itr)
    {
        if(itr->second.flags & (SEAT_FREE | SEAT_VEHICLE_FREE))
        {
            if(!force && (itr->second.vs_flags & SF_UNACCESSIBLE))
                continue;

            *seatId = itr->first;
            return true;
        }
    }

    return false;
}

int8 Vehicle::GetEmptySeatsCount(bool force)
{
    int8 count = 0;
    for(SeatMap::iterator itr = m_Seats.begin(); itr != m_Seats.end(); ++itr)
   {
        if(itr->second.flags & (SEAT_FREE | SEAT_VEHICLE_FREE))
        {
            if(!force && (itr->second.vs_flags & SF_UNACCESSIBLE))
                continue;

            count++;
        }
    }

    return count;
}

bool Vehicle::GetNextEmptySeat(int8 *seatId, bool next, bool force)
{
    SeatMap::const_iterator i_seat = m_Seats.find(*seatId);
    if(i_seat == m_Seats.end()) return GetFirstEmptySeat(seatId, force);

    while((i_seat->second.flags & (SEAT_FULL | SEAT_VEHICLE_FULL)) || (!force && (i_seat->second.vs_flags & SF_UNACCESSIBLE)))
    {
        if(next)
        {
            ++i_seat;
            if(i_seat == m_Seats.end())
                i_seat = m_Seats.begin();
        }
        else
        {
            if(i_seat == m_Seats.begin())
                i_seat = m_Seats.end();
            --i_seat;
        }
        if(i_seat->first == *seatId)
            return false;
    }
    *seatId = i_seat->first;
    return true;
}

void Vehicle::EmptySeatsCountChanged()
{
    uint8 m_count = GetTotalSeatsCount();
    uint8 p_count = GetEmptySeatsCount(false);
    uint8 u_count = GetEmptySeatsCount(true);

    // seats accesibles by players
    if(p_count > 0)
        SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);
    else
        RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);

    if(u_count == m_count)
    {
        RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_24);
    }
    else
        SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNK_24);

    if(uint64 vehicleGUID = GetVehicleGUID())
    {
        if(Vehicle *vehicle = ObjectAccessor::GetVehicle(vehicleGUID))
        {
            if(u_count > 0)
                vehicle->ChangeSeatFlag(m_SeatData.seat, SEAT_VEHICLE_FREE);
            else
                vehicle->ChangeSeatFlag(m_SeatData.seat, SEAT_VEHICLE_FULL);
        }
    }
}



void Vehicle::Dismiss()
{
    RemoveAllPassengers();
    SendObjectDeSpawnAnim(GetGUID());
    CombatStop();
    CleanupsBeforeDelete();
    AddObjectToRemoveList();
}

void Vehicle::RellocatePassengers(Map *map)
{
    for(SeatMap::iterator itr = m_Seats.begin(); itr != m_Seats.end(); ++itr)
    {
        if(itr->second.flags & SEAT_FULL)
        {
            // passenger cant be NULL here
            Unit *passengers = itr->second.passenger;
            assert(passengers);

            float scale = GetFloatValue(OBJECT_FIELD_SCALE_X);
            float xx = GetPositionX() + passengers->m_SeatData.OffsetX * scale;
            float yy = GetPositionY() + passengers->m_SeatData.OffsetY * scale;
            float zz = GetPositionZ() + passengers->m_SeatData.OffsetZ * scale;
            //float oo = passengers->m_SeatData.Orientation;
            // this is not correct, we should recalculate
            // actual rotation depending on vehicle
            float oo = passengers->GetOrientation();

            if(passengers->GetTypeId() == TYPEID_PLAYER)
                ((Player*)passengers)->SetPosition(xx, yy, zz, oo);
            else
                map->CreatureRelocation((Creature*)passengers, xx, yy, zz, oo);
        }
        else if(itr->second.flags & (SEAT_VEHICLE_FULL | SEAT_VEHICLE_FREE))
        {
            // passenger cant be NULL here
            Unit *passengers = itr->second.passenger;
            assert(passengers);

            float scale = GetFloatValue(OBJECT_FIELD_SCALE_X);
            float xx = GetPositionX() + passengers->m_SeatData.OffsetX * scale;
            float yy = GetPositionY() + passengers->m_SeatData.OffsetY * scale;
            float zz = GetPositionZ() + passengers->m_SeatData.OffsetZ * scale;
            //float oo = passengers->m_SeatData.Orientation;
            // this is not correct, we should recalculate
            // actual rotation depending on vehicle
            float oo = passengers->GetOrientation();

            map->CreatureRelocation((Creature*)passengers, xx, yy, zz, oo);
            ((Vehicle*)passengers)->RellocatePassengers(map);
        }
    }
}

void Vehicle::AddPassenger(Unit *unit, int8 seatId, bool force)
{
    SeatMap::iterator seat;
    seat = m_Seats.find(seatId);

    // this should never happen
    if(seat == m_Seats.end())
        return;

    if(seat->second.flags & SEAT_VEHICLE_FREE)
    {
        // this should never be NULL, but..
        if(Vehicle *v = (Vehicle*)seat->second.passenger)
        {
            int8 s_id = 0;
            v->GetFirstEmptySeat(&s_id, force);
            v->AddPassenger(unit, s_id, force);
        }
        return;
    }
    unit->SetVehicleGUID(GetGUID());

    seat->second.passenger = unit;
    if(unit->GetTypeId() == TYPEID_UNIT && ((Creature*)unit)->isVehicle())
    {
        if(((Vehicle*)unit)->GetEmptySeatsCount(true) == 0)
            seat->second.flags = SEAT_VEHICLE_FULL;
        else
            seat->second.flags = SEAT_VEHICLE_FREE;
    }
    else
    {
        seat->second.flags = SEAT_FULL;
    }

    if(unit->GetTypeId() == TYPEID_PLAYER)
    {
        WorldPacket data0(SMSG_FORCE_MOVE_ROOT, 10);
        data0.append(unit->GetPackGUID());
        data0 << (uint32)((seat->second.vs_flags & SF_CAN_CAST) ? 2 : 0);
        unit->SendMessageToSet(&data0,true);
    }

    if(seat->second.vs_flags & SF_MAIN_RIDER)
    {
        if(!(GetVehicleFlags() & VF_MOVEMENT))
        {
            GetMotionMaster()->Clear(false);
            GetMotionMaster()->MoveIdle();
            StopMoving();
            SetCharmerGUID(unit->GetGUID());
            unit->SetUInt64Value(UNIT_FIELD_CHARM, GetGUID());
            if(unit->GetTypeId() == TYPEID_PLAYER)
            {
                ((Player*)unit)->SetMoverInQueve(this);
                ((Player*)unit)->SetClientControl(this, 1);
            }
        }

        SpellClickInfoMapBounds clickPair = objmgr.GetSpellClickInfoMapBounds(GetEntry());
        for(SpellClickInfoMap::const_iterator itr = clickPair.first; itr != clickPair.second; ++itr)
        {
            if (unit->GetTypeId() == TYPEID_UNIT || itr->second.IsFitToRequirements((Player*)unit))
            {
                Unit *caster = (itr->second.castFlags & 0x1) ? unit : this;
                Unit *target = (itr->second.castFlags & 0x2) ? unit : this;

                caster->CastSpell(target, itr->second.spellId, true);
            }
        }
        if(unit->GetTypeId() == TYPEID_PLAYER)
        {
            // it should be added only on rider enter?
            if(((Player*)unit)->GetGroup())
                ((Player*)unit)->SetGroupUpdateFlag(GROUP_UPDATE_VEHICLE);

            ((Player*)unit)->SetFarSightGUID(GetGUID());

            BuildVehicleActionBar((Player*)unit);
        }

        if(!(GetVehicleFlags() & VF_FACTION))
            setFaction(unit->getFaction());

        if(GetVehicleFlags() & VF_CANT_MOVE)
        {
            WorldPacket data2(SMSG_FORCE_MOVE_ROOT, 10);
            data2.append(GetPackGUID());
            data2 << (uint32)(2);
            SendMessageToSet(&data2,false);
        }

        if(GetVehicleFlags() & VF_NON_SELECTABLE)
            SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
    }
    if(seat->second.vs_flags & SF_UNATTACKABLE)
        unit->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);

    EmptySeatsCountChanged();
}

void Vehicle::RemovePassenger(Unit *unit)
{
    SeatMap::iterator seat;
    for(seat = m_Seats.begin(); seat != m_Seats.end(); ++seat)
    {
        if((seat->second.flags & (SEAT_FULL | SEAT_VEHICLE_FREE | SEAT_VEHICLE_FULL)) && seat->second.passenger == unit)
        {
            unit->SetVehicleGUID(0);

            if(seat->second.vs_flags & SF_MAIN_RIDER)
            {
                RemoveSpellsCausingAura(SPELL_AURA_CONTROL_VEHICLE);
                if(unit->GetTypeId() == TYPEID_PLAYER)
                {
                    ((Player*)unit)->SetClientControl(unit, 1);
                    ((Player*)unit)->SetMoverInQueve(NULL);
                    WorldPacket data(SMSG_PET_SPELLS, 8+4);
                    data << uint64(0);
                    data << uint32(0);
                    ((Player*)unit)->GetSession()->SendPacket(&data);

                    if(((Player*)unit)->GetGroup())
                        ((Player*)unit)->SetGroupUpdateFlag(GROUP_UPDATE_VEHICLE);
                }
                unit->SetCharm(NULL);
                SetCharmerGUID(NULL);
                setFaction(GetCreatureInfo()->faction_A);
            }
            if(GetVehicleFlags() & VF_NON_SELECTABLE)
                RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            if(seat->second.vs_flags & SF_UNATTACKABLE)
                unit->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            // restore player control
            if(unit->GetTypeId() == TYPEID_PLAYER)
            {
                ((Player*)unit)->SetFarSightGUID(NULL);

                if(seat->second.vs_flags & SF_CAN_CAST)
                {
                    WorldPacket data0(SMSG_FORCE_MOVE_UNROOT, 10);
                    data0.append(unit->GetPackGUID());
                    data0 << (uint32)(2);                        // can rotate
                    unit->SendMessageToSet(&data0,true);
                }
                else
                {
                    WorldPacket data1(SMSG_FORCE_MOVE_UNROOT, 10);
                    data1.append(unit->GetPackGUID());
                    data1 << (uint32)(0);                        // cannot rotate
                    unit->SendMessageToSet(&data1,true);
                }
            }
            unit->m_SeatData.OffsetX = 0.0f;
            unit->m_SeatData.OffsetY = 0.0f;
            unit->m_SeatData.OffsetZ = 0.0f;
            unit->m_SeatData.Orientation = 0.0f;
            unit->m_SeatData.c_time = 0;
            unit->m_SeatData.dbc_seat = 0;
            unit->m_SeatData.seat = 0;
            unit->m_SeatData.s_flags = 0;
            unit->m_SeatData.v_flags = 0;

            seat->second.passenger = NULL;
            seat->second.flags = SEAT_FREE;
            EmptySeatsCountChanged();
            break;
        }
    }
}

void Vehicle::RemoveAllPassengers()
{
    for(SeatMap::iterator itr = m_Seats.begin(); itr != m_Seats.end(); ++itr)
    {
        if(itr->second.flags & SEAT_FULL)
        {
            if(Unit *passenger = itr->second.passenger)                     // this cant be NULL, but..
                passenger->ExitVehicle();
        }
        else if(itr->second.flags & (SEAT_VEHICLE_FULL | SEAT_VEHICLE_FREE))
        {
            if(Unit *passenger = itr->second.passenger)                     // this cant be NULL, but..
            {
                passenger->ExitVehicle();
                ((Vehicle*)passenger)->Dismiss();
            }
        }
    }
    // make sure everything is cleared
    InitSeats();
}

bool Vehicle::HasSpell(uint32 spell) const
{
    if(!m_VehicleData)
        return false;

    for(uint8 j = 0; j < MAX_VEHICLE_SPELLS; j++)
    {
        if(m_VehicleData->v_spells[j] == spell)
            return true;
    }

    return false;
}

void Vehicle::BuildVehicleActionBar(Player *plr) const
{
    WorldPacket data(SMSG_PET_SPELLS, 8+4+4+4+4*10+1+1);
    data << uint64(GetGUID());
    data << uint32(0x00000000);                     // creature family, not used in vehicles
    data << uint32(0x00000000);                     // unk
    data << uint32(0x00000101);                     // react state

    for(uint32 i = 0; i < MAX_VEHICLE_SPELLS; ++i)
    {
        data << uint16(m_VehicleData ? m_VehicleData->v_spells[i] : NULL);
        data << uint8(0) << uint8(i+8);
    }

    data << uint8(0);                               //aditional spells in spellbook, not used in vehicles

    uint8 cooldownsCount = m_CreatureSpellCooldowns.size() + m_CreatureCategoryCooldowns.size();
    data << uint8(cooldownsCount);
    time_t curTime = time(NULL);

    for(CreatureSpellCooldowns::const_iterator itr = m_CreatureSpellCooldowns.begin(); itr != m_CreatureSpellCooldowns.end(); ++itr)
    {
        time_t cooldown = (itr->second > curTime) ? (itr->second - curTime) * IN_MILISECONDS : 0;

        data << uint32(itr->first);                         // spellid
        data << uint16(0);                                  // spell category?
        data << uint32(cooldown);                           // cooldown
        data << uint32(0);                                  // category cooldown
    }

    for(CreatureSpellCooldowns::const_iterator itr = m_CreatureCategoryCooldowns.begin(); itr != m_CreatureCategoryCooldowns.end(); ++itr)
    {
        time_t cooldown = (itr->second > curTime) ? (itr->second - curTime) * IN_MILISECONDS : 0;

        data << uint32(itr->first);                         // spellid
        data << uint16(0);                                  // spell category?
        data << uint32(0);                                  // cooldown
        data << uint32(cooldown);                           // category cooldown
    }

    plr->GetSession()->SendPacket(&data);

    data.Initialize(SMSG_PET_GUIDS, 12);
    data << uint32(1);                                        // count
    data << uint64(GetGUID());
    plr->GetSession()->SendPacket(&data);
}
