#include "tap.c"

int main(int argc, char **argv)
{
	plan_tests(1);
	pass("this is a test");

	return exit_status();
}

