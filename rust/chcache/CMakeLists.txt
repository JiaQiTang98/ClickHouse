if (COMPILER_CACHE STREQUAL "chcache")
    corrosion_import_crate(
        MANIFEST_PATH Cargo.toml
        PROFILE release
        LOCKED
    )

    corrosion_set_env_vars(
        chcache
        RUSTFLAGS=
        RUST_CFLAGS=
        RUST_CXXFLAGS=
        CFLAGS=
        CXXFLAGS=
    )

    corrosion_set_hostbuild(chcache)
endif()
