set -x

mkdir -p $1
sudo cpupower frequency-set -g performance

for i in `seq 6`; do
	taskset 0x4 ./gettime_perf
done  > $1/gettime_perf.log

for i in `seq 3`; do
	taskset 0x4  ./gettime_perf_cold > $1/gettime_perf_cold.log.$i
done

for i in `seq 3`; do
	taskset 0x4  ./gettime_perf_cold ns > $1/gettime_perf_cold.ns.log.$i
done
exit 0

for i in `seq 2`; do
(
	cd /home/avagin/git/vdsotest/
	for i in `./vdsotest --help | grep clock`; do
		taskset 0x4 ./vdsotest -d 2 $i bench
	done
) > $1/vdsotest.log.$i
done
