create table if not exists data_01223 (key Int) Engine=Memory();
create table if not exists dist_layer_01223 as data_01223 Engine=Distributed(test_cluster_two_shards, currentDatabase(), data_01223);
create table if not exists dist_01223 as data_01223 Engine=Distributed(test_cluster_two_shards, currentDatabase(), dist_layer_01223);

select * from dist_01223;

insert into data_01223 select * from numbers(3);

select 'DISTINCT ORDER BY';
select distinct * from dist_01223 order by key;
select 'GROUP BY ORDER BY';
select * from dist_01223 group by key order by key;
select 'GROUP BY ORDER BY LIMIT';
select * from dist_01223 group by key order by key limit 1;
select 'HAVING';
select * from dist_01223 having key = 1;
select 'GROUP BY HAVING';
select * from dist_01223 group by key having key = 1;
select 'ORDER BY';
select * from dist_01223 order by key;
select 'ORDER BY LIMIT';
select * from dist_01223 order by key limit 1;
select 'ORDER BY LIMIT BY';
select * from dist_01223 order by key limit 1 by key;
select 'cluster() ORDER BY';
select * from cluster(test_cluster_two_shards, currentDatabase(), dist_01223) order by key;
select 'cluster() GROUP BY ORDER BY';
select * from cluster(test_cluster_two_shards, currentDatabase(), dist_01223) group by key order by key;

select 'LEFT JOIN';
select toInt32(number) key, b.key from numbers(2) a left join (select distinct * from dist_01223) b using key order by b.key;
select 'RIGHT JOIN';
select toInt32(number) key, b.key from numbers(2) a right join (select distinct * from dist_01223) b using key order by b.key;

-- more data for GROUP BY
insert into data_01223 select number%3 from numbers(30);

-- group_by_two_level_threshold
select 'GROUP BY ORDER BY group_by_two_level_threshold';
select * from dist_01223 group by key order by key settings
group_by_two_level_threshold=1,
group_by_two_level_threshold_bytes=1;

-- distributed_aggregation_memory_efficient
select 'GROUP BY ORDER BY distributed_aggregation_memory_efficient';
select * from dist_01223 group by key order by key settings
distributed_aggregation_memory_efficient=1;

-- distributed_aggregation_memory_efficient/group_by_two_level_threshold
select 'GROUP BY ORDER BY distributed_aggregation_memory_efficient/group_by_two_level_threshold';
select * from dist_01223 group by key order by key settings
group_by_two_level_threshold=1,
group_by_two_level_threshold_bytes=1,
distributed_aggregation_memory_efficient=1;

drop table dist_01223;
drop table dist_layer_01223;
drop table data_01223;
