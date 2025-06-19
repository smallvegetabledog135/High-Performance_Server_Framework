# 只生成 .d,.o即可   
BIN =   
include $(BUILD_ROOT)/common.mk  

# 修改：生成到 app/link_obj/ 而不是 app/link_obj/logic/  
$(BUILD_ROOT)/app/link_obj/library_manager.o: library_manager.cpp  
	@mkdir -p $(dir $@)  
	g++ -std=c++11 -g -I$(BUILD_ROOT)/_include -o $@ -c $<  

# 修改依赖文件路径  
$(BUILD_ROOT)/app/dep/library_manager.d: library_manager.cpp  
	@mkdir -p $(dir $@)  
	@echo -n $(BUILD_ROOT)/app/link_obj/ > $@  
	g++ -std=c++11 -g -I$(BUILD_ROOT)/_include -MM $< >> $@  

# 修改 all 目标  
all: $(BUILD_ROOT)/app/link_obj/library_manager.o  

# 包含依赖文件  
-include $(BUILD_ROOT)/app/dep/library_manager.d