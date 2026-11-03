// SPDX-License-Identifier: MIT
#pragma once

struct CompetitiveSiteCamp {
 int site = 0;
 float since = -1;
 float cooldown[3] = {};
 int Update(float now, int current, bool eligible, bool enemyFound) {
  if(!eligible) { site=0; since=-1; return 0; }
  if(current<1 || current>2) { site=0; since=-1; return 0; }
  if(enemyFound) {
   site=current; since=now; cooldown[current]=0; return 0;
  }
  if(site!=current || since<0 || now<since) {site=current;since=now;}
  return now<cooldown[current] || now-since>=30 ? 3-current : 0;
 }
 void Rotated(float now) {
  if(site>=1 && site<=2)cooldown[site]=now+60;
  site=0;since=-1;
 }
};
