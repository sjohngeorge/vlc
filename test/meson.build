subdir('src')
subdir('modules')
subdir('libvlc')

foreach vlc_test: vlc_tests
    if not vlc_test.has_key('name')
        error('Got invalid vlc_tests entry without \'name\' key')
    endif
    if not vlc_test.has_key('sources')
        error('Got invalid vlc_tests entry without \'sources\' key')
    endif

    # This list MUST be kept in sync with the keys used below!
    valid_dict_keys = [
        'name',
        'sources',
        'moc_headers',
        'moc_sources',
        'suite',
        'link_with',
        'module_depends',
        'dependencies',
        'c_args',
        'cpp_args',
        'objc_args',
        'include_directories',
        'env',
    ]

    foreach key : vlc_test.keys()
        if key not in valid_dict_keys
            error('Invalid key \'@0@\' found in vlc_tests entry for \'@1@\''
                .format(key, vlc_test['name']))
        endif
    endforeach

    common_args = [
        '-DSRCDIR="@0@"'.format(vlc_src_root + '/test/'),
        '-DTOP_BUILDDIR="@0@"'.format(vlc_build_root),
        '-DTOP_SRCDIR="@0@"'.format(vlc_src_root),
    ]

    disabled_dependencies = []
    test_modules_deps = []
    foreach module_name : vlc_test.get('module_depends', [])
        if module_name not in vlc_plugins_manifest.keys()
            error('Test entry \'@0@\' cannot depend on non-existant VLC plugin \'@1@\''
                  .format(vlc_test['name'], module_name))
        endif

        if not vlc_plugins_manifest[module_name].get('enabled', true)
            disabled_dependencies += module_name
        else
            test_modules_deps += vlc_plugins_targets[module_name]
        endif
    endforeach

    if disabled_dependencies != []
        # TODO: mark as skipped
        warning('Test \'@0@\' will be disabled, missing plugins: @1@'.format(
                vlc_test['name'], ', '.join(disabled_dependencies)))
        continue
    endif

    moc_sources = []
    if (vlc_test.has_key('moc_headers') or vlc_test.has_key('moc_sources')) and qt6_dep.found()
        moc_sources += qt6.preprocess(
            moc_headers: vlc_test.get('moc_headers', []),
            moc_sources: vlc_test.get('moc_sources', []),
            include_directories: qt_include_dir,
            dependencies: qt6_dep)
    endif

    test(vlc_test['name'],
        executable(vlc_test['name'], vlc_test['sources'], moc_sources,
            build_by_default: false,
            link_with: [vlc_test.get('link_with', []),
                vlc_libcompat],
            include_directories: [vlc_test.get('include_directories', []),
                vlc_include_dirs],
            dependencies: [vlc_test.get('dependencies', []),
                libvlccore_deps, opengl_dep],
            c_args: [vlc_test.get('c_args', []), common_args],
            cpp_args: [vlc_test.get('cpp_args', []), common_args],
            objc_args: [vlc_test.get('objc_args', []), common_args]),
            env: vlc_test.get('env', []),
        suite: [vlc_test.get('suite', []), 'test'],
        depends: [test_modules_deps])
endforeach

libvlc_demux_defines = []
libvlc_demux_deps = []
# TODO support !HAVE_DYNAMIC_PLUGINS
# libvlc_demux_defines=['-DHAVE_STATIC_MODULES']
# if libdvbpsi_dep.found()
#     libvlc_demux_defines += '-DHAVE_DVBPSI'
#     libvlc_demux_deps += [libdvbpsi_dep, aribb24_dep, libdvbcsa_dep]
# endif
# if libebml_dep.found() and libmatroska_dep.found()
#     libvlc_demux_defines += '-DHAVE_MATROSKA'
#     libvlc_demux_deps += [libebml_dep, libmatroska_dep, z_dep]
# endif

libvlc_demux_run = static_library ('libvlc_demux_run',
    files('src/input/demux-run.c', 'src/input/common.c', '../src/input/var.c'),
    c_args: libvlc_demux_defines,
    dependencies: libvlc_demux_deps,
    include_directories: [vlc_include_dirs],
    link_with: [libvlc, libvlccore, vlc_libcompat])

libvlc_demux_dec_defines = [libvlc_demux_defines, '-DHAVE_DECODERS']
libvlc_demux_dec_run = static_library ('libvlc_demux_dec_run',
    files('src/input/demux-run.c', 'src/input/common.c', '../src/input/var.c', 'src/input/decoder.c'),
    c_args: libvlc_demux_dec_defines,
    dependencies: libvlc_demux_deps,
    include_directories: [vlc_include_dirs],
    link_with: [libvlc, libvlccore, vlc_libcompat])

executable('vlc-demux-run', 'vlc-demux-run.c',
    include_directories: [vlc_include_dirs],
    link_with: [libvlc_demux_run, libvlc, libvlccore, vlc_libcompat],
    install: false,
    win_subsystem: 'console')

executable('vlc-demux-dec-run', 'vlc-demux-run.c',
    include_directories: [vlc_include_dirs],
    link_with: [libvlc_demux_dec_run, libvlc, libvlccore, vlc_libcompat],
    install: false,
    win_subsystem: 'console')

executable('vlc-window', 'vlc-window.c',
    include_directories: [vlc_include_dirs],
    link_with: [libvlc, libvlccore, vlc_libcompat],
    c_args: common_args,
    install: false,
    win_subsystem: 'console')
