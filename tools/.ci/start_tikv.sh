nohup tiup playground v5.4.2 --mode tikv-slim  -T tikv00 --without-monitor --pd.port 2379 &
sleep 10
nohup tiup playground v5.4.2 --mode tikv-slim  -T tikv01 --without-monitor --pd.port 2381 &
sleep 10
nohup tiup playground v5.4.2 --mode tikv-slim  -T tikv02 --without-monitor --pd.port 2383 &
sleep 10
nohup tiup playground v5.4.2 --mode tikv-slim  -T tikv03 --without-monitor --pd.port 2385 &
sleep 10
