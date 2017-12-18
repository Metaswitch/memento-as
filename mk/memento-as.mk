MEMENTO-AS_DIR := ${ROOT}/plugins/memento-as/src

memento-as:
	make -C ${MEMENTO-AS_DIR}

memento-as_test:
	make -C ${MEMENTO-AS_DIR} test

memento-as_full_test:
	make -C ${MEMENTO-AS_DIR} full_test

memento-as_clean:
	make -C ${MEMENTO-AS_DIR} clean

.PHONY: memento-as memento-as_test memento-as_clean
