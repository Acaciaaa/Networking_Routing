#MakeFile
all:router agent

router:  common.c common.h router.c
	gcc -o router common.c common.h router.c
agent:  common.c common.h agent.c
	gcc -o agent common.c common.h agent.c
 
.PHONY:clean
clean:
	rm *.o router agent
