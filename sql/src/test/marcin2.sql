drop table t3;
create table t3(id int, val int);
insert into t3 values(2,6);
insert into t3 values(2,NULL);
insert into t3 values(2,5);
insert into t3 values(1,NULL);
insert into t3 values(1,5);
insert into t3 values(1,6);
insert into t3 values(NULL,5);
insert into t3 values(NULL,6);
insert into t3 values(NULL,NULL);

#those 2 don't sort (bad server_output?)
select * from t3 order by val;
select * from t3 order by id;

#but those 2 do
select * from t3 order by val,id;
select * from t3 order by id,val;

#this one works, and I think it shouldn't 
select sum(*) from t3;

#this one, although wrong, results in core-dump of sql_client :)
select sum(*),val from t3 group by val ;


