ZIPKAT_PATH=/home/jp2585/build
ZIPLOG_ORDER_IP=192.168.99.21
ZIPLOG_ORDER_PORT=6666
python3 e1_e2_local.py \
		--ziplog_order_binary ${ZIPKAT_PATH}/order \
        --ziplog_order_port ${ZIPLOG_ORDER_PORT} \
        --ziplog_order_cpus "1" \
		--ziplog_storage_binary ${ZIPKAT_PATH}/storage  \
		--zipkat_storage_binary ${ZIPKAT_PATH}/zipkat   \
		--client_binary ${ZIPKAT_PATH}/retwisClient     \
		--config_file_directory ${ZIPKAT_PATH}          \
		--key_file ${ZIPKAT_PATH}/keys                  \
		--suite_directory ${ZIPKAT_PATH}/rafael_benchmark
