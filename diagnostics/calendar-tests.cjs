const assert=require('node:assert/strict');
const {day,cells,moveMonth,formatTime}=require('../ui/calendar.js');
const time=day(2024,0,1);time.setHours(14,3,9);
assert.equal(formatTime(time,"HH:mm:ss"),'14:03:09');
assert.equal(formatTime(time,"h:mm tt",'am','pm'),'2:03 pm');
assert.equal(formatTime(time,"HH 'hours' mm"),'14 hours 03');
let checks=0;
for(let year=1996;year<=2104;year++) for(let month=0;month<12;month++) for(let first=0;first<7;first++) {
 const dates=cells(year,month,first);
 const offset=(day(year,month,1).getDay()-first+7)%7;
 assert.equal(dates.length,Math.max(5,Math.ceil((offset+day(year,month+1,0).getDate())/7))*7); assert.equal(dates[0].getDay(),first);
 const current=dates.filter(d=>d.getMonth()===month&&d.getFullYear()===year);
 assert.equal(current.length,day(year,month+1,0).getDate());
 assert.equal(current[0].getDate(),1);
 for(let i=1;i<dates.length;i++) {const next=new Date(dates[i-1]);next.setDate(next.getDate()+1);assert.equal(next.getTime(),dates[i].getTime());}
 ++checks;
}
assert.equal(moveMonth(day(2024,0,31),1).getDate(),29);
assert.equal(moveMonth(day(2100,0,31),1).getDate(),28);
assert.equal(moveMonth(day(2024,0,31),-1).getFullYear(),2023);
console.log(`PASS: ${checks} calendar month/week-start cases, DST-safe consecutive dates, leap centuries and month/year transitions.`);
