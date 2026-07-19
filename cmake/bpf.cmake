set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR bpf)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_C_FLAGS_INIT "-target bpf -O2 -g -Wall -Werror")

find_program(BPFTOOL_EXE NAMES bpftool)
find_program(CLANG_EXE NAMES clang)

if(BPFTOOL_EXE AND CLANG_EXE)
    set(BPF_SRC "${CMAKE_SOURCE_DIR}/bpf/kbx_trace.bpf.c")
    set(BPF_OBJ "${CMAKE_BINARY_DIR}/kbx_trace.bpf.o")
    set(BPF_SKEL "${CMAKE_SOURCE_DIR}/bpf/kbx_trace.skel.h")

    add_custom_command(
        OUTPUT ${BPF_SKEL}
        COMMAND ${CLANG_EXE} -O2 -g -target bpf -I/usr/include/x86_64-linux-gnu -I${CMAKE_SOURCE_DIR}/bpf -c ${BPF_SRC} -o ${BPF_OBJ}
        COMMAND ${BPFTOOL_EXE} gen skeleton ${BPF_OBJ} > ${BPF_SKEL}
        DEPENDS ${BPF_SRC}
        COMMENT "Compiling eBPF program and generating skeleton"
    )
    add_custom_target(bpf_gen ALL DEPENDS ${BPF_SKEL})
endif()

function(target_add_bpf target)
    if(TARGET bpf_gen)
        add_dependencies(${target} bpf_gen)
    endif()
endfunction()
