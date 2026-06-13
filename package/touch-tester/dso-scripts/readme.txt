1)connect rigol scope to network and notedown its ip address
2)open webpage of rigol scope and enable scpi interface(port 5555)
3)now rigol scope is ready to be controlled through shell script
web based monitoring of dso screen: http://192.168.1.7/control.html
copy current setup: ./rigol-tool.sh --command=copy-setup --ip=192.168.1.7 --output=dso-configs/touch-tester/final-test.json
apply setup       : ./rigol-tool.sh --command=apply-setup --ip=192.168.1.7 --input=dso-configs/touch-tester/test-config.json --verbose

