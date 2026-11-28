.PHONY: knytos-inspect
knytos-inspect:
	@echo $(ERLANG_OSTYPE)
	@echo $(OPSYS)
	@echo $(OS_OBJS)
	@echo drivers=$(DRV_OBJS)
