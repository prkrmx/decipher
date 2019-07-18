%include <pybuffer.i>

%module libvankus
%{
int burst_load(char * cbuf, size_t sizee);
int getnumfree();
int frag_clblob(char * cbuf, size_t sizee);
void report(char * cbuf, size_t sizee);
int pop_result(char * cbuf, size_t sizee);
int pop_solution(char * cbuf, size_t sizee);
%}

%pybuffer_mutable_binary(char * cbuf, size_t sizee);
int burst_load(char * cbuf, size_t sizee);
int getnumfree();
int frag_clblob(char * cbuf, size_t sizee);
void report(char * cbuf, size_t sizee);
int pop_result(char * cbuf, size_t sizee);
int pop_solution(char * cbuf, size_t sizee);
