%include <pybuffer.i>

%module delta
%{
void   delta_init(void);
void ncq_submit(char * fraguf, size_t sizee);
void ncq_read(char * fraguf, size_t sizee);
%}

void   delta_init(void);

%pybuffer_mutable_binary(char *fragbuf, size_t sizee);
void ncq_submit(char *fragbuf, size_t sizee);
void ncq_read(char *fragbuf, size_t sizee);


