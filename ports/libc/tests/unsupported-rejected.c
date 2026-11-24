/* No POSIX filesystem or fake successful open is supplied by this profile. */
extern int open(const char *, int, ...);
int main(void) { return open("/unsupported", 0); }
