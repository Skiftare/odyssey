struct iovec {
    void  *iov_base;  /* Начальный адрес буфера памяти */
    size_t iov_len;   /* Размер памяти в байтах, на которую указывает iov_base */
};
