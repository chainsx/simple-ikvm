project(
    'simple-ikvm',
    'cpp',
    version: '1.0',
    license: 'GPLv2',
    default_options: [
        'cpp_std=c++17',
        'werror=true',
    ],
    meson_version: '>=1.1.1',
)

executable(
    'simple-ikvm',
    [
        'ikvm_args.cpp',
        'ikvm_input.cpp',
        'ikvm_manager.cpp',
        'ikvm_server.cpp',
        'ikvm_video.cpp',
        'simple-ikvm.cpp',
    ],
    dependencies: [
        dependency('libvncserver'),
        dependency('threads'),
        dependency('boost'),
    ],
    install: true,
)
