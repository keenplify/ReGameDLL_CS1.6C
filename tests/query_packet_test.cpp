#include "../query/query_packet.h"
#include <vector>
#include <cstdlib>
void check(bool v) { if(!v) std::abort(); }
int main() {
 BotQuery::Bot bot={"Native",42.0f};
 std::vector<uint8_t> info={255,255,255,255,'I',48,'s',0,'m',0,'c',0,'g',0,10,0,2,20,2,'d','l',0,0,'1',0};
 for(size_t n=0;n<info.size();++n) {auto copy=info; check(!BotQuery::Rewrite(copy.data(),n,&bot,1)); check(copy==info);}
 check(BotQuery::Rewrite(info.data(),info.size(),&bot,1)); check(info[18]==0);
 std::vector<uint8_t> players={255,255,255,255,'D',1,0,'N','a','t','i','v','e',0,0,0,0,0,0,0,0,0};
 for(size_t n=0;n<players.size();++n) {auto copy=players; check(!BotQuery::Rewrite(copy.data(),n,&bot,1)); check(copy==players);}
 check(BotQuery::Rewrite(players.data(),players.size(),&bot,1));
 float seconds; std::memcpy(&seconds,players.data()+18,4); check(seconds==42);
 auto human=players; human[7]='H'; check(!BotQuery::Rewrite(human.data(),human.size(),&bot,1));
 std::vector<uint8_t> legacy={255,255,255,255,'m','a',0,'s',0,'m',0,'c',0,'g',0,2,20,48,'d','l',0,0,1,2};
 check(BotQuery::Rewrite(legacy.data(),legacy.size(),&bot,1)); check(legacy.back()==0);
 legacy[4]='x'; auto copy=legacy; check(!BotQuery::Rewrite(copy.data(),copy.size(),&bot,1)); check(copy==legacy);
}
