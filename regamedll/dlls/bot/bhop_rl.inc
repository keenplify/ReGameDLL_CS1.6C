// SPDX-License-Identifier: MIT
// Included inside the private training namespace; never enabled on a match server.
cvar_t rlEnabled={"bot_bhop_rl","0",FCVAR_SERVER,0,nullptr};
struct RLState {
    bool active=false,reset=false,pending=false,finished=false,reached=false;
    int id=0,slot=0,userid=0,seq=0,steps=0,stalled=0;
    int actions[5]{};
    int holdSteps=0,held=0;
    Vector start,goal;
    float yaw=0,previousDistance=0,bestDistance=0,waitingSince=0;
} rlTraining;
RLState rlView[33];
float rlViewNextCamera=0,rlViewNextFollow=0;
int rlViewCamera=OBS_IN_EYE;
bool RLViewer() {
    const char *flag=std::getenv("BHOP_RL_VIEW");
    return flag && !strcmp(flag,"1") && IS_DEDICATED_SERVER() && gpGlobals->maxClients==32
        && enabled.value==1 && rlEnabled.value==1 && CVAR_GET_FLOAT("sv_lan")==1;
}
// Ghost only player peers during synchronous movement; map geometry stays solid.
struct RLViewGhost {
    CBasePlayer *players[32]{}; int solids[32]{},count=0;
    explicit RLViewGhost(CBot *bot) {
        if(!RLViewer())return;
        for(int i=1;i<=gpGlobals->maxClients;++i){auto *p=UTIL_PlayerByIndex(i);
            if(!p || p==bot || !p->IsBot() || p->pev->solid==SOLID_NOT)continue;
            players[count]=p;solids[count++]=p->pev->solid;p->pev->solid=SOLID_NOT;
        }
    }
    ~RLViewGhost(){for(int i=0;i<count;++i)players[i]->pev->solid=solids[i];}
};
float RLBound(float x){return std::max(-1.0f,std::min(1.0f,x));}
bool RLSupported(const Vector &p) {
    TraceResult tr;
    UTIL_TraceLine(p,p-Vector(0,0,54),ignore_monsters,nullptr,&tr);
    return !tr.fStartSolid && !tr.fAllSolid && tr.flFraction<1 && tr.vecPlaneNormal.z>=.7f && ClearHull(p,p);
}
void RLResetState(bool viewer) {
    const int offset=viewer?1:0;
    float slot=0;
    if(viewer && (!Number(1,slot) || slot!=std::floor(slot) || slot<1 || slot>32)){Error("invalid viewer slot");return;}
    RLState &rl=viewer?rlView[int(slot)]:rlTraining;
    if(!NoNavigation() || !(viewer?RLViewer():Lab()) || episode.active || (CMD_ARGC()!=9+offset && CMD_ARGC()!=10+offset)){Error("rl reset requires isolated NAV-free lab: id start_xyz goal_xyz yaw");return;}
    float a[8];for(int i=0;i<8;++i)if(!Number(i+1+offset,a[i]) || std::fabs(a[i])>1000000){Error("invalid rl reset number");return;}
    if(a[0]<1 || a[0]!=std::floor(a[0]) || std::fabs(a[7])>180){Error("invalid rl id or yaw");return;}
    float hold=0;
    if(CMD_ARGC()==10+offset && (!Number(9+offset,hold) || hold!=std::floor(hold) || hold<0 || hold>50)){Error("hold steps must be 0..50");return;}
    const Vector start(a[1],a[2],a[3]),goal(a[4],a[5],a[6]);
    const float distance=(goal-start).Length2D();
    if(distance<64 || distance>2048 || !RLSupported(start) || !RLSupported(goal)){Error("rl endpoints fail hull/floor/range validation");return;}
    for(int i=1;i<=gpGlobals->maxClients;++i){auto *b=UTIL_PlayerByIndex(i);
        if((viewer && i!=int(slot)) || !b || !b->IsBot() || !b->IsAlive() || b->pev->maxspeed<100 || (b->pev->flags&FL_FROZEN) || CSGameRules()->IsFreezePeriod())continue;
        rl={};rl.active=true;rl.reset=true;rl.id=int(a[0]);rl.slot=i;rl.userid=GETPLAYERUSERID(b->edict());
        rl.start=start;rl.goal=goal;rl.yaw=a[7];rl.holdSteps=int(hold);rl.previousDistance=rl.bestDistance=distance;ReportEnvironment();return;
    }
    Error("rl bot not movable yet");
}
void RLActionState(bool viewer) {
    const int offset=viewer?1:0;float slot=0;
    if(viewer && (!Number(1,slot) || slot!=std::floor(slot) || slot<1 || slot>32)){Error("invalid viewer slot");return;}
    RLState &rl=viewer?rlView[int(slot)]:rlTraining;
    if(!NoNavigation() || !(viewer?RLViewer():Lab()) || !rl.active || rl.reset || rl.pending || CMD_ARGC()!=8+offset){Error("rl action requires waiting episode: id seq forward side turn jump duck");return;}
    float a[7];for(int i=0;i<7;++i)if(!Number(i+1+offset,a[i]) || a[i]!=std::floor(a[i])){Error("rl action must be integral");return;}
    if(a[0]!=rl.id || a[1]!=rl.seq){Error("stale rl action");return;}
    for(int i=0;i<5;++i)if(a[i+2]<0 || a[i+2]>=(i<3?5:2)){Error("rl action outside bounds");return;}
    for(int i=0;i<5;++i)rl.actions[i]=int(a[i+2]);
    rl.pending=true;
}
void RLObserve(RLState &rl,CBot *bot,float reward,bool done,const char *reason) {
    const auto &p=bot->pev->origin;const auto &v=bot->pev->velocity;
    const float yaw=bot->pev->v_angle.y*Pi/180,c=std::cos(yaw),s=std::sin(yaw);
    const Vector goal=rl.goal-p;
    float o[24]{};
    o[0]=RLBound((v.x*c+v.y*s)/600);o[1]=RLBound((-v.x*s+v.y*c)/600);o[2]=RLBound(v.z/400);
    o[3]=RLBound((goal.x*c+goal.y*s)/1024);o[4]=RLBound((-goal.x*s+goal.y*c)/1024);o[5]=RLBound(goal.z/256);
    o[6]=(bot->pev->flags&FL_ONGROUND)?1:0;o[7]=(bot->pev->flags&FL_DUCKING)?1:0;
    for(int i=0;i<8;++i){const float angle=yaw+i*Pi/4;TraceResult tr;
        UTIL_TraceHull(p,p+Vector(std::cos(angle)*128,std::sin(angle)*128,0),ignore_monsters,
            (bot->pev->flags&FL_DUCKING)?head_hull:human_hull,bot->edict(),&tr);
        o[8+i]=(tr.fStartSolid||tr.fAllSolid)?0:tr.flFraction;
    }
    for(int i=0;i<4;++i){const float angle=yaw+i*Pi/2;const Vector probe=p+Vector(std::cos(angle)*48,std::sin(angle)*48,0);TraceResult tr;
        UTIL_TraceLine(probe,probe-Vector(0,0,128),ignore_monsters,bot->edict(),&tr);
        o[16+i]=(tr.fStartSolid||tr.fAllSolid)?1:tr.flFraction;
    }
    TraceResult floor,head;UTIL_TraceLine(p,p-Vector(0,0,128),ignore_monsters,bot->edict(),&floor);
    o[20]=floor.vecPlaneNormal.x*c+floor.vecPlaneNormal.y*s;o[21]=-floor.vecPlaneNormal.x*s+floor.vecPlaneNormal.y*c;
    UTIL_TraceHull(p,p+Vector(0,0,64),ignore_monsters,(bot->pev->flags&FL_DUCKING)?head_hull:human_hull,bot->edict(),&head);
    o[22]=(head.fStartSolid||head.fAllSolid)?0:head.flFraction;o[23]=std::min(1.0f,goal.Length2D()/2048);
    const bool reached=!strcmp(reason,"reached");
    const float length=std::max(64.0f,(rl.goal-rl.start).Length2D());
    const float progress=std::max(0.0f,std::min(1.0f,1-goal.Length2D()/length));
    const int score=reached?100+int(100*std::max(0.0f,1-rl.steps/300.0f)):int(99*progress);
    if(RLViewer() && (int(bot->pev->frags)!=score || rl.steps==0 || done)){
        bot->pev->frags=score;
        MESSAGE_BEGIN(MSG_ALL,gmsgScoreInfo);WRITE_BYTE(bot->entindex());WRITE_SHORT(score);
        WRITE_SHORT(bot->m_iDeaths);WRITE_SHORT(0);WRITE_SHORT(bot->m_iTeam);MESSAGE_END();
    }
    std::ostringstream out;out<<"BHOP_TRAIN {\"type\":\"rl_step\",\"version\":1,\"id\":"<<rl.id<<",\"slot\":"<<rl.slot<<",\"seq\":"<<rl.seq
        <<",\"reward\":"<<reward<<",\"done\":"<<(done?"true":"false")<<",\"reason\":\""<<reason<<"\",\"seconds\":"<<rl.steps*.04f
        <<",\"feet\":["<<p.x<<","<<p.y<<","<<(p.z-((bot->pev->flags&FL_DUCKING)?18:36))<<"],\"world_time\":"<<gpGlobals->time<<",\"hold_steps\":"<<rl.held<<",\"required_hold_steps\":"<<rl.holdSteps<<",\"navigation_score\":"<<score<<",\"score_version\":1,\"goal_distance\":"<<goal.Length2D()<<",\"speed\":"<<v.Length2D()<<",\"observation\":[";
    for(int i=0;i<24;++i){if(i)out<<",";out<<o[i];}out<<"]}\n";SERVER_PRINT(out.str().c_str());
    rl.waitingSince=gpGlobals->time;if(done){rl.active=false;rl.finished=true;rl.reached=!strcmp(reason,"reached");}
}
bool RLTick(CBot *bot) {
    if(!NoNavigation())return false;
    const bool viewer=RLViewer();
    RLState &rl=viewer?rlView[bot->entindex()]:rlTraining;
    if(!viewer && !Lab()){rl={};return false;}
    RLViewGhost ghost(bot);
    if(!rl.active)return bot->IsAlive() && bot->pev->maxspeed>=100 && !(bot->pev->flags&FL_FROZEN);
    if(bot->entindex()!=rl.slot || !bot->IsAlive() || GETPLAYERUSERID(bot->edict())!=rl.userid){rl={};Error("rl bot changed");return true;}
    if(rl.reset){
        bot->StandUp();bot->Run();bot->pev->velocity=Vector(0,0,0);bot->pev->basevelocity=Vector(0,0,0);
        bot->pev->punchangle=Vector(0,0,0);bot->pev->oldbuttons=0;bot->pev->fuser2=0;
        bot->pev->flags&=~(FL_ONGROUND|FL_DUCKING);bot->pev->groundentity=nullptr;
        UTIL_SetOrigin(bot->pev,rl.start);bot->pev->v_angle=Vector(0,rl.yaw,0);
        PLAYER_RUN_MOVE(bot->edict(),bot->pev->v_angle,0,0,0,0,0,40);
        rl.reset=false;RLObserve(rl,bot,0,false,"reset");return true;
    }
    if(!rl.pending){if(gpGlobals->time-rl.waitingSince>30)RLObserve(rl,bot,-10,true,"action_timeout");return true;}
    rl.pending=false;
    const float movement[5]={-250,-125,0,125,250};const float turn[5]={-12,-6,0,6,12};
    const float forward=movement[rl.actions[0]],side=movement[rl.actions[1]];
    bot->pev->v_angle.y=Wrap(bot->pev->v_angle.y+turn[rl.actions[2]]);
    const int buttons=(rl.actions[3]?IN_JUMP:0)|(rl.actions[4]?IN_DUCK:0);
    PLAYER_RUN_MOVE(bot->edict(),bot->pev->v_angle,forward,side,0,buttons,0,40);
    ++rl.steps;++rl.seq;
    const Vector delta=rl.goal-bot->pev->origin;const float distance=delta.Length2D();
    float reward=std::max(-1.0f,std::min(1.0f,(rl.previousDistance-distance)*.02f))-.004f;
    rl.previousDistance=distance;
    if(distance<rl.bestDistance-4){rl.bestDistance=distance;rl.stalled=0;}else ++rl.stalled;
    const bool atGoal=distance<32 && std::fabs((bot->pev->origin.z-((bot->pev->flags&FL_DUCKING)?18:36))-(rl.goal.z-36))<8 && (bot->pev->flags&FL_ONGROUND);
    if(atGoal && (rl.holdSteps==0 || bot->pev->velocity.Length2D()<80))++rl.held;else rl.held=0;
    const bool reached=atGoal && (rl.holdSteps==0 || rl.held>=rl.holdSteps);
    const bool failed=rl.steps>=300 || rl.stalled>=150 || bot->pev->origin.z<rl.start.z-128 || bot->pev->waterlevel>0;
    if(reached)reward+=10;else if(failed)reward-=10;
    RLObserve(rl,bot,reward,reached||failed,reached?"reached":failed?"timeout_stuck_or_fall":"step");return true;
}

void RLViewFrame() {
    if(!RLViewer())return;
    if(gpGlobals->time>=rlViewNextCamera){rlViewCamera=rlViewCamera==OBS_IN_EYE?OBS_CHASE_FREE:OBS_IN_EYE;rlViewNextCamera=gpGlobals->time+5;}
    if(gpGlobals->time<rlViewNextFollow)return;
    rlViewNextFollow=gpGlobals->time+.25f;
    CBasePlayer *target=nullptr;float best=-1e9f;
    // Unfinished attempts take priority. Compare normalized current progress
    // across route lengths; once all finish, fastest success wins the view.
    for(int i=1;i<=32;++i){auto *p=UTIL_PlayerByIndex(i);const auto &state=rlView[i];
        if(!p || !p->IsBot() || !p->IsAlive() || GETPLAYERUSERID(p->edict())!=state.userid || (!state.active && !state.finished))continue;
        const float length=std::max(64.0f,(state.goal-state.start).Length2D());
        const float progress=std::max(-1.0f,std::min(1.0f,1-state.previousDistance/length));
        const float score=state.active?10+progress:state.reached?5-state.steps*.001f:progress;
        if(score>best){best=score;target=p;}
    }
    for(int i=1;i<=32;++i){auto *p=UTIL_PlayerByIndex(i);
        if(!p || p->IsDormant() || p->IsBot() || (p->pev->flags&FL_PROXY))continue;
        if(p->m_iJoiningState==READINGLTEXT)continue;
        if(p->m_iTeam!=SPECTATOR)p->CSPlayer()->JoinTeam(SPECTATOR);
        if(target && (p->m_hObserverTarget!=target || p->pev->iuser1!=rlViewCamera)){
            p->m_flNextFollowTime=0;p->Observer_FindNextPlayer(false,STRING(target->pev->netname));p->Observer_SetMode(rlViewCamera);
        }
    }
}
void RLViewRoster() {
    if(!RLViewer()){Error("viewer disabled");return;}
    ReportEnvironment();
    std::ostringstream out;out<<"BHOP_TRAIN {\"type\":\"rl_roster\",\"slots\":[";bool first=true;
    for(int i=1;i<=32;++i){auto *p=UTIL_PlayerByIndex(i);
        if(!p || !p->IsBot() || !p->IsAlive() || p->pev->maxspeed<100 || (p->pev->flags&FL_FROZEN) || CSGameRules()->IsFreezePeriod())continue;
        if(!first)out<<",";
        first=false;out<<i;
    }
    out<<"]}\n";SERVER_PRINT(out.str().c_str());
}

void RLViewGeneration() {
    float generation=0;
    if(!RLViewer() || CMD_ARGC()!=2 || !Number(1,generation) || generation<0 || generation>1000000 || generation!=std::floor(generation))return;
    static const char *const names[32]={
        "Alex","Blake","Casey","Drew","Elliot","Finn","Gabriel","Harper",
        "Jamie","Kai","Logan","Morgan","Noah","Oliver","Parker","Quinn",
        "Riley","Sam","Taylor","Theo","Avery","Cameron","Dylan","Emery",
        "Hayden","Jordan","Leo","Micah","Rowan","Sasha","Toby","Charlie"
    };
    for(int i=1;i<=32;++i){auto *p=UTIL_PlayerByIndex(i);if(!p || !p->IsBot())continue;
        SET_CLIENT_KEY_VALUE(i,GET_INFO_BUFFER(p->edict()),"name",UTIL_VarArgs("%s [EXP G%d]",names[i-1],int(generation)));
    }
    UTIL_ClientPrintAll(HUD_PRINTTALK,UTIL_VarArgs("[Experimental PPO] Checkpoint generation %d. 31 actors; unvalidated policy.\n",int(generation)));
}
