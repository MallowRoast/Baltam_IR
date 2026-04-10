% 变量赋值出现在使用后
function test40
	for i = 1:10
		if (i == 5)
			disp(x);
		end
		if (i == 4)
			x = [1 2 3];
		end
	end
end
