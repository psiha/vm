/// \note
///   This file intentionally lacks header include guards.
///                                            (28.07.2011.) (Domagoj Saric)

#ifdef BOOST_MMAP_HEADER_ONLY
    #include BOOST_MMAP_IMPL_FILE
#else
    // do nothing
#endif // BOOST_MMAP_HEADER_ONLY

#undef BOOST_MMAP_IMPL_FILE