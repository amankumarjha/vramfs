CXX ?= g++
CXXFLAGS += -std=c++20 $(shell pkg-config fuse3 --cflags) -I include/
LDFLAGS += $(shell pkg-config fuse3 --libs) -l OpenCL

ifeq ($(DEBUG), 1)
	CXXFLAGS += -g -DDEBUG -Wall -Werror
endif

ifeq ($(USE_CUDA),1)
    CXXFLAGS += -DUSE_CUDA
    LDFLAGS += -lcudart
endif

bin/vramfs: build/util.o build/memory.o build/entry.o build/file.o build/dir.o build/symlink.o build/vramfs.o | bin
	$(CXX) -o $@ $^ $(LDFLAGS)

build bin:
	@mkdir -p $@

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

bin/nbd_server: tools/nbd_backing/nbd_server.cpp | bin
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

bin/cuda_bench: tools/nbd_backing/cuda_bench.cpp | bin
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

bin/nbdkit_cuda_plugin.so: tools/nbd_backing/nbdkit_cuda_plugin.cpp src/cuda_memory.cpp | bin
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $^ $(LDFLAGS)

bin/test_cuda: tests/test_cuda_memory.cpp src/cuda_memory.cpp | bin
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: test
test: bin/test_cuda
	./bin/test_cuda

.PHONY: clean
clean:
	rm -rf build/ bin/
